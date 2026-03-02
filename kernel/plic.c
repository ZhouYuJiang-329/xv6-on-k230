// kernel/plic.c - 简化版本用于调试
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void plicinit(void)
{
    // 设置所有中断源的优先级为 1
    *(uint32*)(PLIC + UART0_IRQ*4) = 1;
}

void plicinithart(void)
{
    int hart = cpuid();

    // set enable bits for this hart's S-mode
    // for the uart and virtio disk.
    *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ);

    // set this hart's S-mode priority threshold to 0.
    *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

int plic_claim(void)
{
    int hart = cpuid();
    int irq = *(uint32*)PLIC_SCLAIM(hart);
    return irq;
}

void plic_complete(int irq)
{
    int hart = cpuid();
    *(uint32*)PLIC_SCLAIM(hart) = irq;
}