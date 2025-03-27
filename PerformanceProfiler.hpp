#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <array>
#include <limits>

/**
 * @brief Lock-free ring buffer for profiling events
 *
 * This class provides a fixed-size, lock-free ring buffer specifically
 * designed for storing profiling events without blocking the producer thread.
 */
class ProfilerRingBuffer {
public:
    // Buffer size must be power of 2 for efficient masking
    static constexpr size_t BUFFER_SIZE = 4096;
    static constexpr size_t BUFFER_MASK = BUFFER_SIZE - 1;

    struct ProfileEvent {
        std::chrono::high_resolution_clock::time_point timestamp;
        const char* section;
        bool isEntry;
        uint64_t threadId;
        uint64_t additionalData;
    };

    ProfilerRingBuffer();

    /**
     * @brief Try to write an event to the ring buffer
     *
     * @param section Name of the code section
     * @param isEntry True if entering section, false if exiting
     * @param additionalData Any additional data to store with event
     * @return true if write was successful, false if buffer was full
     */
    bool tryWrite(const char* section, bool isEntry, uint64_t additionalData = 0);

    /**
     * @brief Try to read an event from the ring buffer
     *
     * @param processor Function to process the event
     * @return true if an event was read and processed, false if buffer was empty
     */
    bool tryRead(std::function<void(const ProfileEvent&)> processor);

private:
    // Cache-aligned buffer to prevent false sharing
    alignas(64) std::array<ProfileEvent, BUFFER_SIZE> m_buffer;

    // Write index (producer)
    alignas(64) std::atomic<size_t> m_writeIndex;

    // Read index (consumer)
    alignas(64) std::atomic<size_t> m_readIndex;
};

/**
 * @brief High-performance, non-blocking profiler for real-time applications
 *
 * This class provides a comprehensive profiling system designed specifically
 * for real-time applications where latency is critical. It uses a background
 * thread for file I/O and a lock-free ring buffer to avoid blocking the
 * instrumented threads.
 */
class PerformanceProfiler {
public:
    /**
     * @brief Construct a new Performance Profiler
     *
     * @param filepath Path for the output CSV file
     */
    explicit PerformanceProfiler(const std::string& filepath);

    /**
     * @brief Destroy the Performance Profiler
     *
     * Stops the profiler if it's still running and finishes writing data
     */
    ~PerformanceProfiler();

    /**
     * @brief Start the profiler
     *
     * @return true if successfully started, false on error
     */
    bool start();

    /**
     * @brief Stop the profiler
     *
     * Waits for the writer thread to finish and writes statistics
     */
    void stop();

    /**
     * @brief Record entering a code section
     *
     * @param section Name of the section being entered
     */
    void enterSection(const char* section);

    /**
     * @brief Record exiting a code section
     *
     * @param section Name of the section being exited
     * @param additionalData Any additional data to record (e.g., batch size)
     */
    void exitSection(const char* section, uint64_t additionalData = 0);

    /**
     * @brief Get the singleton instance
     *
     * @return Reference to the global profiler instance
     */
    static PerformanceProfiler& getInstance();

private:
    // Thread-local storage for section entry times
    struct ThreadStorage {
        std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> entryTimes;
    };
    static thread_local ThreadStorage tls;

    // Statistics for a single code section
    struct SectionStat {
        std::atomic<uint64_t> totalNs{ 0 };
        std::atomic<uint64_t> callCount{ 0 };
        std::atomic<uint64_t> maxNs{ 0 };
        std::atomic<uint64_t> minNs{ std::numeric_limits<uint64_t>::max() };
    };

    std::string m_filepath;
    std::ofstream m_file;
    ProfilerRingBuffer m_ringBuffer;
    std::atomic<bool> m_isRunning;
    std::thread m_writerThread;
    std::chrono::high_resolution_clock::time_point m_startTime;

    // Thread-safe map for section statistics
    std::unordered_map<std::string, SectionStat> m_stats;
    std::shared_mutex m_statsMutex;

    // Writer thread function
    void writerLoop();
};

// Convenience macros for profiling
#define PROFILER_START() \
    PerformanceProfiler::getInstance().start()

#define PROFILER_STOP() \
    PerformanceProfiler::getInstance().stop()

#define PROFILE_SCOPE_START(name) \
    PerformanceProfiler::getInstance().enterSection(name)

#define PROFILE_SCOPE_END(name) \
    PerformanceProfiler::getInstance().exitSection(name)

/**
 * @brief RAII-style automatic section profiler
 *
 * Automatically profiles a scope from construction to destruction
 */
class ScopedProfiler {
public:
    /**
     * @brief Construct a new Scoped Profiler
     *
     * @param section Section name to profile
     * @param additionalData Optional additional data to record on exit
     */
    explicit ScopedProfiler(const char* section, uint64_t additionalData = 0);

    /**
     * @brief Destroy the Scoped Profiler
     *
     * Records the section exit time
     */
    ~ScopedProfiler();

private:
    const char* m_section;
    uint64_t m_additionalData;
};

// Convenient macros for RAII profiling
#define PROFILE_FUNCTION() \
    ScopedProfiler _profiler_##__LINE__(__FUNCTION__)

#define PROFILE_SCOPE(name) \
    ScopedProfiler _profiler_##__LINE__(name)

#define PROFILE_SCOPE_WITH_DATA(name, data) \
    ScopedProfiler _profiler_##__LINE__(name, data)

// For release builds, you can define this to disable profiling:
// #ifdef NDEBUG
//     #define PROFILE_FUNCTION()
//     #define PROFILE_SCOPE(name)
//     #define PROFILE_SCOPE_WITH_DATA(name, data)
//     #define PROFILER_START()
//     #define PROFILER_STOP()
// #endif