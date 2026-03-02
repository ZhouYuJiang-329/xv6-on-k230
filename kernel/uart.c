#include <stdarg.h>
#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

// ====================================================================
// SECTION 1: K230 UART 硬件定义 (Driver Layer)
// ====================================================================

// K230 UART0 寄存器访问宏 (32位对齐，4字节步进)
#define Reg(reg) ((volatile uint32 *)(UART0 + (reg) * 4))
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// DW8250 寄存器定义
#define RHR 0                 // Receive Holding Register (Read)
#define THR 0                 // Transmit Holding Register (Write)
#define IER 1                 // Interrupt Enable Register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO Control Register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1)
#define ISR 2                 // Interrupt Status Register
#define LCR 3                 // Line Control Register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7)
#define MCR 4                 // Modem Control Register
#define LSR 5                 // Line Status Register
#define LSR_RX_READY (1<<0)
#define LSR_TX_IDLE (1<<5)
#define USR 31                // K230/DW DesignWare Extension

// 全局状态
volatile int panicking = 0; // 正在 panic
volatile int panicked = 0;  // panic 完成，冻结输出

// UART 发送锁与同步通道
static struct spinlock uart_tx_lock;
static int uart_tx_busy = 0;  // UART 是否正在通过中断发送数据
static int uart_tx_chan;      // sleep channel

// 前向声明
void uartputc_sync(int c);
void consoleintr(int c);

// ====================================================================
// SECTION 2: UART 底层驱动实现
// ====================================================================

void
uartinit(void)
{
  initlock(&uart_tx_lock, "uart");

  // 1. 关闭中断
  WriteReg(IER, 0x00);

  // 2. 配置 8n1
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 3. 复位 FIFO
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 4. 配置 Modem 信号 (RTS/DTR) 和中断总开关 (OUT2)
  // 注意：K230 可能不需要 OUT2，但加上无害
  WriteReg(MCR, 0x0B); 

  // 5. 使能接收和发送中断
  // xv6 的机制依赖 TX 中断来唤醒 uartwrite
  WriteReg(IER, IER_RX_ENABLE | IER_TX_ENABLE);
}

// 同步写字符（忙等待），用于 printf 和 echo
// 自动处理换行符 \n -> \r\n
void
uartputc_sync(int c)
{
  if(panicking == 0)
    push_off(); // 关中断防止死锁

  if(panicked){
    for(;;)
      ;
  }

  // 换行符处理
  if(c == '\n'){
      while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
        ;
      WriteReg(THR, '\r');
  }

  // 等待发送空闲
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  if(panicking == 0)
    pop_off();
}

// 供系统调用 write() 使用，支持睡眠等待
void
uartwrite(char buf[], int n)
{
  acquire(&uart_tx_lock);

  int i = 0;
  while(i < n){
    // 如果是换行符，先发送 \r
    if(buf[i] == '\n'){
      while(uart_tx_busy){
        sleep(&uart_tx_chan, &uart_tx_lock);
      }
      WriteReg(THR, '\r');
      uart_tx_busy = 1;
    }
    
    // 等待上一轮发送完成
    while(uart_tx_busy){
      sleep(&uart_tx_chan, &uart_tx_lock);
    }
    
    // 发送当前字符
    WriteReg(THR, buf[i]);
    uart_tx_busy = 1;
    i++;
  }

  release(&uart_tx_lock);
}

// 底层非阻塞读
int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY){
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// UART 中断处理程序 (Top Level ISR)
// 需要在 trap.c 的 devintr 中调用
void
uartintr(void)
{
  // 1. 读取 ISR 应答中断 (DW IP 要求)
  volatile int id = ReadReg(ISR);
  (void)id;

  // 2. 处理发送完成 (TX)
  acquire(&uart_tx_lock);
  if(ReadReg(LSR) & LSR_TX_IDLE){
    if(uart_tx_busy){
      uart_tx_busy = 0;
      wakeup(&uart_tx_chan);
    }
  }
  release(&uart_tx_lock);

  // 3. 处理接收数据 (RX)
  while(1){
    int c = uartgetc();
    
    // K230 Ghost Interrupt 处理
    if(c == -1){
        volatile uint32 usr = ReadReg(USR);
        (void)usr;
        if((ReadReg(LSR) & LSR_RX_READY) == 0) break;
        continue;
    }

    // 交给 console 层处理 (Input Line Discipline)
    consoleintr(c);
  }
}

// ====================================================================
// SECTION 3: Console 逻辑 (缓冲与行律)
// ====================================================================

struct {
  struct spinlock lock;
  #define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

// 辅助函数：输出到 UART 且带锁保护（可选）
// 这里直接用 uartputc_sync 简化依赖
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 退格处理：退格 -> 空格 -> 退格
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

// 系统调用 read() 入口
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // 等待一行输入完成
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // EOF
      if(n < target){
        // 如果已经读了一些数据，把 EOF 留给下一次，保证这次返回 > 0
        cons.r--;
      }
      break;
    }

    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

// 系统调用 write() 入口
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32]; // 分块拷贝
  int i = 0;

  while(i < n){
    int nn = sizeof(buf);
    if(nn > n - i)
      nn = n - i;
    if(either_copyin(buf, user_src, src+i, nn) == -1)
      break;
    
    uartwrite(buf, nn);
    i += nn;
  }
  return i;
}

// 接收中断逻辑：处理特殊按键
void
consoleintr(int c)
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print Process List
    procdump(); // 暂时注释，等你 proc.c 就绪后打开
    break;
  case C('U'):  // Kill Line
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('X'):
    panic("User triggered panic!");
    break;
  case C('C'):  // Ctrl-C
    {
      struct proc *p = myproc();
      if(p && p->pid > 1) { // 保护 init 进程不被杀
        setkilled(p);
        consputc('^');
        consputc('C');
      }
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c; // CR -> LF

      // 回显
      consputc(c);

      // 存入 buffer
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      // 如果是换行或缓冲区满，唤醒读取者
      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  // 物理硬件初始化
  uartinit();

  // 绑定 VFS 设备表
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;

  printfinit();
}

// ====================================================================
// SECTION 4: Printf & Panic (格式化输出)
// ====================================================================

static struct {
  struct spinlock lock;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(long long xx, int base, int sign)
{
  char buf[20];
  int i;
  unsigned long long x;

  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uartputc_sync(buf[i]); // 使用同步输出
}

static void
printptr(uint64 x)
{
  int i;
  uartputc_sync('0');
  uartputc_sync('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    uartputc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

void
printf(char *fmt, ...)
{
  va_list ap;
  int i, cx, c0, c1, c2;
  char *s;

  if(panicking == 0)
    acquire(&pr.lock);

  va_start(ap, fmt);
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){
      uartputc_sync(cx);
      continue;
    }
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;

    if(c0 == 'd'){
      printint(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'x'){
      printint(va_arg(ap, int), 16, 0);
    } else if(c0 == 'p'){
      printptr(va_arg(ap, uint64));
    } else if(c0 == 's'){
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uartputc_sync(*s);
    } else if(c0 == '%'){
      uartputc_sync('%');
    } else {
      uartputc_sync('%');
      uartputc_sync(c0);
    }
  }
  va_end(ap);

  if(panicking == 0)
    release(&pr.lock);
}

void
panic(char *s)
{
  intr_off();

  panicking = 1;
  printf("\n");
  printf("panic: ");
  printf("%s\n", s);
  printf("Rebooting K230...\n");
  panicked = 1; // 冻结其他 CPU 的输出

  // 调用 K230 硬件复位
  // 假设 k230_wdt_reboot() 在其他地方定义，或者在这里 extern
//   k230_wdt_reboot();
  
  // 如果复位失败，死循环
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
}