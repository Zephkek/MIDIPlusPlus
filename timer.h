#ifndef RDTSC_TIMER_H
#define RDTSC_TIMER_H

#include <windows.h>
#include <intrin.h>
#include <stdint.h>

enum {
    RDTSC_TIMER_READY = 0,
    RDTSC_TIMER_ERR_CPU_FREQ,
};

static double __cpu_freq;           // CPU frequency in Hz
static unsigned int __timer_status; // Timer status

// Estimate CPU frequency using RDTSC and Sleep
static double __timer_calculate_cpu_freq() {
    const int sleep_ms = 100;
    unsigned long long start = __rdtsc();
    Sleep(sleep_ms);
    unsigned long long end = __rdtsc();
    double elapsed_seconds = (double)sleep_ms / 1000.0;
    return (double)(end - start) / elapsed_seconds;
}

static void __timer_init() {
    __cpu_freq = __timer_calculate_cpu_freq();
    if (__cpu_freq == 0.0) {
        __timer_status = RDTSC_TIMER_ERR_CPU_FREQ;
    }
    else {
        __timer_status = RDTSC_TIMER_READY;
    }
}

static inline unsigned int rdtsc_timer_status() {
    return __timer_status;
}

static inline double rdtsc_timer_precision() {
    return 1.0 / (__cpu_freq / 1000000000.0);
}

#endif /* RDTSC_TIMER_H */