#ifndef RISCV_H
#define RISCV_H

// 汇编代码不需要包含 types.h
#ifndef __ASSEMBLER__
#include "types.h"
#endif

// ====================================================================
// 1. K230/C908 特有扩展
// ====================================================================

// 时钟频率：27MHz，定时器间隔 10ms
#define CLOCK_INTERVAL 270000

// MXSTATUS (0x7C0) - 机器模式扩展状态寄存器
#define CSR_MXSTATUS        0x7C0
#define MXSTATUS_CLINTEE    (1L << 17) // 允许 S-mode 响应 CLINT 中断
#define MXSTATUS_MAEE       (1L << 21) // 扩展 MMU 属性使能
#define MXSTATUS_THEADISAEE (1L << 22) // 玄铁扩展指令集使能

// MENVCFG (0x30A) - 机器环境配置寄存器
#define CSR_MENVCFG         0x30A
#define MENVCFG_STCE        (1ULL << 63) // Sstc 扩展：允许 S-mode 使用 stimecmp

// ====================================================================
// 2. RISC-V 特权级寄存器位定义
// ====================================================================

// --- Machine Status (mstatus) ---
#define MSTATUS_MPP_MASK (3L << 11)  // 前一特权级掩码
#define MSTATUS_MPP_M    (3L << 11)  // 机器模式
#define MSTATUS_MPP_S    (1L << 11)  // 监管模式
#define MSTATUS_MPP_U    (0L << 11)  // 用户模式
#define MSTATUS_MIE      (1L << 3)   // 机器中断使能

// --- Supervisor Status (sstatus) ---
#define SSTATUS_SPP      (1L << 8)   // 前一特权级 (0=User, 1=Supervisor)
#define SSTATUS_SPIE     (1L << 5)   // 异常前中断使能位
#define SSTATUS_SIE      (1L << 1)   // 监管模式中断使能

// --- Supervisor Interrupt Enable (sie) ---
#define SIE_SSIE (1L << 1) // 软件中断使能
#define SIE_STIE (1L << 5) // 时钟中断使能
#define SIE_SEIE (1L << 9) // 外部中断使能

// ====================================================================
// 3. 内存管理与分页 (Sv39)
// ====================================================================

// 虚拟地址空间
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1)) // Sv39: 最大虚拟地址
#define PGSIZE 4096                         // 页面大小 4KB
#define PGSHIFT 12                          // 页面偏移位数

// 页面对齐宏
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))

// 页表项 (PTE) 标志位
#define PTE_V (1L << 0) // 有效位
#define PTE_R (1L << 1) // 可读
#define PTE_W (1L << 2) // 可写
#define PTE_X (1L << 3) // 可执行
#define PTE_U (1L << 4) // 用户可访问
#define PTE_G (1L << 5) // 全局映射
#define PTE_A (1L << 6) // 已访问 (K230 需手动设置)
#define PTE_D (1L << 7) // 已修改 (K230 需手动设置)

// T-Head C908 扩展 PTE 标志位 (RSW 位段，bit 8-9)
// MAEE = Memory Access Extension Enable
// K230 的 C908 核心需要这个位来启用缓存和原子操作
#define PTE_THEAD_MAEE  ((1L << 62) | (1L << 61) | (1L << 60))
// [新增] T-Head C908 特有页表属性
// Bit 63: Strong Order (SO) - 用于 MMIO 设备，禁止 Cache 和乱序
// Bit 62: Cacheable (C) - 用于内存
// Bit 61: Bufferable (B)
// Bit 60: Shareable (S)
// Bit 59: Secondary (Sec)
// 我们定义一个 PTE_IO 宏，专门用于映射外设
#define PTE_THEAD_SO   (1L << 63)
#define PTE_IO         (PTE_R | PTE_W | PTE_THEAD_SO)

// PTE 与物理地址转换
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)  // 物理地址 → PTE
#define PTE2PA(pte) ((((pte) >> 10) << 12) & 0x00FFFFFFFFFFFF00L) 
#define PTE_FLAGS(pte) ((pte) & 0x3FF)           // 提取标志位

// 页表索引计算 (Sv39 三级页表)
#define PXMASK          0x1FF                    // 9位索引掩码
#define PXSHIFT(level)  (PGSHIFT + (9*(level)))  // 各级偏移
#define PX(level, va)   ((((uint64)va) >> PXSHIFT(level)) & PXMASK)

// SATP 寄存器配置
#define SATP_SV39 (8L << 60)                     // Sv39 模式
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// ====================================================================
// C 语言专用部分（类型定义和内联函数）
// ====================================================================
#ifndef __ASSEMBLER__

// 类型定义
typedef uint64 pte_t;        // 页表项类型
typedef uint64 *pagetable_t; // 页表指针类型

// ====================================================================
// 4. CSR 读写辅助函数
// ====================================================================

// --- K230 扩展寄存器 ---

static inline uint64 r_mxstatus() {
    uint64 x; asm volatile("csrr %0, %1" : "=r" (x) : "i" (CSR_MXSTATUS)); return x;
}
static inline void w_mxstatus(uint64 x) {
    asm volatile("csrw %0, %1" : : "i" (CSR_MXSTATUS), "r" (x));
}

static inline uint64 r_menvcfg() {
    uint64 x; asm volatile("csrr %0, %1" : "=r" (x) : "i" (CSR_MENVCFG)); return x;
}
static inline void w_menvcfg(uint64 x) {
    asm volatile("csrw %0, %1" : : "i" (CSR_MENVCFG), "r" (x));
}

// Sstc 扩展：直接写 stimecmp (0x14D)，无需 ecall
static inline void w_stimecmp(uint64 x) {
    asm volatile("csrw 0x14D, %0" : : "r" (x));
}
static inline uint64 r_sp(){
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}
static inline uint64 r_stimecmp() {
    uint64 x; asm volatile("csrr %0, 0x14D" : "=r" (x)); return x;
}

// --- Machine 模式寄存器 ---

static inline uint64 r_mstatus() {
    uint64 x; asm volatile("csrr %0, mstatus" : "=r" (x)); return x;
}
static inline void w_mstatus(uint64 x) {
    asm volatile("csrw mstatus, %0" : : "r" (x));
}

static inline uint64 r_mhartid() {
    uint64 x; asm volatile("csrr %0, mhartid" : "=r" (x)); return x;
}

static inline void w_mepc(uint64 x) {
    asm volatile("csrw mepc, %0" : : "r" (x));
}

static inline void w_medeleg(uint64 x) {
    asm volatile("csrw medeleg, %0" : : "r" (x));
}

static inline void w_mideleg(uint64 x) {
    asm volatile("csrw mideleg, %0" : : "r" (x));
}

static inline void w_mcounteren(uint64 x) {
    asm volatile("csrw mcounteren, %0" : : "r" (x));
}

// PMP 配置
static inline void w_pmpcfg0(uint64 x) {
    asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

// --- Supervisor 模式寄存器 ---

static inline uint64 r_sstatus() {
    uint64 x; asm volatile("csrr %0, sstatus" : "=r" (x)); return x;
}
static inline void w_sstatus(uint64 x) {
    asm volatile("csrw sstatus, %0" : : "r" (x));
}

static inline uint64 r_sepc() {
    uint64 x; asm volatile("csrr %0, sepc" : "=r" (x)); return x;
}
static inline void w_sepc(uint64 x) {
    asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64 r_scause() {
    uint64 x; asm volatile("csrr %0, scause" : "=r" (x)); return x;
}

static inline uint64 r_stval() {
    uint64 x; asm volatile("csrr %0, stval" : "=r" (x)); return x;
}

static inline uint64 r_stvec() {
    uint64 x; asm volatile("csrr %0, stvec" : "=r" (x)); return x;
}
static inline void w_stvec(uint64 x) {
    asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64 r_sie() {
    uint64 x; asm volatile("csrr %0, sie" : "=r" (x)); return x;
}
static inline void w_sie(uint64 x) {
    asm volatile("csrw sie, %0" : : "r" (x));
}

static inline uint64 r_sip() {
    uint64 x; asm volatile("csrr %0, sip" : "=r" (x)); return x;
}
static inline void w_sip(uint64 x) {
    asm volatile("csrw sip, %0" : : "r" (x));
}

// SATP (地址翻译与保护)
static inline uint64 r_satp() {
    uint64 x; asm volatile("csrr %0, satp" : "=r" (x)); return x;
}
static inline void w_satp(uint64 x) {
    asm volatile("csrw satp, %0" : : "r" (x));
}

// --- 其他寄存器 ---

// 线程指针 (tp)
static inline uint64 r_tp() {
    uint64 x; asm volatile("mv %0, tp" : "=r" (x)); return x;
}
static inline void w_tp(uint64 x) {
    asm volatile("mv tp, %0" : : "r" (x));
}

// 时间计数器
static inline uint64 r_time() {
    uint64 x; asm volatile("csrr %0, time" : "=r" (x)); return x;
}

// ====================================================================
// 5. 常用操作宏
// ====================================================================

// TLB 刷新
static inline void sfence_vma() {
    asm volatile("sfence.vma zero, zero");
}

// 中断控制
static inline void intr_on() {
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void intr_off() {
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 查询中断是否使能
static inline int intr_get() {
    uint64 x = r_sstatus();
    return (x & SSTATUS_SIE) != 0;
}
// Machine-mode Interrupt Enable
#define MIE_STIE (1L << 5)  // supervisor timer
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}
static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

#endif // __ASSEMBLER__

#endif // RISCV_H