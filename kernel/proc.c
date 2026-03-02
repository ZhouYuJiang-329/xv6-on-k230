#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "initcode.h"

struct cpu cpus[NCPU];
struct proc proc[NPROC];
int nextpid = 1;
struct proc *initproc;
struct spinlock pid_lock;
struct spinlock wait_lock;
extern void forkret(void);
static void freeproc(struct proc *p);


extern char trampoline[]; // trampoline.S

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    p->kstack = va;
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W | PTE_THEAD_MAEE | PTE_A | PTE_D);
  }
}

void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      // p->kstack = KSTACK((int) (p - proc));
  }
 
}


// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}
// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}
// 初始化当前 CPU 的状态
void cpuinit()
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    
    c->noff = 0;
    c->intena = 0;
    c->proc = 0;
}



// 获取当前进程
struct proc* myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        //  printf("[Scheduler] CPU %d: Switching to process %d\n", cpuid(), p->pid);
        // printf("process name: %s\n",p->name);
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        //  printf("[Scheduler] CPU %d: Returned from process %d\n", cpuid(), p->pid);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}


// 切换到调度器
// 必须持有 p->lock，并且已经改变 proc->state
// 保存和恢复 intena，因为 intena 是此内核线程的属性，而不是此 CPU 的属性
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 让出 CPU 给其他进程
// 获取锁，改变状态，调用 sched() 切换到调度器
void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}






// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X | PTE_A | PTE_THEAD_MAEE) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W | PTE_A | PTE_D | PTE_THEAD_MAEE) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // 此处可添加文件系统初始化等操作

    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);
    
    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }

  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}


static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  
  p->context.ra = (uint64)forkret;
  
  p->context.sp = p->kstack + PGSIZE;
  return p;
}




void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // 1. 设置进程名
  safestrcpy(p->name, "init", sizeof(p->name)); 

  // 2. 设置工作目录 (必须！)
  p->cwd = namei("/"); 

  // 3. 这里的内存分配 (uvminit) 可以删掉了
  // 因为 kexec 会负责分配新的内存页表并加载 ELF。
  // allocproc 已经为我们分配了一个空的页表 (包含 trampoline 和 trapframe 映射)，这就足够了。
  p->sz = 0; 

  // 4. Trapframe 设置也可以简化
  // kexec 会重置 epc (入口点) 和 sp (栈指针)
  // 所以这里不需要设 epc 和 sp
  
  // 5. 不要在这里打开文件描述符 (FD)
  // 让 /init 程序自己去 open("console")，这样符合 UNIX 标准行为。

  // 6. 就绪
  p->state = RUNNABLE;

  release(&p->lock);
}


// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}
// ============================================================================
//                            进程创建和退出
// ============================================================================

/*
 * kfork - 创建当前进程的子进程
 * 
 * 返回值：
 * - 父进程中：返回子进程的 PID
 * - 子进程中：返回 0
 * - 失败：返回 -1
 * 
 * 实现步骤：
 * 1. 调用 allocproc() 分配新进程结构
 * 2. 使用 uvmcopy() 复制父进程的用户内存
 * 3. 复制父进程的 trapframe（保存所有寄存器状态）
 * 4. 设置子进程的返回值为 0（通过修改 trapframe->a0）
 * 5. 复制进程名称
 * 6. 设置父子进程关系
 * 7. 标记子进程为 RUNNABLE
 * 
 * 关键点：
 * - 子进程继承父进程的用户空间内存、寄存器状态
 * - 子进程从 fork 调用点返回，但返回值为 0
 * - 父进程返回子进程 PID
 */
// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int  pid;
  int i;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);
  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);


  return pid;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

/*
 * reparent - 将进程 p 的所有子进程重新分配给 initproc
 * 
 * 当进程退出时，需要将其子进程转移给 init 进程
 * 必须持有 wait_lock
 */
// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}
/*
 * kexit - 终止当前进程
 * @status: 退出状态码
 * 
 * 步骤：
 * 1. 检查是否为 init 进程（不允许退出）
 * 2. 将子进程重新分配给 init
 * 3. 唤醒父进程（可能在 wait 中等待）
 * 4. 设置状态为 ZOMBIE
 * 5. 调用 sched() 切换到调度器，永不返回
 * 
 * 注意：进程资源在父进程调用 wait() 时才会被释放
 */
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  // for(int fd = 0; fd < NOFILE; fd++){
  //   if(p->ofile[fd]){
  //     struct file *f = p->ofile[fd];
  //     fileclose(f);
  //     p->ofile[fd] = 0;
  //   }
  // }

  // begin_op();
  // iput(p->cwd);
  // end_op();
  // p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}
/*
 * kwait - 等待任意子进程退出
 * @addr: 用户空间地址，用于存储子进程退出状态
 * 
 * 返回：
 * - 成功：子进程的 PID
 * - 失败：-1（没有子进程）
 * 
 * 扫描进程表查找已退出的子进程：
 * - 找到 ZOMBIE 子进程：回收资源并返回其 PID
 * - 有子进程但未退出：sleep 等待
 * - 没有子进程：返回 -1
 */
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();
  
  acquire(&wait_lock);

  for(;;){
    // 检查是否有子进程
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
           // 找到已退出的子进程，回收资源
          // Found one.
          pid = pp->pid;
           // 如果提供了地址，复制退出状态到用户空间
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }
    // 没有子进程，或者被杀死
    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    // 有子进程但没有退出的，睡眠等待
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}
// ============================================================================
//                            进程同步原语
// ============================================================================

/*
 * sleep - 原子地释放锁并睡眠
 * @chan: 等待通道（任意地址，用于唤醒时匹配）
 * @lk: 调用时持有的锁（sleep 返回时会重新获取）
 * 
 * 睡眠在通道 chan 上，释放锁 lk，重新获取 lk 后返回
 * 
 * 关键：释放 lk 和设置状态为 SLEEPING 必须是原子的
 * 否则可能发生：
 * 1. 进程 A 释放 lk
 * 2. 进程 B 获取 lk，修改条件，调用 wakeup(chan)
 * 3. 进程 A 设置状态为 SLEEPING（错过了 wakeup）
 */
// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}
/*
 * wakeup - 唤醒所有睡眠在通道 chan 上的进程
 * @chan: 等待通道
 * 
 * 扫描进程表，将所有睡眠在 chan 上的进程设为 RUNNABLE
 * 
 * 注意：wakeup 必须持有每个进程的 p->lock
 * 调用者不需要持有任何特定的锁
 */
// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
// ============================================================================
//                            进程终止信号
// ============================================================================

/*
 * kkill - 向进程发送终止信号
 * @pid: 目标进程 ID
 * 
 * 返回：0 成功，-1 失败（进程不存在）
 * 
 * 注意：kill 只是设置 killed 标志，不会立即终止进程
 * 进程会在下次从内核返回用户空间或 sleep 时检查该标志并退出
 */
// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      // 如果进程在睡眠，唤醒它以便能检查 killed 标志
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

/*
 * setkilled - 标记进程为已被终止
 * 
 * 设置 p->killed 为 1，表示该进程应退出
 */
void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}
/*
 * killed - 检查当前进程是否被标记为终止
 * 
 * 返回：1 表示已被 kill，0 表示正常
 * 
 * 用于在关键点检查进程是否应该退出
 * 例如：系统调用返回前、长时间循环中等
 */
int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}
// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 测试函数 A - 循环打印 'A'
void test_func_a(void) {
  struct proc *p = myproc();
  // 【关键修复】释放 scheduler 移交过来的锁
  // 当 scheduler 首次 swtch 到此进程时，它持有 p->lock
  // 需要在进入正常执行前释放该锁
  release(&p->lock);
  
  for(;;) {
    printf("A");
    // 模拟耗时工作
    for(volatile int i=0; i<1000000; i++); 
    yield(); // 主动让出 CPU
  }
}

// 测试函数 B - 循环打印 'B'
void test_func_b(void) {
  struct proc *p = myproc();
  // 【关键修复】释放 scheduler 移交过来的锁
  // 理由同 test_func_a
  release(&p->lock);
  
  for(;;) {
    printf("B");
    for(volatile int i=0; i<1000000; i++); 
    yield();
  }
}

// 手动创建测试进程
// 为两个测试函数分别创建进程，设置其上下文使其能被调度执行
void test_proc_init(void) {
    struct proc *p;
    char *sp;

    // === 创建进程 A ===
    p = &proc[0];
    p->state = RUNNABLE;  // 设为可运行状态
    p->pid = 3;
    
    // 分配内核栈（假设 kalloc 已初始化）
    p->kstack = (uint64)kalloc(); 
    if(p->kstack == 0) 
        panic("kalloc failed");
    
    // 【关键】伪造进程上下文
    // 当 scheduler 第一次 swtch 到此进程时：
    // - swtch 会恢复 ra 寄存器（返回地址）
    // - swtch 的 ret 指令会跳转到 ra（即 test_func_a）
    // - sp 指向进程的内核栈顶
    sp = (char *)(p->kstack + PGSIZE);   // 栈顶地址
    p->context.ra = (uint64)test_func_a; // 返回地址 = 函数入口
    p->context.sp = (uint64)sp;          // 栈指针

    // === 创建进程 B ===
    p = &proc[1];
    p->state = RUNNABLE;
    p->pid = 4;
    
    p->kstack = (uint64)kalloc();
    if(p->kstack == 0) 
        panic("kalloc failed");

    sp = (char *)(p->kstack + PGSIZE);
    p->context.ra = (uint64)test_func_b;
    p->context.sp = (uint64)sp;
}