#ifndef MEMLAYOUT_H
#define MEMLAYOUT_H
// 1. 内核加载的物理基地址 (参考你的 kernel.ld)
// QEMU 是 0x80000000，K230 我们定在了 2MB 处
#define KERNBASE 0x00200000L

// 2. 物理内存结束地址 (PHYSTOP)
// K230 有 512MB 或 1GB 内存。
// 但为了安全起见，我们在裸机实验阶段先只管理 128MB。
// 任何超过 PHYSTOP 的地址，kalloc 都不会去触碰，防止踩到未知的硬件区域。
#define PHYSTOP (KERNBASE + 128*1024*1024) 

// #define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 3. 页大小 (RISC-V 标准)
#define PGSIZE 4096

// 4. 串口地址 (你已经验证过是正确的)
#define UART0 0x91400000L
#define VIRTIO0_IRQ 1

#define UART0_IRQ 16

// K230 C908 PLIC 地址
// 物理地址在 0xf00000000，但 Sv39 无法直接映射
// 我们将其映射到虚拟地址 0x10000000
#define PLIC_PA             0x0f00000000L  // 物理地址
#define PLIC                0x0f00000000L    // 虚拟地址（内核访问用这个）

// PLIC 寄存器偏移（相对于 PLIC 虚拟地址）
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)
// 5. PLIC 中断控制器 (Platform Level Interrupt Controller)
// K230 的 PLIC 地址需要查手册。
// 根据 K230 SDK 或设备树，K230 的 PLIC 基地址通常在 0xF0000000 附近，但也可能不同。
// 暂时先留空或写个 TODO，等做到中断 (Trap) 那一章时再填。
// #define PLIC 0x.............. 

// 6. 虚拟内存映射的映射表 (未来做 kvminit 时用到)
// xv6 会把物理内存映射到虚拟地址的这个位置
#define KERN_VIRT_BASE 0x80000000L


// 7. 用户栈和陷阱帧位置
#define TRAMPOLINE (MAXVA - PGSIZE)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

#endif // MEMLAYOUT_H