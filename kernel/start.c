#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "param.h"


// PMP 配置位定义 (仅在 start.c 中使用)
#define PMP_R       0x01
#define PMP_W       0x02
#define PMP_X       0x04
#define PMP_A_NAPOT 0x18
// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096*NCPU];
void main();
void timer_m_mode_enable();
void
timer_s_init();
void start()
{
// 1. (建议) 清空 BSS 段
    extern char edata[], end[];
    for (char *p = edata; p < end; p++) {
        *p = 0;
    }



    // set M Previous Privilege mode to Supervisor, for mret.
    // 2. 配置 mstatus：设置 MPP 位为 S-Mode
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  // 3. 设置 mepc：mret 跳转后的目标地址
    // 我们让它跳转到新的 main() 函数
    w_mepc((uint64)main);

  // disable paging for now.
    w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  // 5. 委托中断和异常：将所有的异常和中断交给 S-Mode 处理
  //   // 这对后面在 S-Mode 下处理串口/时钟中断至关重要
  w_medeleg(0xffff & ~(1 << 9));
  w_mideleg(0xffff);

  w_sie(r_sie() | SIE_SEIE | SIE_STIE);
  // timer_m_mode_enable();  
  timer_s_init();
  

  // 4. [关键步骤] 解锁并验证 PLIC_CTRL (必须在 M-Mode 且无 Cache 干扰时进行)
    volatile uint32 *plic_ctrl_pa = (uint32*)(PLIC + 0x01FFFFC);
    *plic_ctrl_pa = 1;
    
   
  asm volatile("csrw pmpaddr3, %0" : : "r" (-1ULL));


    // pmpcfg0: 配置 Entry 3 (Bits 24-31)
    // R/W/X 权限 + NAPOT 模式
    uint64 cfg = (PMP_R | PMP_W | PMP_X | PMP_A_NAPOT) << 24;
    w_pmpcfg0(cfg);

 

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  // 6. 执行 mret！
    // 这一步会跳转到 main() 并且 CPU 模式变为 S-Mode
  
  asm volatile("mret");
}

// sstc模式
void
timer_m_mode_enable()
{
     // 开启 MXSTATUS.CLINTEE (Bit 17): 允许 S-mode 响应 CLINT 中断
    uint64 mxs = r_mxstatus();
    mxs |= MXSTATUS_CLINTEE;
    mxs |= MXSTATUS_THEADISAEE; // 允许玄铁扩展指令
    w_mxstatus(mxs);

  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);
  
   // 开启 MENVCFG.STCE (Bit 63): Sstc 扩展
    // 允许 S-mode 直接写 stimecmp 寄存器，无需 SBI 调用
    uint64 menv = r_menvcfg();
    menv |= MENVCFG_STCE;
    w_menvcfg(menv);
  
  // 允许 S-mode 访问所有硬件计数器 (time, cycle...)
    w_mcounteren(0xffffffff);
}

// sstc模式
void
timer_s_init()
{
 // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);
  
  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);
  
  // ask for the very first timer interrupt. 1000000
  w_stimecmp(r_time() +1000000);
}
