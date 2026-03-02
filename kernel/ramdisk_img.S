# kernel/ramdisk_img.S

# 将文件系统镜像放置在数据段(.data)
# 使用 .data 而不是 .rodata，是为了确保这块内存具有“写”权限
# 否则如果页表将其映射为只读，xv6 在尝试写文件时会触发 Page Fault
.section .data

# 4KB 页对齐，这对性能友好，也方便后续可能的内存映射操作
.balign 4096

# 导出全局符号，供 C 语言代码引用
.globl fs_img_start
.globl fs_img_end

fs_img_start:
    # 核心指令：直接包含二进制文件
    # 注意：fs.img 必须存在于工程根目录（即 Makefile 所在的目录）
    # 或者你需要调整 Makefile 确保它能被找到
    .incbin "fs.img"

fs_img_end:
    # 这里的地址就是 fs.img 结束后的下一个字节地址
    .byte 0