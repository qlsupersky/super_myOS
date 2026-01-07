#include "syscall.h"

#include "task_manager.h"
#include "stdio.h"
#include "defs.h"


struct ret_info syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    // TODO: implement syscall function
    struct ret_info ret;
    switch (syscall_num) {
    case SYS_WRITE:
        // SYS_WRITE: 打印字符串到屏幕
        if (arg0 == 1) { // fd == 1 表示标准输出
            const char *buf = (const char *)arg1;
            size_t count = (size_t)arg2;
            for (size_t i = 0; i < count; i++) {
                putchar(buf[i]);
            }
            ret.a0 = count; // 返回打印的字符数
        } else {
            ret.a0 = -1; // 错误的文件描述符
        }
        break;

    case SYS_GETPID:
        // SYS_GETPID: 获取当前进程的 PID
        ret.a0 = getpid();
        break;
    
    default:
        printf("Unknown syscall! syscall_num = %d\n", syscall_num);
        while(1);
        break;
    }
    return ret;
}