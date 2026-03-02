#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
void  main()
{
    if(cpuid() == 0){
    consoleinit();
    cpuinit();
    kinit();
    kvminit();
    kvminithart();
     procinit();
    trapinit();
    trapinithart();
    plicinit();
    plicinithart();
    // timerinit();
    // intr_on();
    
   
   
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    ramdisk_init();
    // test_proc_init(); 
    userinit();
    // userinit();
    
    
    // intr_off(); 
    printf("hart %d starting\n", cpuid());
   }
   else {
		printf("hart %d starting\n", cpuid());
	}
    scheduler();

}