#pragma once
#ifndef TIMER_H
#define TIMER_H

#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <vector>
#include "config.hpp"
enum {
    RDTSC_TIMER_READY = 0,
    RDTSC_TIMER_ERR_CPU_FREQ,
};

// We store the measured frequency here:
static double    __cpu_freq = 0.0;  // Estimated CPU frequency in Hz
static unsigned  __timer_status = RDTSC_TIMER_ERR_CPU_FREQ;

static int    MAX_PASSES = midi::Config::getInstance().autoplayer_timing.MAX_PASSES;    
static double MEASURE_SEC = midi::Config::getInstance().autoplayer_timing.MEASURE_SEC;  
static constexpr double MIN_ACCEPTABLE_FREQ_HZ = 1e5; // Must be above 100 kHz
static DWORD_PTR __pin_thread_to_core(int coreIndex = 0)
{
    DWORD_PTR affinityMask = (static_cast<DWORD_PTR>(1) << coreIndex);
    HANDLE    thread = GetCurrentThread();
    DWORD_PTR oldMask = SetThreadAffinityMask(thread, affinityMask);
    return oldMask;
}
static void __unpin_thread_from_core(DWORD_PTR oldMask)
{
    if (oldMask != 0) {
        SetThreadAffinityMask(GetCurrentThread(), oldMask);
    }
}

/**
 * Perform one pass of TSC frequency measurement using CPUID fences
 * to ensure precise timing. Returns frequency in Hz, or 0 on failure.
 */
static double __measure_tsc_freq_once(double measureTimeSec, int coreIndex)
{
    LARGE_INTEGER qpcFreq;
    if (!QueryPerformanceFrequency(&qpcFreq)) {
        // QPC not supported
        return 0.0;
    }

    // Pin thread to avoid TSC domain changes on older hardware
    DWORD_PTR oldAffinity = __pin_thread_to_core(coreIndex);

    // Warm up QPC
    LARGE_INTEGER startCount;
    QueryPerformanceCounter(&startCount);

    // Serialize before reading initial TSC
    int dummy[4];
    __cpuid(dummy, 0);                // fence/serialize
    LARGE_INTEGER tscStartQpc;
    QueryPerformanceCounter(&tscStartQpc);
    uint64_t startTSC = __rdtsc();

    // Busy-wait until measureTimeSec
    LARGE_INTEGER nowCount;
    double elapsedSec = 0.0;
    do {
        QueryPerformanceCounter(&nowCount);
        elapsedSec = double(nowCount.QuadPart - tscStartQpc.QuadPart) / double(qpcFreq.QuadPart);
    } while (elapsedSec < measureTimeSec);

    // Serialize again before reading final TSC
    __cpuid(dummy, 0);                // fence/serialize
    uint64_t endTSC = __rdtsc();

    // Unpin
    __unpin_thread_from_core(oldAffinity);

    double duration = elapsedSec; // measured by QPC, more accurate than measureTimeSec
    if (duration <= 0.0) {
        return 0.0;
    }

    // Convert TSC counts over 'duration' seconds to frequency in Hz
    double measuredFreq = double(endTSC - startTSC) / duration;
    return measuredFreq;
}

/**
 * Calibrate TSC frequency by running multiple passes and picking the median.
 */
static double __timer_calculate_cpu_freq(int passes = MAX_PASSES,
    double measureTimeSec = MEASURE_SEC)
{
    if (passes <= 0) passes = 1;

    std::vector<double> freqs;
    freqs.reserve(passes);

    for (int i = 0; i < passes; ++i) {
        double f = __measure_tsc_freq_once(measureTimeSec, 0 /* pin to core 0 */);
        if (f > MIN_ACCEPTABLE_FREQ_HZ) {
            freqs.push_back(f);
        }
    }
    if (freqs.empty()) {
        return 0.0;
    }

    // Sort and pick median
    std::sort(freqs.begin(), freqs.end());
    size_t mid = freqs.size() / 2;
    double median = (freqs.size() % 2 == 1)
        ? freqs[mid]
        : 0.5 * (freqs[mid - 1] + freqs[mid]);

    return median;
}

/**
 * Initialize the TSC-based timer.
 */
static void rdtsc_timer_init()
{
    double freq = __timer_calculate_cpu_freq(MAX_PASSES, MEASURE_SEC);
    if (freq >= MIN_ACCEPTABLE_FREQ_HZ) {
        __cpu_freq = freq;
        __timer_status = RDTSC_TIMER_READY;
    }
    else {
        __cpu_freq = 0.0;
        __timer_status = RDTSC_TIMER_ERR_CPU_FREQ;
    }
}

// Return the status
static inline unsigned rdtsc_timer_status()
{
    return __timer_status;
}

// Return the CPU frequency in Hz (0.0 if not ready)
static inline double rdtsc_timer_get_frequency()
{
    return (__timer_status == RDTSC_TIMER_READY) ? __cpu_freq : 0.0;
}


#endif // TIMER_H
