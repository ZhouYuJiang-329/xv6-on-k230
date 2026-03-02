#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
// #include "spinlock.h"
#include "proc.h"
#include "defs.h"
struct spinlock tickslock;
uint ticks;
extern char trampoline[], uservec[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();
extern int devintr();
void
clockintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// 设备中断分发
int devintr() {
    uint64 scause = r_scause();
    // printf("devintr: scause=0x%d\n", scause);

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();
    //  printf("irq=%d\n", irq);
    // printf("devintr: scause=0x%d\n", scause);
    

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      // virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    // printf("timer interrupt received\n");
    return 2;
  } else {
    return 0;
  }
}

// 所有的内核中断/异常都会跳到这里
void kerneltrap(void) {
  // printf("kerneltrap: start\n");
    int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%p sepc=0x%p stval=0x%p\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 )
    // yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }
  //  ticks++;
    // printf(".");    
  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

uint64 usertrap(void) {
  int which_dev = 0;
  // printf("usertrap: start\n");
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  
    

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);  //DOC: kernelvec

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      kexit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.


    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  }
  else if((r_scause() == 15 || r_scause() == 13) ) {
    if(vmfault(p->pagetable, r_stval(), (r_scause() == 13) ? 1 : 0) != 0){
				// 成功处理缺页
			} else {
				// 处理缺页失败，杀死进程
				// printf("usertrap(): vmfault failed pid=%d scause=0x%p stval=0x%p\n",
				// 	p->pid, r_scause(), r_stval());
				// setkilled(p);
			}
    // page fault on lazily-allocated page
  }
  else {
         printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  // // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);

  // return to trampoline.S; satp value in a0.
  return satp;
}