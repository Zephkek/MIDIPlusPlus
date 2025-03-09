// MIDI2Key.cpp: 
// - Iteration 1: First attempt at this MIDI bullshit, latency was worse than playing piano with mittens on
// - Iteration 2: Still garbage but at least now it's FAST garbage and fixed junglemaster minor second reported issue
// - Iteration 3: Improvements in latency compared to midi web api, still some issues with note handling that need to be fixed.
// THE GREAT WAR ON LATENCY CONTINUES


#include "MIDI2Key.hpp"
#include "CallBackHandler.h"
#include "InputHeader.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <string_view>
#include <cmath>
#include <windows.h>
#include <stdlib.h>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <stdexcept>
#include <atomic>

// globals are bad but so is your mom, and we still use her
extern int g_sustainCutoff;

static __forceinline INPUT makeKeybdInput(WORD wScan, DWORD dwFlags) {
    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = 0;
    inp.ki.wScan = wScan;
    inp.ki.dwFlags = dwFlags;
    return inp;
}
// First cache: Note name lookup because I'm too lazy to compute this shit every event
static std::array<const char*, 128> NOTE_NAME_CACHE = []() {
    std::array<const char*, 128> cache{};
    constexpr std::array<const char[3], 12> names = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    static char bufs[128][8] = {}; // Static buffer because I'm a caveman and don't care about your fancy heap allocations
    for (int n = 0; n < 128; ++n) {
        int pitch = n % 12;
        int octave = (n / 12) - 1;
        int len = (names[pitch][1] == '#') ? 2 : 1;
        std::memcpy(bufs[n], names[pitch], len);
        sprintf_s(bufs[n] + len, sizeof(bufs[n]) - len, "%d", octave); // sprintf because it's 1998 in my heart
        cache[n] = bufs[n];
    }
    return cache;
    }();

// Second cache: precomputed qwerty mapping table
alignas(64) static const std::array<WORD, 256> SCAN_TABLE = []() {
    std::array<WORD, 256> table{};
    table.fill(0);
    // Digits
    table[(unsigned char)'1'] = 0x02;
    table[(unsigned char)'2'] = 0x03;
    table[(unsigned char)'3'] = 0x04;
    table[(unsigned char)'4'] = 0x05;
    table[(unsigned char)'5'] = 0x06;
    table[(unsigned char)'6'] = 0x07;
    table[(unsigned char)'7'] = 0x08;
    table[(unsigned char)'8'] = 0x09;
    table[(unsigned char)'9'] = 0x0A;
    table[(unsigned char)'0'] = 0x0B;
    // Letters
    table[(unsigned char)'q'] = 0x10;
    table[(unsigned char)'w'] = 0x11;
    table[(unsigned char)'e'] = 0x12;
    table[(unsigned char)'r'] = 0x13;
    table[(unsigned char)'t'] = 0x14;
    table[(unsigned char)'y'] = 0x15;
    table[(unsigned char)'u'] = 0x16;
    table[(unsigned char)'i'] = 0x17;
    table[(unsigned char)'o'] = 0x18;
    table[(unsigned char)'p'] = 0x19;
    table[(unsigned char)'a'] = 0x1E;
    table[(unsigned char)'s'] = 0x1F;
    table[(unsigned char)'d'] = 0x20;
    table[(unsigned char)'f'] = 0x21;
    table[(unsigned char)'g'] = 0x22;
    table[(unsigned char)'h'] = 0x23;
    table[(unsigned char)'j'] = 0x24;
    table[(unsigned char)'k'] = 0x25;
    table[(unsigned char)'l'] = 0x26;
    table[(unsigned char)'z'] = 0x2C;
    table[(unsigned char)'x'] = 0x2D;
    table[(unsigned char)'c'] = 0x2E;
    table[(unsigned char)'v'] = 0x2F;
    table[(unsigned char)'b'] = 0x30;
    table[(unsigned char)'n'] = 0x31;
    table[(unsigned char)'m'] = 0x32;
    // Shifted punctuation
    table[(unsigned char)'!'] = 0x02;
    table[(unsigned char)'@'] = 0x03;
    table[(unsigned char)'#'] = 0x04;
    table[(unsigned char)'$'] = 0x05;
    table[(unsigned char)'%'] = 0x06;
    table[(unsigned char)'^'] = 0x07;
    table[(unsigned char)'&'] = 0x08;
    table[(unsigned char)'*'] = 0x09;
    table[(unsigned char)'('] = 0x0A;
    table[(unsigned char)')'] = 0x0B;
    return table;
    }();

// struct for keeping track of shifted keys and numbers
struct CharInfoRec {
    char canonical;
    bool shifted;
};
// Cache 3: precomputed chars and their shifted form, and numbers and their canonical representation
inline std::array<CharInfoRec, 256> initCharInfoTable() {
    std::array<CharInfoRec, 256> table{};
    for (int i = 0; i < 256; ++i) {
        char c = static_cast<char>(i);
        table[i] = { c, false };
        if (c >= 'A' && c <= 'Z')
            table[i] = { static_cast<char>(c + 32), true };
    }
    table[(unsigned char)'!'] = { '1', true };
    table[(unsigned char)'@'] = { '2', true };
    table[(unsigned char)'#'] = { '3', true };
    table[(unsigned char)'$'] = { '4', true };
    table[(unsigned char)'%'] = { '5', true };
    table[(unsigned char)'^'] = { '6', true };
    table[(unsigned char)'&'] = { '7', true };
    table[(unsigned char)'*'] = { '8', true };
    table[(unsigned char)'('] = { '9', true };
    return table;
}
inline const auto CHAR_INFO_TABLE = initCharInfoTable();

// structs for keeping track of mod sequences during main precomputation of key combos
struct FixedKeyEvent {
    WORD scan;
    DWORD flags;
};
struct ModSequence {
    size_t downCount;
    std::array<FixedKeyEvent, 3> down;
    size_t upCount;
    std::array<FixedKeyEvent, 3> up;
};
// Constants 
constexpr WORD ALT_SCAN = 0x38;
constexpr WORD CTRL_SCAN = 0x1D;
constexpr WORD SHIFT_SCAN = 0x2A;
constexpr DWORD SC_FLAG = KEYEVENTF_SCANCODE;
constexpr DWORD KU_FLAG = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

// Cache 4: precomputed mod sequences
inline std::array<ModSequence, 8> initModSequences() {
    std::array<ModSequence, 8> seq{};
    // No mod
    seq[0] = { 0, {{{0, 0}, {0, 0}, {0, 0}}}, 0, {{{0, 0}, {0, 0}, {0, 0}}} };
    // Alt 
    seq[1] = { 1, {{{ALT_SCAN, SC_FLAG}, {0, 0}, {0, 0}}}, 1, {{{ALT_SCAN, KU_FLAG}, {0, 0}, {0, 0}}} };
    // Ctrl
    seq[2] = { 1, {{{CTRL_SCAN, SC_FLAG}, {0, 0}, {0, 0}}}, 1, {{{CTRL_SCAN, KU_FLAG}, {0, 0}, {0, 0}}} };
    // completely unecessary but at this point im dealing with a bunch of kids who complain a lot
    seq[3] = { 2, {{{ALT_SCAN, SC_FLAG}, {CTRL_SCAN, SC_FLAG}, {0, 0}}}, 2, {{{CTRL_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}, {0, 0}}} };
    // Shift
    seq[4] = { 1, {{{SHIFT_SCAN, SC_FLAG}, {0, 0}, {0, 0}}}, 1, {{{SHIFT_SCAN, KU_FLAG}, {0, 0}, {0, 0}}} };
    // same shit
    seq[5] = { 2, {{{ALT_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}, {0, 0}}}, 2, {{{SHIFT_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}, {0, 0}}} };
    // Ctrl+Shift
    seq[6] = { 2, {{{CTRL_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}, {0, 0}}}, 2, {{{SHIFT_SCAN, KU_FLAG}, {CTRL_SCAN, KU_FLAG}, {0, 0}}} };
    // might as well lol
    seq[7] = { 3, {{{ALT_SCAN, SC_FLAG}, {CTRL_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}}}, 3, {{{SHIFT_SCAN, KU_FLAG}, {CTRL_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}}} };
    return seq;
}
inline const auto modSequences = initModSequences();


// Final boss of precomputed structures this holds all the key events we need
struct alignas(64) PrecomputedKeyEvents {
    size_t pressCount; 
    std::array<INPUT, 16> press; 
    size_t releaseCount;
    std::array<INPUT, 16> release;
    // TODO -- Verify this is even aligned like it should, well don't care... to bad!
    uint16_t mainScan; // The scan code of the main key (not the modifiers)
    char keySource; // The character that created this event
    uint8_t padding[45]; // Padding to make the struct 64-byte aligned, because OCD
};
// I'm an absolute retard 
static PrecomputedKeyEvents computePrecomputedKeyEvents(std::string_view key_str) {
    PrecomputedKeyEvents events;
    bool hasAlt = (key_str.find("alt+") != std::string_view::npos);
    bool hasCtrl = (key_str.find("ctrl+") != std::string_view::npos);

    char lastChar = key_str.empty() ? '\0' : key_str.back();
    const CharInfoRec& info = CHAR_INFO_TABLE[(unsigned char)lastChar];
    WORD main_scan = SCAN_TABLE[(unsigned char)info.canonical];
    bool isShifted = info.shifted;

    // bits: alt=1, ctrl=2, shift=4
    int mods = (int)hasAlt | ((int)hasCtrl << 1) | ((int)isShifted << 2);
    const ModSequence& seq = modSequences[mods];

    size_t idx = 0;
    for (size_t i = 0; i < seq.downCount; ++i)
        events.press[idx++] = makeKeybdInput(seq.down[i].scan, seq.down[i].flags);
    events.press[idx++] = makeKeybdInput(main_scan, SC_FLAG);
    for (size_t i = 0; i < seq.upCount; ++i)
        events.press[idx++] = makeKeybdInput(seq.up[i].scan, seq.up[i].flags);
    events.pressCount = idx;

    idx = 0;
    for (size_t i = 0; i < seq.downCount; ++i)
        events.release[idx++] = makeKeybdInput(seq.down[i].scan, seq.down[i].flags);
    events.release[idx++] = makeKeybdInput(main_scan, SC_FLAG | KEYEVENTF_KEYUP);
    for (size_t i = 0; i < seq.upCount; ++i)
        events.release[idx++] = makeKeybdInput(seq.up[i].scan, seq.up[i].flags);
    events.releaseCount = idx;

    events.keySource = lastChar;
    events.mainScan = main_scan;
    return events;
}

static std::unordered_map<std::string, PrecomputedKeyEvents> g_precomputeMap;
static PrecomputedKeyEvents* g_fullKeyEvents[128];
static PrecomputedKeyEvents* g_limitedKeyEvents[128];
alignas(64) static char activeKeySourceCache[256] = { 0 };

static char g_velocityMapping[128] = { 0 };

static int g_adjustedNote[128] = { 0 };

static void precomputeAllMappings(VirtualPianoPlayer& player) {
    g_precomputeMap.clear();
    std::fill(std::begin(g_fullKeyEvents), std::end(g_fullKeyEvents), nullptr);
    std::fill(std::begin(g_limitedKeyEvents), std::end(g_limitedKeyEvents), nullptr);
    std::fill(std::begin(activeKeySourceCache), std::end(activeKeySourceCache), 0);

   for (int vel = 0; vel < 128; ++vel) {
        std::string keyStr = player.getVelocityKey(vel);
        g_velocityMapping[vel] = (!keyStr.empty()) ? keyStr[0] : 0;
    }

    // Precompute the adjusted note mapping.
    if (player.eightyEightKeyModeActive || !player.ENABLE_OUT_OF_RANGE_TRANSPOSE) {
        for (int n = 0; n < 128; ++n)
            g_adjustedNote[n] = n;
    }
    else {
        for (int n = 0; n < 128; ++n) {
            int mod = n % 12;
            int lower = 36 + mod;
            int upper = 96 - (11 - mod);
            g_adjustedNote[n] = (n < 36) ? lower : (n > 96 ? upper : n);
        }
    }

    for (int midi_n = 0; midi_n < 128; ++midi_n) {
        const char* noteStr = NOTE_NAME_CACHE[midi_n];
        if (!noteStr || noteStr[0] == '\0')
            continue;
        const std::string& fullKey = player.full_key_mappings[noteStr];
        if (!fullKey.empty()) {
            try {
                auto it = g_precomputeMap.find(fullKey);
                if (it == g_precomputeMap.end()) {
                    auto [newIt, inserted] = g_precomputeMap.emplace(fullKey, computePrecomputedKeyEvents(fullKey));
                    it = newIt;
                }
                g_fullKeyEvents[midi_n] = &it->second;
            }
            catch (const std::exception& ex) {
                std::cerr << "Error loading: " << noteStr << ": " << ex.what() << std::endl;
                g_fullKeyEvents[midi_n] = nullptr;
            }
        }
        const std::string& limitedKey = player.limited_key_mappings[noteStr];
        if (!limitedKey.empty()) {
            try {
                auto it2 = g_precomputeMap.find(limitedKey);
                if (it2 == g_precomputeMap.end()) {
                    auto [newIt2, inserted] = g_precomputeMap.emplace(limitedKey, computePrecomputedKeyEvents(limitedKey));
                    it2 = newIt2;
                }
                g_limitedKeyEvents[midi_n] = &it2->second;
            }
            catch (const std::exception& ex) {
                std::cerr << "Spray error with: " << noteStr << ": " << ex.what() << std::endl;
                g_limitedKeyEvents[midi_n] = nullptr;
            }
        }
    }

    // ?
    std::atomic_thread_fence(std::memory_order_release);
}

// these have to be aligned so the compiler doesn't start bitching
alignas(64) INPUT MIDI2Key::m_velocityInputs[4] = {};
alignas(64) INPUT MIDI2Key::m_sustainInput[2] = {};
alignas(64) char  MIDI2Key::m_lastVelocityKey = '\0';

MIDI2Key::MIDI2Key(VirtualPianoPlayer* player)
    : m_rtMidiIn(nullptr)
    , m_selectedDevice(-1)
    , m_selectedChannel(-1)
    , m_isActive(false)
    , m_player(player)
{ }

MIDI2Key::~MIDI2Key() {
    CloseDevice();
}

void MIDI2Key::OpenDevice(int deviceIndex) {
    if (m_rtMidiIn) {
        try {
            CloseDevice();
        }
        catch (...) {
            m_rtMidiIn = nullptr;
        }
    }
    if (deviceIndex < 0) return;
    try {
        try {
            m_rtMidiIn = new RtMidiIn(RtMidi::WINDOWS_MM);
        }
        catch (RtMidiError&) {
            m_rtMidiIn = new RtMidiIn();
        }
        unsigned int nPorts = m_rtMidiIn->getPortCount();
        if (nPorts == 0) {
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
            return;
        }
        if ((unsigned int)deviceIndex >= nPorts)
            deviceIndex = 0;
        try {
            m_rtMidiIn->openPort(deviceIndex);
        }
        catch (RtMidiError&) {
            if (deviceIndex != 0 && nPorts > 0) {
                m_rtMidiIn->openPort(0);
                deviceIndex = 0;
            }
            else {
                delete m_rtMidiIn;
                m_rtMidiIn = nullptr;
                return;
            }
        }
        try {
            m_rtMidiIn->setCallback(&MIDI2Key::RtMidiCallback, this);
            m_rtMidiIn->ignoreTypes(false, false, false);
        }
        catch (RtMidiError&) {
            m_rtMidiIn->closePort();
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
            return;
        }
        m_selectedDevice = deviceIndex;
    }
    catch (const std::exception&) {
        if (m_rtMidiIn) {
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
        }
    }
}

void MIDI2Key::CloseDevice() {
    if (m_rtMidiIn) {
        m_rtMidiIn->cancelCallback();
        m_rtMidiIn->closePort();
        delete m_rtMidiIn;
        m_rtMidiIn = nullptr;
    }
}

void MIDI2Key::SetMidiChannel(int channel) {
    m_selectedChannel = channel;
}

bool MIDI2Key::IsActive() const {
    return m_isActive.load(std::memory_order_relaxed);
}

// TODO: rewrite this crap
void MIDI2Key::SetActive(bool active) {
    m_isActive.store(active, std::memory_order_release);
    if (active) {
        if (SyscallNumber == 0) {
            try {
                SyscallNumber = GetNtUserSendInputSyscallNumber();
            }
            catch (const std::exception& e) {
                std::cout << "Failed to load MidiIn device: " << e.what() << std::endl;
                exit(1);
            }
        }
        if (m_player)
            precomputeAllMappings(*m_player);
    }
}

int MIDI2Key::GetSelectedDevice() const {
    return m_selectedDevice;
}

int MIDI2Key::GetSelectedChannel() const {
    return m_selectedChannel;
}

void MIDI2Key::RtMidiCallback(double /*deltaTime (ignored here)*/,
    std::vector<unsigned char>* message,
    void* userData) {

    if (!message || message->size() < 3)
        return;

    auto* self = reinterpret_cast<MIDI2Key*>(userData);
    if (!self || !self->m_player || !self->IsActive())
        return;

    VirtualPianoPlayer& p = *self->m_player;

    const bool velocityKeyEnabled = p.enable_velocity_keypress.load(std::memory_order_relaxed);
    const bool volumeAdjustEnabled = p.enable_volume_adjustment.load(std::memory_order_relaxed);

    const unsigned char status = (*message)[0];
    const unsigned char data1 = (*message)[1];
    _mm_prefetch((const char*)&g_fullKeyEvents[data1], _MM_HINT_T0); // probably doesn't make a difference but we're latency obsessed psychopaths
    _mm_prefetch((const char*)&g_limitedKeyEvents[data1], _MM_HINT_T0);
    const unsigned char data2 = (*message)[2];
    const unsigned char cmd = status & 0xF0;
    const unsigned char chan = status & 0x0F;

    if (self->m_selectedChannel >= 0 && chan != (unsigned char)self->m_selectedChannel)
        return;

    int midi_n = 0;
    PrecomputedKeyEvents* eventsPtr = nullptr;
    char currentActive = 0;

    switch (cmd) {
    case 0x90: { // Note On
        if (data2 > 0) {
            if (velocityKeyEnabled) {
                const char newVelKey = g_velocityMapping[data2];
                if (newVelKey != MIDI2Key::m_lastVelocityKey) {
                    MIDI2Key::m_lastVelocityKey = newVelKey;
                    MIDI2Key::m_velocityInputs[0] = makeKeybdInput(0x38, KEYEVENTF_SCANCODE); // Alt down
                    const WORD scan = SCAN_TABLE[(unsigned char)newVelKey];
                    MIDI2Key::m_velocityInputs[1] = makeKeybdInput(scan, KEYEVENTF_SCANCODE);
                    MIDI2Key::m_velocityInputs[2] = makeKeybdInput(scan, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
                    MIDI2Key::m_velocityInputs[3] = makeKeybdInput(0x38, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP); // Alt up
                    NtUserSendInputCall(4, MIDI2Key::m_velocityInputs, sizeof(INPUT));
                }
            }
            if (volumeAdjustEnabled) { // auto vol for older games yes idk why tf do i have this at this point 
                int velocity = data2;
                if (velocity >= 0 && velocity < 128) {
                    int target_vol = p.volume_lookup[velocity];
                    int current_vol = p.current_volume.load(std::memory_order_relaxed);
                    int diff = target_vol - current_vol;
                    int step_size = midi::Config::getInstance().volume.VOLUME_STEP;
                    if (std::abs(diff) >= step_size) {
                        constexpr WORD VOL_UP_SCAN = 0x4D;
                        constexpr WORD VOL_DOWN_SCAN = 0x4B;
                        WORD sc = (diff > 0) ? VOL_UP_SCAN : VOL_DOWN_SCAN;
                        int steps = std::abs(diff) / step_size;
                        INPUT inputs[40];
                        int inputCount = steps * 2;
                        for (int i = 0; i < steps && (i * 2 + 1) < 40; ++i) {
                            inputs[i * 2] = makeKeybdInput(sc, SC_FLAG | KEYEVENTF_EXTENDEDKEY);
                            inputs[i * 2 + 1] = makeKeybdInput(sc, KU_FLAG | KEYEVENTF_EXTENDEDKEY);
                        }
                        NtUserSendInputCall(static_cast<UINT>(inputCount), inputs, sizeof(INPUT));
                        p.current_volume.store(target_vol, std::memory_order_relaxed);
                    }
                }
            }
            // get the "precomputed" note
            midi_n = g_adjustedNote[data1];
            eventsPtr = p.eightyEightKeyModeActive ? g_fullKeyEvents[midi_n] : g_limitedKeyEvents[midi_n];
            if (eventsPtr) {
                bool wasPressed = self->pressed[midi_n].exchange(true, std::memory_order_relaxed);
                currentActive = activeKeySourceCache[eventsPtr->mainScan & 0xFF];
                if (wasPressed || (currentActive && currentActive != eventsPtr->keySource))
                    NtUserSendInputCall((UINT)eventsPtr->releaseCount, eventsPtr->release.data(), sizeof(INPUT)); // probably handle the failure of this but that won't be good anyway... too bad!
                if (currentActive != eventsPtr->keySource) {
                    NtUserSendInputCall((UINT)eventsPtr->pressCount, eventsPtr->press.data(), sizeof(INPUT));
                    activeKeySourceCache[eventsPtr->mainScan & 0xFF] = eventsPtr->keySource;
                }
            }
        }
        else {
            // 0 velocity = note off because MIDI designers were high when they made the spec
            midi_n = g_adjustedNote[data1];
            eventsPtr = p.eightyEightKeyModeActive ? g_fullKeyEvents[midi_n] : g_limitedKeyEvents[midi_n];
            if (eventsPtr) {
                bool wasPressed = self->pressed[midi_n].exchange(false, std::memory_order_relaxed);
                if (wasPressed) {
                    currentActive = activeKeySourceCache[eventsPtr->mainScan & 0xFF];
                    if (currentActive == eventsPtr->keySource) {
                        NtUserSendInputCall((UINT)eventsPtr->releaseCount, eventsPtr->release.data(), sizeof(INPUT));
                        activeKeySourceCache[eventsPtr->mainScan & 0xFF] = 0;
                    }
                }
            }
        }
        break;
    }
    case 0x80: { // Note Off
        midi_n = g_adjustedNote[data1];
        eventsPtr = p.eightyEightKeyModeActive ? g_fullKeyEvents[midi_n] : g_limitedKeyEvents[midi_n];
        if (eventsPtr) {
            bool wasPressed = self->pressed[midi_n].exchange(false, std::memory_order_relaxed);
            if (wasPressed) {
                currentActive = activeKeySourceCache[eventsPtr->mainScan & 0xFF];
                if (currentActive == eventsPtr->keySource) {
                    NtUserSendInputCall((UINT)eventsPtr->releaseCount, eventsPtr->release.data(), sizeof(INPUT));
                    activeKeySourceCache[eventsPtr->mainScan & 0xFF] = 0;
                }
            }
        }
        break;
    }
    case 0xB0: { // CC
        if (data1 == 64 && p.currentSustainMode != SustainMode::IG) {
            bool pedal_on = (data2 >= g_sustainCutoff);
            bool should_press = (p.currentSustainMode == SustainMode::SPACE_DOWN) ? pedal_on : !pedal_on;
            if (should_press != p.isSustainPressed) {
                // this is staying like this idc
                constexpr WORD spaceScan = 0x39;
                MIDI2Key::m_sustainInput[0] = makeKeybdInput(spaceScan, should_press ? 0 : KEYEVENTF_KEYUP);
                NtUserSendInputCall(1, MIDI2Key::m_sustainInput, sizeof(INPUT));
                p.isSustainPressed = should_press;
            }
        }
        break;
    }
    default:
        break;
    }
}