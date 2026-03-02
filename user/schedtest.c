#include "kernel/types.h"
#include "user/user.h"

// 防止编译器优化掉空循环
void spin_delay(int count) {
    volatile int i;
    for (i = 0; i < count; i++) {
        // 空操作
    }
}

int main(int argc, char *argv[])
{
    int pid;
    
    printf("Scheduler test starting...\n");
    printf("If scheduler is working, you should see A and B outputs interleaved.\n");
    printf("If hung on AAAAA... or BBBBB..., preemption is NOT working.\n\n");

    pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // --- 子进程 (打印 B) ---
        for (int i = 0; i < 20; i++) {
            // 1. 进行大量计算，霸占 CPU
            // K230 是 1.6GHz 左右，这里需要足够大的数才能让它跑几十毫秒
            // 如果打印太快，说明这个数太小，没能跨越时钟中断周期
            spin_delay(50000000); 
            
            // 2. 输出标识
            printf("B ");
        }
        printf("\nChild finished.\n");
        exit(0);
    } else {
        // --- 父进程 (打印 A) ---
        for (int i = 0; i < 20; i++) {
            // 1. 进行大量计算，霸占 CPU
            spin_delay(50000000);
            
            // 2. 输出标识
            printf("A ");
        }
        printf("\nParent finished.\n");
        
        // 等待子进程结束
        wait(0); 
    }

    exit(0);
}