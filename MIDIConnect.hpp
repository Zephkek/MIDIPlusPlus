#pragma once
#include "PlaybackSystem.hpp"
#include <atomic>
#include <array>
#include <vector>
#include <windows.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Devices::Midi;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;

class MIDIConnect {
public:
    MIDIConnect();
    ~MIDIConnect();

    void OpenDevice(int deviceIndex);
    void CloseDevice();

    // Inlined trivial getters
    inline bool IsActive() const { return m_isActive.load(std::memory_order_relaxed); }
    inline int GetSelectedDevice() const { return m_selectedDevice; }

    void SetActive(bool active);
    void ReleaseAllNumpadKeys();

private:
    void ProcessMidiMessage(IMidiMessage const& midiMessage);

    MidiInPort m_midiInPort{ nullptr };
    event_token m_messageToken;
    int m_selectedDevice;
    std::atomic<bool> m_isActive;

    // Predefined mapping: for each numpad key the "down" and "up" scan codes
    static constexpr struct {
        WORD down;
        WORD up;
    } NUMPAD_SCANCODES[12] = {
        {0x52, 0x52}, {0x4F, 0x4F}, {0x50, 0x50}, {0x51, 0x51},
        {0x4B, 0x4B}, {0x4C, 0x4C}, {0x4D, 0x4D}, {0x47, 0x47},
        {0x48, 0x48}, {0x49, 0x49}, {0x4A, 0x4A}, {0x4E, 0x4E}
    };

    // Pre-initialized key buffer to avoid per-event allocations
    struct KeyBuffer {
        INPUT inputs[10];
        bool initialized;
    } m_keyBuffer;

    // Precomputed mapping for note events: for every possible note (data1) and velocity (data2)
    std::array<std::array<std::array<INPUT, 10>, 128>, 128> m_noteMapping;

    // Precomputed mapping for sustain events: for every possible sustain value (data2)
    std::array<std::array<INPUT, 10>, 128> m_sustainMapping;
};