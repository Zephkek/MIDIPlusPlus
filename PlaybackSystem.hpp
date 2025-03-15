#ifndef PLAYBACK_SYSTEM_HPP
#define PLAYBACK_SYSTEM_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma once

// Windows headers
#include <windows.h>
#include <windowsx.h>
#include <avrt.h>

// Standard headers
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <condition_variable>

// Project-specific headers
#include "resource.h"
#include "config.hpp"      // Contains midi::Config and configuration definitions
#include "Transpose.h"
#include "json.hpp"
#include "midi_parser.h"
#include "InputHeader.h"   // For NtUserSendInputCall and GetNtUserSendInputSyscallNumber
#include "thread_pool.h"   // dp::thread_pool
#include "timer.h"

class VirtualPianoPlayer;
extern VirtualPianoPlayer* g_player;

// Global variables (definitions provided in CPP)
extern double g_totalSongSeconds;
extern int    g_sustainCutoff;

// =====================================================
// Sustain Mode Enumeration
// =====================================================
enum class SustainMode {
    IG,
    SPACE_DOWN,
    SPACE_UP
};

// =====================================================
// EventType: For note actions (Press/Release)
// =====================================================
enum class EventType : uint8_t {
    Press,
    Release
};

// =====================================================
// NoteEvent: Represents an internal note or control event.
// =====================================================
struct alignas(64) NoteEvent {
    std::chrono::nanoseconds time;
    std::string_view note;    // e.g. "C4" or "sustain"
    EventType action;         // Press or Release
    int velocity;
    bool isSustain;           // true if sustain pedal event
    int sustainValue;
    int trackIndex;

    NoteEvent() noexcept;
    NoteEvent(std::chrono::nanoseconds t, std::string_view n, EventType a, int v, bool s, int sv, int trackIdx) noexcept;
    bool operator>(const NoteEvent& other) const noexcept;
};

// =====================================================
// NoteEventPool: Fast allocator for NoteEvent objects.
// =====================================================
class alignas(64) NoteEventPool {
public:
    NoteEventPool();
    ~NoteEventPool();

    template<typename... Args>
    NoteEvent* allocate(Args&&... args) {
        if (reinterpret_cast<size_t>(current) + sizeof(NoteEvent) > reinterpret_cast<size_t>(end))
            allocateNewBlock();
        NoteEvent* event = new (current) NoteEvent(std::forward<Args>(args)...);
        current += sizeof(NoteEvent);
        allocated_count.fetch_add(1, std::memory_order_relaxed);
        return event;
    }
    void reset();
    size_t getAllocatedCount() const;
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t BLOCK_SIZE = 1024 * 1024; // 1 MB block

    struct alignas(CACHE_LINE_SIZE) Block {
        char data[BLOCK_SIZE];
        Block* next;
    };

    Block* head;
    char* current;
    char* end;
    std::atomic<size_t> allocated_count;

    void allocateNewBlock();
};

// =====================================================
// PlaybackControl: Controls skip/rewind/restart commands.
// =====================================================
class PlaybackControl {
public:
    enum class Command { NONE, SKIP, REWIND, RESTART };
    struct State {
        std::chrono::nanoseconds position{ 0 };
        size_t event_index{ 0 };
        bool needs_reset{ false };
    };

    void requestSkip(std::chrono::seconds amount);
    void requestRewind(std::chrono::seconds amount);
    bool hasCommand() const;
    State processCommand(const State& current_state, double speed, size_t buffer_size);
private:
    mutable std::mutex mutex;
    Command pending_command{ Command::NONE };
    std::chrono::seconds command_amount{ 0 };
    std::atomic<bool> command_processed{ true };
};

// ----------------------------------------------------
// RawNoteEvent: Holds raw MIDI event data.
// ----------------------------------------------------
struct RawNoteEvent {
    std::chrono::nanoseconds time;
    std::string_view note_or_control;
    EventType action;         // Converted to enum
    int velocity;
    int trackIndex;
};

// =====================================================
// VirtualPianoPlayer: Main class for virtual piano playback.
// =====================================================
class VirtualPianoPlayer {
public:
    VirtualPianoPlayer() noexcept(false);
    ~VirtualPianoPlayer();

    // Track controls
    void set_track_mute(size_t trackIndex, bool mute);
    void set_track_solo(size_t trackIndex, bool solo);

    // Playback controls
    void toggle_play_pause();
    void skip(std::chrono::seconds duration);
    void rewind(std::chrono::seconds duration);
    void restart_song();
    void speed_up();
    void slow_down();
    void toggle_out_of_range_transpose();
    void toggle_88_key_mode();
    void toggle_velocity_keypress();
    void toggle_volume_adjustment();
    void toggleSustainMode();
    int  toggle_transpose_adjustment();
    // Other operations
    void release_all_keys();
    void calibrate_volume();
    void process_tracks(const MidiFile& midi_file);

    // Static handle for command event
    static HANDLE command_event;

    // Data members
    std::vector<RawNoteEvent> note_events;
    std::vector<std::string> string_storage;
    std::vector<std::pair<double, double>> tempo_changes;
    std::vector<TimeSignature> timeSignatures;
    std::unique_ptr<std::jthread> playback_thread;
    std::atomic<bool> eightyEightKeyModeActive{ true };

    std::vector<std::shared_ptr<std::atomic<bool>>> trackMuted;
    std::vector<std::shared_ptr<std::atomic<bool>>> trackSoloed;
    std::atomic<bool> midiFileSelected{ false };
    std::atomic<bool> should_stop{ false };
    std::atomic<bool> paused{ true };
    std::atomic<bool> playback_started{ false };
    std::atomic<size_t> buffer_index{ 0 };

    double current_speed{ 1.0 };
    double paused_time = 0.0;
    int currentTransposition = 0;
    std::chrono::nanoseconds total_adjusted_time{ 0 };

    // Returns current adjusted playback time.
    std::chrono::nanoseconds get_adjusted_time() noexcept;

    // Precomputed scan table for key mapping.
    static const std::array<WORD, 256> SCAN_TABLE_AUTO;

    MidiFile midi_file;

    // Velocity functions
    void setVelocityCurveIndex(size_t index);
    std::string getVelocityCurveName(midi::VelocityCurveType curveType);
    std::string getVelocityKey(int targetVelocity);

    // Sustain settings
    SustainMode currentSustainMode{ SustainMode::IG };
    std::atomic<bool> enable_volume_adjustment{ false };
    std::atomic<bool> enable_velocity_keypress{ false };

    // Key mapping
    std::map<std::string, std::string> limited_key_mappings;
    std::map<std::string, std::string> full_key_mappings;
    std::unordered_map<std::string, std::atomic<bool>> pressed_keys;
    std::string lastPressedKey;
    bool isSustainPressed{ false };
    WORD sustain_key_code{ 0 };

    bool ENABLE_OUT_OF_RANGE_TRANSPOSE{ false };

    std::array<int, 128> volume_lookup;
    WORD volume_up_key_code{ 0 };
    WORD volume_down_key_code{ 0 };
    WORD pause_key_code{ 0 };
    WORD rewind_key_code{ 0 };
    WORD skip_key_code{ 0 };
    WORD emergency_exit_key_code{ 0 };
    std::atomic<int> current_volume{ 0 };
    std::atomic<int> max_volume{ 0 };
    std::vector<bool> drum_flags;
    std::unique_ptr<std::jthread> hotkey_thread;
    std::atomic<bool> hotkey_stop{ false }; // new flag for hotkey thread
    void hotkey_listener();
    void emergency_exit();
    bool isTrackEnabled(int trackIndex) const;
    WORD vkToScanCode(int vk);
    std::condition_variable playback_cv;
    std::mutex playback_cv_mutex;
    unsigned long long last_resume_tsc;
    unsigned long long playback_start_time;
private:
    std::mutex buffer_mutex;
    PlaybackControl playback_control;
    UINT m_timerResolutionSet{ 0 };
    double inv_cpu_freq;  // Optional for optimization
    double time_factor;
    // Static waitable timer shared by all instances.
    static HANDLE waitable_timer;

    // Thread pool for processing note events.
    dp::thread_pool<> processing_pool;


    // Helper to signal playback thread (notify condition variable and legacy event)
    inline void signalPlayback() noexcept {
        SetEvent(command_event);
        playback_cv.notify_all();
    }

    // Core playback functions.
    void play_notes();
    void prepare_event_queue();
    void execute_note_event(const NoteEvent& event) noexcept;
    void handle_sustain_event(const NoteEvent& event);
    size_t find_next_event_index(const std::chrono::nanoseconds& target_time);
    void reset_volume();
    void initializeKeyCache();
    void KeyPress(std::string_view key, bool press);
    int stringToVK(std::string_view keyName);
    void sendVirtualKey(WORD vk, bool is_press);
    void pressKey(WORD vk);
    void releaseKey(WORD vk);
    void press_key(std::string_view note) noexcept;
    void release_key(std::string_view note) noexcept;
    std::string transpose_note(std::string_view note);
    int note_name_to_midi(std::string_view note_name);
    std::string get_note_name(int midi_note);
    void handle_note_off(std::chrono::nanoseconds ctime, int ch, int note, int vel, int trackIndex,
        std::unordered_map<int, std::unordered_map<int, std::vector<std::chrono::nanoseconds>>>& active_notes);
    void handle_note_on(std::chrono::nanoseconds ctime, int ch, int note, int vel, int trackIndex,
        std::unordered_map<int, std::unordered_map<int, std::vector<std::chrono::nanoseconds>>>& active_notes);
    void add_sustain_event(std::chrono::nanoseconds time, int channel, int sustainValue, int trackIndex);
    void add_note_event(std::chrono::nanoseconds time, std::string_view note, EventType action, int velocity, int trackIndex);
    void adjust_playback_speed(double factor);
    void arrowsend(WORD scanCode, bool extended);
    void precompute_volume_adjustments();
    void AdjustVolumeBasedOnVelocity(int velocity) noexcept;
    std::pair<std::map<std::string, std::string>, std::map<std::string, std::string>> define_key_mappings();

    // Internal buffers.
    std::vector<NoteEvent*> note_buffer;
    NoteEventPool event_pool;

    // Transpose engine and velocity curve index.
    TransposeEngine transposeEngine;
    size_t currentVelocityCurveIndex = 0;
};

#endif // PLAYBACK_SYSTEM_HPP
