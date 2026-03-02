// kernel/ramdisk.c
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

extern uchar fs_img_start[];
extern uchar fs_img_end[];

void
ramdisk_init(void)
{
  uint64 size = fs_img_end - fs_img_start;
  printf("ramdisk: mapped at %p - %p, size %d KB\n", 
         fs_img_start, fs_img_end, size / 1024);
}

// 修改点：增加 int write 参数
void
ramdisk_rw(struct buf *b, int write)
{
  // 1. 防御性检查
  if(!holdingsleep(&b->lock))
    panic("ramdisk_rw: buf not locked");

  uint64 diskaddr = b->blockno * BSIZE;
  uchar *addr = fs_img_start + diskaddr;

  // 2. 越界检查
  if (addr + BSIZE > fs_img_end) {
    printf("ramdisk: panic access blockno %d\n", b->blockno);
    panic("ramdisk: read/write out of bounds");
  }

  // 3. 根据 write 参数决定读写方向
  if(write) {
    // --- 写操作 (Write) ---
    // Buffer -> Ramdisk
    memmove(addr, b->data, BSIZE);
    
    // 注意：xv6 的 buf.h 没有 dirty 字段，
    // bwrite 调用完这个函数后会释放锁，这就足够了。
  } else {
    // --- 读操作 (Read) ---
    // Ramdisk -> Buffer
    memmove(b->data, addr, BSIZE);
    
    // 修改点：直接设置 valid 字段，而不是操作 flags
    b->valid = 1; 
  }
}