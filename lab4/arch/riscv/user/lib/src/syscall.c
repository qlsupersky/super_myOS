#include "syscall.h"


struct ret_info u_syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, \
                uint64_t arg3, uint64_t arg4, uint64_t arg5){
    struct ret_info ret;
    // TODO: 完成系统调用，将syscall_num放在a7中，将参数放在a0-a5中，触发ecall，将返回值放在ret中

    __asm__ volatile(
        "mv a0, %2\n"
        "mv a1, %3\n"
        "mv a2, %4\n"
        "mv a3, %5\n"
        "mv a4, %6\n"
        "mv a5, %7\n"
        "mv a7, %8\n"
        "ecall\n"
        "mv %0, a0\n"
        "mv %1, a1\n"
        : "=r"(ret.a0), "=r"(ret.a1)
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), "r"(arg5), "r"(syscall_num)
        : "a0", "a1", "a2", "a3", "a4", "a5", "a7", "memory");
    
    return ret;
}