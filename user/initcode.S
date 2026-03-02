# user/initcode.S
# 最终验收: GetPID -> Fork -> Wait -> Child(GetPID+Print) -> Parent Print -> Loop

.globl _start
_start:
    // ----------------------------------------------------
    // 1. [Parent] 测试 SYS_getpid
    // ----------------------------------------------------
    li a7, 11           // SYS_getpid
    ecall
    // 此时 a0 应该等于 1，我们不做处理，主要看内核日志不崩

    // ----------------------------------------------------
    // 2. [Parent] 打印 Start
    // ----------------------------------------------------
    li a0, 1            // fd = 1
    la a1, msg_start
    li a2, 7            // len
    li a7, 16           // SYS_write
    ecall

    // ----------------------------------------------------
    // 3. Fork
    // ----------------------------------------------------
    li a7, 1            // SYS_fork
    ecall
    
    // a0==0 是子进程，跳转到 child 标签
    beqz a0, child

parent:
    // --- 父进程分支 ---
    
    // 4. [Parent] 调用 wait(0)
    // 父进程挂起，等待子进程退出
    li a0, 0
    li a7, 3            // SYS_wait
    ecall

    // 5. [Parent] 醒来后打印 Parent
    li a0, 1
    la a1, msg_parent
    li a2, 7            // len
    li a7, 16           // SYS_write
    ecall

    // 6. [Parent] 死循环 (防止 init 退出导致内核 panic)
    j loop

child:
    // --- 子进程分支 ---

    // 7. [Child] 测试 SYS_getpid
    li a7, 11           // SYS_getpid
    ecall
    // 此时 a0 应该等于 2 (或者其他非0非1值)

    // 8. [Child] 打印 Child
    li a0, 1
    la a1, msg_child
    li a2, 7            // len
    li a7, 16           // SYS_write
    ecall

    // 9. [Child] 退出
    li a0, 0
    li a7, 2            // SYS_exit
    ecall

exit:
    // 理论上不会走到这里，但作为安全出口
    li a0, 0
    li a7, 2            // SYS_exit
    ecall

loop:
    j loop

// ----------------------------------------------------
// 数据段
// ----------------------------------------------------
.data
.align 4
msg_start:
    .string "Start\n\0"
msg_parent:
    .string "Parent\n\0"
msg_child:
    .string "Child\n\0"