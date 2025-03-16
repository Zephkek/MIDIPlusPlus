#pragma once

#define NOMINMAX

#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// WinRT for MIDI
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Midi.h>

// Classic Windows
#include <windows.h>

// Forward declarations
#include "PlaybackSystem.hpp"

struct PrecomputedKeyEvents;

//--------------------------------------------------------------------------------
// MIDI2Key: Pain and Suffering
//--------------------------------------------------------------------------------
class MIDI2Key {
public:
    MIDI2Key(VirtualPianoPlayer* player);
    ~MIDI2Key();

    void OpenDevice(int deviceIndex);
    void CloseDevice();
    void SetMidiChannel(int channel);

    bool IsActive() const;
    void SetActive(bool active);

    int GetSelectedDevice() const;
    int GetSelectedChannel() const;

private:
    void ProcessMidiMessage(winrt::Windows::Devices::Midi::IMidiMessage const& midiMessage);

    // WinRT MIDI port
    winrt::Windows::Devices::Midi::MidiInPort m_midiInPort{ nullptr };
    winrt::event_token m_messageToken;

    int m_selectedDevice;
    int m_selectedChannel;
    std::atomic<bool> m_isActive;
    VirtualPianoPlayer* m_player; // copy 

    // Key injection buffers
    alignas(64) static INPUT m_velocityInputs[4];
    alignas(64) static INPUT m_sustainInput[2];
    alignas(64) static char m_lastVelocityKey;

    // Modifier usage counters (e.g., alt, ctrl, shift)
    alignas(64) static std::atomic<int> modifierCounts[3];

    // For each note [0..127], track if it is pressed
    alignas(64) std::array<std::atomic<bool>, 128> pressed;
};

extern "C" UINT __fastcall NtUserSendInputCall(ULONG cInputs, LPINPUT pInputs, int cbSize);
