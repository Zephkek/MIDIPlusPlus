#define NOMINMAX
#ifndef MIDI2KEY_HPP
#define MIDI2KEY_HPP
#include <array>
#include <atomic>
#include <string>
#include <vector>
#include <windows.h>
#include "RtMidi.h"
#include "PlaybackSystem.hpp" // Assumes this includes VirtualPianoPlayer
#define CACHE_LINE_SIZE 64
#define MAX_BATCH_INPUTSS 32
// MIDI command constants
constexpr unsigned char MIDI_NOTE_ON = 0x90;
constexpr unsigned char MIDI_NOTE_OFF = 0x80;
constexpr unsigned char MIDI_CONTROL_CHANGE = 0xB0;


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
    static bool OptimizeSystem();
    static void RestoreSystemDefaults();
private:
    RtMidiIn* m_rtMidiIn;
    int m_selectedDevice;
    int m_selectedChannel;
    std::atomic<bool> m_isActive;
    VirtualPianoPlayer* m_player;
    std::atomic<bool> m_inCallback;
    std::array<INPUT, MAX_BATCH_INPUTSS> m_batchedInputs;
    static char s_lastVelocityKey;
    static HANDLE s_mmcssHandle;
    static DWORD s_mmcssTaskIndex;
    static DWORD_PTR s_originalAffinity;
    static ULONG s_timerResolution;
    static void SetCallbackThreadPriority();
    static void __stdcall RtMidiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);
};

namespace MIDITables {
    constexpr WORD ALT_SCAN = 0x38;
    constexpr WORD CTRL_SCAN = 0x1D;
    constexpr WORD SHIFT_SCAN = 0x2A;
    constexpr WORD SPACE_SCAN = 0x39;
    constexpr DWORD SC_FLAG = KEYEVENTF_SCANCODE;
    constexpr DWORD KU_FLAG = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

    // Structure definitions
    struct CharInfo {
        char canonical;
        bool shifted;
    };

    struct ModSequence {
        size_t downCount;
        std::array<std::pair<WORD, DWORD>, 3> down;
        size_t upCount;
        std::array<std::pair<WORD, DWORD>, 3> up;
    };

    struct MIDIKeyEvent {
        INPUT press[8];
        size_t pressCount;
        INPUT release[8];
        size_t releaseCount;
        WORD mainScan;
        char keySource;
        bool hasModifiers;
    };

    struct NoteData {
        std::atomic<bool> isPressed;
        MIDIKeyEvent* keyEvent;
        WORD scanCode;
    };

    struct VelocityKeyData {
        char keyChar;
        bool hasValidKey;
        INPUT inputs[4];
        size_t inputCount;
    };

    struct SustainPedalData {
        INPUT pressInput;
        INPUT releaseInput;
    };

    __forceinline INPUT MakeKeyboardInput(WORD scanCode, DWORD flags);
    void Initialize(VirtualPianoPlayer& player);
    void Cleanup();

    // External arrays
    extern std::array<NoteData, 128> g_fullKeyNotes;
    extern std::array<NoteData, 128> g_limitedKeyNotes;
    extern std::array<MIDIKeyEvent*, 256> g_scancodeOwner;
    extern std::array<std::atomic<short>, 256> g_scancodeCount;
    extern std::array<VelocityKeyData, 128> g_velocityData;
    extern SustainPedalData g_sustainData;
    extern std::array<const char*, 128> g_noteNameCache;
    extern std::array<int, 128> g_adjustedNoteMapping;
    extern std::array<WORD, 256> g_scanCodeTable;
    extern std::array<CharInfo, 256> g_charInfoTable;
    extern std::array<ModSequence, 8> g_modSequences;
}

#endif