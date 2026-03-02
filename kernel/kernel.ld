OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /* 【修改点 1】K230 的加载地址是 2MB 处 */
  . = 0x00200000;

 /*//这里是内核代码段，紧接着有一个蹦床页，etext内核代码段结束标志*/
  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

 /*//存放只读数据*/

  /* 【关键点】必须保留 .rodata，否则 printf 的字符串会丢失 */
  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) 
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

 /*//已经初始化的全局变量，edata数据段结束标志*/
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) 
    . = ALIGN(16);
    *(.data .data.*)
  }

  /* 必须定义 edata，否则 entry.S 汇编无法找到符号 */
  . = ALIGN(16);
  PROVIDE(edata = .);

 /* //未初始化的全局变量，end为内核镜像结束的位置*/
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) 
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
}