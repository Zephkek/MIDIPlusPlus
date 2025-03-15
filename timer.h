#ifndef RDTSC_TIMER_H
#define RDTSC_TIMER_H

#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

enum {
    RDTSC_TIMER_READY = 0,
    RDTSC_TIMER_ERR_CPU_FREQ,
    RDTSC_TIMER_ERR_RDTSCP_SUPPORT,// old
    RDTSC_TIMER_ERR_MEASUREMENT,
};

static double __cpu_freq;               // CPU frequency in Hz
static unsigned long __instruction_overhead; // Estimated overhead of timer instructions
static unsigned int __timer_status;     // Timer status
static unsigned int __timer_error;      // Error margin indicator
static int __rdtscp_supported = 0;      // Flag: 1 if RDTSCP is supported, 0 otherwise

static inline void __cpuid(int level, int* a, int* b, int* c, int* d) {
    int info[4];
    __cpuid(info, level);
    *a = info[0];
    *b = info[1];
    *c = info[2];
    *d = info[3];
}

// Calculate mean of an array
static double __timer_calculate_mean(unsigned long* set, unsigned long set_length) {
    unsigned long i;
    double average = 0.0;
    for (i = 0; i < set_length; i++) {
        average += set[i];
    }
    return average / set_length;
}

// Calculate standard deviation
static double __timer_calculate_dev(unsigned long* set, unsigned long set_length, double mean) {
    unsigned long i;
    double variance = 0.0;
    for (i = 0; i < set_length; i++) {
        double diff = set[i] - mean;
        variance += diff * diff;
    }
    return sqrt(variance / set_length);
}

// Calculate margin of error (95% confidence)
static double __timer_calculate_error(unsigned long* set, unsigned long set_length, double mean) {
    const double z_coefficient = 1.96;
    double dev = __timer_calculate_dev(set, set_length, mean);
    return z_coefficient * (dev / sqrt((double)set_length));
}

// Estimate CPU frequency using RDTSC and Sleep
static double __timer_calculate_cpu_freq() {
    const int sleep_ms = 100;
    unsigned long long start = __rdtsc();
    Sleep(sleep_ms);
    unsigned long long end = __rdtsc();
    double elapsed_seconds = (double)sleep_ms / 1000.0;
    return (double)(end - start) / elapsed_seconds;
}

// Calibrate timer overhead
static void __timer_calibrate() {
    const unsigned long repeat_factor = 1000000;
    unsigned long i, start, end;
    unsigned long* timing = (unsigned long*)malloc(repeat_factor * sizeof(unsigned long));
    if (timing == NULL) {
        __timer_status = RDTSC_TIMER_ERR_MEASUREMENT;
        return;
    }
    unsigned int temp;
    int ci_temp[4];
    double mean, error;

    for (i = 0; i < repeat_factor; i++) {
        __cpuid(0, &ci_temp[0], &ci_temp[1], &ci_temp[2], &ci_temp[3]);
        start = __rdtsc();
        if (__rdtscp_supported) {
            end = __rdtscp(&temp); // Use RDTSCP if supported
        } else {
            __cpuid(0, &ci_temp[0], &ci_temp[1], &ci_temp[2], &ci_temp[3]);
            end = __rdtsc();      // Use CPUID then RDTSC if RDTSCP not supported
        }
        __cpuid(0, &ci_temp[0], &ci_temp[1], &ci_temp[2], &ci_temp[3]);
        timing[i] = end - start;
    }

    mean = __timer_calculate_mean(timing, repeat_factor);
    error = __timer_calculate_error(timing, repeat_factor, mean);

    __instruction_overhead = (unsigned long)mean;
    if (error <= (mean / 100.0)) {
        __timer_error = 1; // Below 1%
    } else if (error <= (mean / 50.0)) {
        __timer_error = 2; // Below 2%
    } else if (error <= (mean / 33.34)) {
        __timer_error = 3; // Below 3%
    } else {
        __timer_status = RDTSC_TIMER_ERR_MEASUREMENT;
    }

    free(timing);
}

static void __timer_init() {
    int cpu_info[4];
    __cpu_freq = __timer_calculate_cpu_freq();
    if (__cpu_freq == 0.0) {
        __timer_status = RDTSC_TIMER_ERR_CPU_FREQ;
        return;
    }

    // Check for RDTSCP support
    __cpuid(0x80000001, &cpu_info[0], &cpu_info[1], &cpu_info[2], &cpu_info[3]);
    if (cpu_info[3] & (1 << 27)) {
        __rdtscp_supported = 1; // RDTSCP is supported
    } else {
        __rdtscp_supported = 0; // RDTSCP not supported, will use RDTSC
    }

    __timer_calibrate();
    if (__timer_status != RDTSC_TIMER_ERR_MEASUREMENT) {
        __timer_status = RDTSC_TIMER_READY; 
    }
}
static inline unsigned long rdtsc_timer_start_func() {
    int ci_temp[4];
    __cpuid(0, ci_temp, ci_temp + 1, ci_temp + 2, ci_temp + 3);
    return __rdtsc();
}

static inline unsigned long rdtsc_timer_end_func() {
    unsigned int temp;
    int ci_temp[4];
    if (__rdtscp_supported) {
        unsigned long ret = __rdtscp(&temp); // Use RDTSCP if supported
        __cpuid(0, ci_temp, ci_temp + 1, ci_temp + 2, ci_temp + 3);
        return ret;
    } else {
        __cpuid(0, ci_temp, ci_temp + 1, ci_temp + 2, ci_temp + 3);
        return __rdtsc(); // Use CPUID then RDTSC if RDTSCP not supported
    }
}

static inline unsigned int rdtsc_timer_status() {
    return __timer_status;
}

static inline double rdtsc_timer_precision() {
    return 1.0 / (__cpu_freq / 1000000000.0);
}

#endif /* RDTSC_TIMER_H */