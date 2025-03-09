#pragma once
#include "PlaybackSystem.hpp"
#include "CallbackHandler.h"
#include <atomic>
#include <array>
#include <vector>
#include <windows.h>

// Forward declaration for RtMidiIn (assumed to be provided by your RtMidi library)
class RtMidiIn;

class MIDIConnect {
public:
    MIDIConnect();
    ~MIDIConnect();

    void OpenDevice(int deviceIndex);
    void CloseDevice();

    // Inlined trivial getters.
    inline bool IsActive() const { return m_isActive.load(std::memory_order_relaxed); }
    inline int GetSelectedDevice() const { return m_selectedDevice; }

    void SetActive(bool active);
    void ReleaseAllNumpadKeys();

private:
    // The RtMidi callback is marked static so it can be passed as a C-style function pointer.
    static void RtMidiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);

    RtMidiIn* m_rtMidiIn;
    int m_selectedDevice;
    std::atomic<bool> m_isActive;

    // Predefined mapping: for each numpad key the “down” and “up” scan codes.
    // These are used during precomputation.
    static constexpr struct {
        WORD down;
        WORD up;
    } NUMPAD_SCANCODES[12] = {
        {0x52, 0x52}, {0x4F, 0x4F}, {0x50, 0x50}, {0x51, 0x51},
        {0x4B, 0x4B}, {0x4C, 0x4C}, {0x4D, 0x4D}, {0x47, 0x47},
        {0x48, 0x48}, {0x49, 0x49}, {0x4A, 0x4A}, {0x4E, 0x4E}
    };

    // Pre-initialized key buffer to avoid per-event allocations.
    struct KeyBuffer {
        INPUT inputs[10];
        bool initialized;
    } m_keyBuffer;

    // Precomputed mapping for note events: for every possible note (data1) and velocity (data2)
    // mapping[note][velocity] is an array of 10 INPUT events.
    std::array<std::array<std::array<INPUT, 10>, 128>, 128> m_noteMapping;

    // Precomputed mapping for sustain events: for every possible sustain value (data2)
    // mapping[data2] is an array of 10 INPUT events.
    std::array<std::array<INPUT, 10>, 128> m_sustainMapping;
};
