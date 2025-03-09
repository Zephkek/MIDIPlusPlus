#pragma once

#define NOMINMAX
#define __WINDOWS_MM__  // enable Windows MM in RtMidi
#pragma comment(lib, "winmm.lib")

#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <Windows.h>
#include "CallbackHandler.h"
#include "PlaybackSystem.hpp"  

// Forward declarations for RtMidi classes
class RtMidiIn;
class RtMidiError;

class MIDI2Key {
public:
    // Pass a pointer to your VirtualPianoPlayer instance (this entire thing will probably need to be fucking rewritten anyway lmfao)
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
    // INTERNAL CALLBACK from RtMidi
    static void RtMidiCallback(double deltaTime,
        std::vector<unsigned char>* message,
        void* userData);

    RtMidiIn* m_rtMidiIn;
    int m_selectedDevice;
    int m_selectedChannel;
    std::atomic<bool> m_isActive;
    VirtualPianoPlayer* m_player;

    // Pre-allocated buffers (aligned for performance)
    alignas(64) static INPUT m_velocityInputs[4];
    alignas(64) static INPUT m_sustainInput[2];
    alignas(64) static char  m_lastVelocityKey;
    alignas(64) std::array<std::atomic<bool>, 128> pressed{};


};
