#include "types.h"
#include "riscv.h"
#include "defs.h"

void set_timer(uint64 stime_value) {
    w_stimecmp(stime_value);
}

void timerinit(void) {
    // 开启 S-mode Timer Interrupt Enable
    w_sie(r_sie() | SIE_STIE);
    
    // 设置第一次闹钟
    set_timer(r_time() + CLOCK_INTERVAL);
    

}