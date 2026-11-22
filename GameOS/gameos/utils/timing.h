#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

namespace timing {

void sleep(unsigned int nanosec);
void init();
uint64_t gettickcount();
uint64_t ticks2ms(uint64_t ticks);
uint64_t get_wall_time_ms();
// CPU time used by the calling thread so far, in milliseconds
double thread_cpu_ms();

};

#endif // TIMING_H
