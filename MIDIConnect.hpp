#pragma once
#include "PlaybackSystem.hpp"
#include <atomic>
#include <array>
#include <vector>
#include "RtMidi.h"
#include "InputHeader.h"
#include <windows.h>
#include <timeapi.h>

#define CACHE_LINE_SIZE 64

class MIDIConnect {
public:
    MIDIConnect();
    ~MIDIConnect();

    void OpenDevice(int deviceIndex);
    void CloseDevice();
    inline bool IsActive() const { return m_isActive.load(std::memory_order_relaxed); }
    inline int GetSelectedDevice() const { return m_selectedDevice; }
    void SetActive(bool active);
    void ReleaseAllNumpadKeys();

private:
    static void __stdcall RtMidiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);

    static constexpr struct {
        WORD down;
        WORD up;
    } NUMPAD_SCANCODES[12] = {
        {0x52, 0x52}, {0x4F, 0x4F}, {0x50, 0x50}, {0x51, 0x51},
        {0x4B, 0x4B}, {0x4C, 0x4C}, {0x4D, 0x4D}, {0x47, 0x47},
        {0x48, 0x48}, {0x49, 0x49}, {0x4A, 0x4A}, {0x4E, 0x4E}
    };

    static constexpr size_t MAX_BATCH_INPUTS = 32;
    alignas(CACHE_LINE_SIZE) std::array<INPUT, MAX_BATCH_INPUTS> m_batchedInputs;
    alignas(CACHE_LINE_SIZE) std::array<std::array<std::array<INPUT, 10>, 128>, 128> m_noteMapping;
    alignas(CACHE_LINE_SIZE) std::array<std::array<INPUT, 10>, 128> m_sustainMapping;

    RtMidiIn* m_rtMidiIn;
    int m_selectedDevice;
    std::atomic<bool> m_isActive;
    std::atomic<bool> m_inCallback;

    static HANDLE s_mmcssHandle;
    static DWORD s_mmcssTaskIndex;
    static DWORD_PTR s_originalAffinity;
    static ULONG s_timerResolution;

    static bool OptimizeSystem();
    static void RestoreSystemDefaults();
    static void SetCallbackThreadPriority();
};