#ifndef PRINT_ONLY
#include "defs.h"
#include "clock.h"
#include "print.h"


void handler_s(uint64_t cause) {
  if (cause >> 63) {
    if (((cause << 1) >> 1) == 5) {
      clock_set_next_event();
      puts("Supervisor Time Interrupt. Cnt=");
      put_num(ticks);
      puts("\n");
    } 
  }
}
#endif