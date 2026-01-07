#include "defs.h"
#include "sched.h"
#include "test.h"
#include "syscall.h"
#include "stdio.h"

void handler_s(uint64_t cause, uint64_t epc, uint64_t sp) {
  // interrupt
  if (cause >> 63 == 1) {
    // supervisor timer interrupt
    if (cause == 0x8000000000000005) {
      asm volatile("ecall");
      ticks++;
      if (ticks % 10 == 0) {
        do_timer();
      }
    }
  }
  // exception
  else if (cause >> 63 == 0) {
    // instruction page fault
    if (cause == 0xc) {
      printf("Instruction page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // load page fault
    else if (cause == 0xd) {
      printf("Load page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // Store/AMO page fault
    else if (cause == 0xf) {
      printf("Store/AMO page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // TODO: syscall from user mode
    else if (cause == 8) {
      // TODO: 根据我们规定的接口规范，从a7中读取系统调用号，然后从a0~a5读取参数，调用对应的系统调用处理函数，最后把返回值保存在a0~a1中。
      //       注意读取和修改的应该是保存在栈上的值，而不是寄存器中的值，因为寄存器上的值可能被更改。

      // 1. 从 a7 中读取系统调用号
      // 2. 从 a0 ~ a5 中读取系统调用参数
      // 2. 调用syscall()，并把返回值保存到 a0,a1 中
      // 3. sepc += 4，注意应该修改栈上的sepc，而不是sepc寄存器
      
      // 提示，可以用(uint64_t*)(sp)得到一个数组

      // 从栈中读取寄存器值
      uint64_t *regs = (uint64_t *)sp;
      uint64_t syscall_num = regs[11]; // a7
      uint64_t arg0 = regs[4]; // a0
      uint64_t arg1 = regs[5]; // a1
      uint64_t arg2 = regs[6]; // a2
      uint64_t arg3 = regs[7]; // a3
      uint64_t arg4 = regs[8]; // a4
      uint64_t arg5 = regs[9]; // a5

      // 调用系统调用处理函数
      struct ret_info ret = syscall(syscall_num, arg0, arg1, arg2, arg3, arg4, arg5);

      // 将返回值写回栈中的 a0 和 a1
      regs[4] = ret.a0;
      regs[5] = ret.a1;

      // sepc += 4，跳过 ecall 指令
      regs[16] += 4;
      
    }
    else {
      printf("Unknown exception! epc = 0x%016lx\n", epc);
      while (1);
    }
  }
  return;
}
