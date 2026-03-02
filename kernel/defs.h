#ifndef DEFS_H
#define DEFS_H

// Forward declarations
struct spinlock;
struct cpu;
struct proc;
struct trapframe;

struct context;
struct buf;     // 新增: Buffer Cache
struct inode;   // 新增: Inode
struct file;    // 新增: File descriptor
struct stat;    // 新增: File status
struct dirent;  // 新增: Directory entry
struct pipe;    // 新增: Pipe
struct sleeplock;   // 新增: Sleep lock
struct superblock; // 新增: Superblock
// uart.c

void consoleinit(void);
void printfinit(void);
void printf(char*, ...);
void panic(char*) __attribute__((noreturn));
void consoleintr(int); // 供 trap.c 调用 (如果没有合并 uartintr)
void uartintr(void);   // 供 trap.c 调用

// kalloc.c
void* kalloc(void);
void kfree(void *);
void kinit(void);


// string.c
void* memset(void *dst, int c, uint n);
void* memmove(void *dst, const void *src, uint n);
void* memcpy(void *dst, const void *src, uint n);
int
memcmp(const void *v1, const void *v2, uint n);
int
strncmp(const char *p, const char *q, uint n);
char*
strncpy(char *s, const char *t, int n);
char*
safestrcpy(char *s, const char *t, int n);
int
strlen(const char *s);

// vm.c
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, uint64 perm);
void test_vm(void);
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, uint64 perm);
pagetable_t kvmmake(void);
void kvminit(void);
void kvminithart();
void kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, uint64 perm);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
void uvmfree(pagetable_t pagetable, uint64 sz);
pagetable_t uvmcreate();

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
void uvminit(pagetable_t pagetable, uchar *src, uint sz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);
void uvmclear(pagetable_t pagetable, uint64 va);
uint64
walkaddr(pagetable_t pagetable, uint64 va);
uint64 vmfault(pagetable_t, uint64, int);
//trap.c
void trapinit(void);
void trapinithart(void);
uint64 usertrap(void);
extern struct spinlock tickslock;
extern uint     ticks;

// syscall.c
void            argint(int, int*);
int             argstr(int, char*, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall();
// 获得固定数组的数量
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// timer.c
void set_timer(uint64 stime_value);
void timerinit(void);

// proc.c
struct cpu*mycpu(void);
int cpuid();
void cpuinit();
// --- 进程表初始化 ---
void procinit(void);
void test_proc_init(void);
pagetable_t proc_pagetable(struct proc *p);
void proc_mapstacks(pagetable_t kpgtbl);
void procinit(void);
void userinit(void);
void prepare_return(void);
struct proc* myproc(void);
// --- 调度器 ---
void scheduler(void);
void    yield(void);
int kfork(void);            // 创建子进程
void kexit(int status);        // 终止当前进程
int kwait(uint64 addr);        // 等待子进程退出并回收资源
// --- 睡眠与唤醒 ---
void sleep(void *chan, struct spinlock *lk);  // 睡眠等待 chan，释放 lk
void wakeup(void *chan);                      // 唤醒等待 chan 的所有进程
// --- 信号和终止 ---
int kkill(int pid);            // 向指定进程发送终止信号
int killed(struct proc *p);    // 检查进程是否被标记为 killed
void setkilled(struct proc *p);// 设置进程的 killed 标志
void procdump(void);                     // 打印进程列表（调试用）

int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len);
int         growproc(int n);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);  // 释放进程页表

// swtch.S - 上下文切换
void swtch(struct context*, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);


// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// ============================================================================
// 程序执行 (exec.c)
// ============================================================================
int             kexec(char*, char**);                       // 加载并执行用户程序

// ============================================================================
// 睡眠锁 (sleeplock.c)
// ============================================================================
void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);

// ============================================================================
// 文件系统与驱动 (Buffer Cache, FS, Ramdisk)
// ============================================================================

// --- Buffer Cache (bio.c) ---
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// --- File System (fs.c) ---
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);
void            ireclaim(int);

// --- File Layer (file.c) ---
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// --- Logging (log.c) ---
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// --- Pipe (pipe.c) ---
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// --- Ramdisk Driver (ramdisk.c) ---
void            ramdisk_init(void);
void            ramdisk_rw(struct buf*, int);  // <--- 增加了 int 参数
// trampoline.S - 用户态/内核态切换
extern char trampoline[];  // trampoline 页的起始地址
extern char uservec[];     // 用户态陷阱入口
extern char userret[];     // 返回用户态的代码

#endif // DEFS_H
