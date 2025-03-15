// MIDI2Key.cpp

#include "MIDI2Key.hpp"
#include "InputHeader.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment(lib, "avrt.lib")

using namespace winrt;
using namespace Windows::Devices::Midi;
using namespace Windows::Devices::Enumeration;

static void setThreadToRealTime() {
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (!hTask) {
        std::wcerr << L"Failed to join MMCSS Pro Audio class" << std::endl;
    }
    else {
        BOOL ok = AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL);
        if (!ok) {
            std::wcerr << L"Failed to set MMCSS subpriority" << std::endl;
        }
    }
}
alignas(64) INPUT MIDI2Key::m_velocityInputs[4] = {};
alignas(64) INPUT MIDI2Key::m_sustainInput[2] = {};
alignas(64) char  MIDI2Key::m_lastVelocityKey = '\0';
static __forceinline INPUT makeKeybdInput(WORD wScan, DWORD dwFlags) {
    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = 0;     
    inp.ki.wScan = wScan;
    inp.ki.dwFlags = dwFlags;
    return inp;
}

// -----------------------------------------------------------------------------
// NOTE_NAME_CACHE for "C#4", "E3" etc. Precomputed at startup
// -----------------------------------------------------------------------------
static std::array<const char*, 128> NOTE_NAME_CACHE = []() {
    std::array<const char*, 128> cache{};
    constexpr std::array<const char[3], 12> names = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    static char bufs[128][8]{};
    for (int n = 0; n < 128; ++n) {
        int pitch = n % 12;
        int octave = (n / 12) - 1;
        int len = (names[pitch][1] == '#') ? 2 : 1;
        std::memcpy(bufs[n], names[pitch], len);
        sprintf_s(bufs[n] + len, sizeof(bufs[n]) - len, "%d", octave);
        cache[n] = bufs[n];
    }
    return cache;
    }();

// -----------------------------------------------------------------------------
// SCAN_TABLE: QWERTY scancodes for digits, letters, punctuation
// -----------------------------------------------------------------------------
alignas(64) static const std::array<WORD, 256> SCAN_TABLE = []() {
    std::array<WORD, 256> table{};
    table.fill(0);

    table[(unsigned char)'1'] = 0x02;   table[(unsigned char)'2'] = 0x03;
    table[(unsigned char)'3'] = 0x04;   table[(unsigned char)'4'] = 0x05;
    table[(unsigned char)'5'] = 0x06;   table[(unsigned char)'6'] = 0x07;
    table[(unsigned char)'7'] = 0x08;   table[(unsigned char)'8'] = 0x09;
    table[(unsigned char)'9'] = 0x0A;   table[(unsigned char)'0'] = 0x0B;

    table[(unsigned char)'q'] = 0x10;   table[(unsigned char)'w'] = 0x11;
    table[(unsigned char)'e'] = 0x12;   table[(unsigned char)'r'] = 0x13;
    table[(unsigned char)'t'] = 0x14;   table[(unsigned char)'y'] = 0x15;
    table[(unsigned char)'u'] = 0x16;   table[(unsigned char)'i'] = 0x17;
    table[(unsigned char)'o'] = 0x18;   table[(unsigned char)'p'] = 0x19;
    table[(unsigned char)'a'] = 0x1E;   table[(unsigned char)'s'] = 0x1F;
    table[(unsigned char)'d'] = 0x20;   table[(unsigned char)'f'] = 0x21;
    table[(unsigned char)'g'] = 0x22;   table[(unsigned char)'h'] = 0x23;
    table[(unsigned char)'j'] = 0x24;   table[(unsigned char)'k'] = 0x25;
    table[(unsigned char)'l'] = 0x26;   table[(unsigned char)'z'] = 0x2C;
    table[(unsigned char)'x'] = 0x2D;   table[(unsigned char)'c'] = 0x2E;
    table[(unsigned char)'v'] = 0x2F;   table[(unsigned char)'b'] = 0x30;
    table[(unsigned char)'n'] = 0x31;   table[(unsigned char)'m'] = 0x32;

    // Shifted digits/punctuation share same scancodes
    table[(unsigned char)'!'] = 0x02;   table[(unsigned char)'@'] = 0x03;
    table[(unsigned char)'#'] = 0x04;   table[(unsigned char)'$'] = 0x05;
    table[(unsigned char)'%'] = 0x06;   table[(unsigned char)'^'] = 0x07;
    table[(unsigned char)'&'] = 0x08;   table[(unsigned char)'*'] = 0x09;
    table[(unsigned char)'('] = 0x0A;   table[(unsigned char)')'] = 0x0B;

    return table;
    }();

// -----------------------------------------------------------------------------
// CHAR_INFO_TABLE: maps a char -> { canonical lower-case, isShifted? }
// -----------------------------------------------------------------------------
struct CharInfoRec {
    char canonical;
    bool shifted;
};

inline std::array<CharInfoRec, 256> initCharInfoTable() {
    std::array<CharInfoRec, 256> table{};
    for (int i = 0; i < 256; ++i) {
        char c = static_cast<char>(i);
        table[i] = { c, false };
        if (c >= 'A' && c <= 'Z') {
            table[i] = { static_cast<char>(c + 32), true };
        }
    }
    // numeric punctuation
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
static const auto CHAR_INFO_TABLE = initCharInfoTable();

// -----------------------------------------------------------------------------
// Mod sequences for alt, ctrl, shift combos
// -----------------------------------------------------------------------------
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

constexpr WORD ALT_SCAN = 0x38;
constexpr WORD CTRL_SCAN = 0x1D;
constexpr WORD SHIFT_SCAN = 0x2A;
constexpr DWORD SC_FLAG = KEYEVENTF_SCANCODE;
constexpr DWORD KU_FLAG = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

inline std::array<ModSequence, 8> initModSequences() {
    std::array<ModSequence, 8> seq{};

    // 0 = none
    seq[0] = {
        0, {{{0,0},{0,0},{0,0}}},
        0, {{{0,0},{0,0},{0,0}}}
    };
    // 1 = alt
    seq[1] = {
        1, {{{ALT_SCAN, SC_FLAG},{0,0},{0,0}}},
        1, {{{ALT_SCAN, KU_FLAG},{0,0},{0,0}}}
    };
    // 2 = ctrl
    seq[2] = {
        1, {{{CTRL_SCAN, SC_FLAG},{0,0},{0,0}}},
        1, {{{CTRL_SCAN, KU_FLAG},{0,0},{0,0}}}
    };
    // 3 = alt + ctrl
    seq[3] = {
        2, {{{ALT_SCAN, SC_FLAG},{CTRL_SCAN, SC_FLAG},{0,0}}},
        2, {{{CTRL_SCAN, KU_FLAG},{ALT_SCAN, KU_FLAG},{0,0}}}
    };
    // 4 = shift
    seq[4] = {
        1, {{{SHIFT_SCAN, SC_FLAG},{0,0},{0,0}}},
        1, {{{SHIFT_SCAN, KU_FLAG},{0,0},{0,0}}}
    };
    // 5 = alt + shift
    seq[5] = {
        2, {{{ALT_SCAN, SC_FLAG},{SHIFT_SCAN, SC_FLAG},{0,0}}},
        2, {{{SHIFT_SCAN, KU_FLAG},{ALT_SCAN, KU_FLAG},{0,0}}}
    };
    // 6 = ctrl + shift
    seq[6] = {
        2, {{{CTRL_SCAN, SC_FLAG},{SHIFT_SCAN, SC_FLAG},{0,0}}},
        2, {{{SHIFT_SCAN, KU_FLAG},{CTRL_SCAN, KU_FLAG},{0,0}}}
    };
    // 7 = alt + ctrl + shift
    seq[7] = {
        3, {{{ALT_SCAN, SC_FLAG},{CTRL_SCAN, SC_FLAG},{SHIFT_SCAN, SC_FLAG}}},
        3, {{{SHIFT_SCAN, KU_FLAG},{CTRL_SCAN, KU_FLAG},{ALT_SCAN, KU_FLAG}}}
    };
    return seq;
}
static const auto modSequences = initModSequences();

// -----------------------------------------------------------------------------
// PrecomputedKeyEvents: holds the press/release sequences for a single key combo
// -----------------------------------------------------------------------------
struct alignas(64) PrecomputedKeyEvents {
    size_t pressCount;
    std::array<INPUT, 16> press;
    size_t releaseCount;
    std::array<INPUT, 16> release;

    uint16_t mainScan;
    char keySource;
    uint8_t padding[45];
};

// -----------------------------------------------------------------------------
// computePrecomputedKeyEvents - builds press & release for e.g. "ctrl+a", "shift+Z", etc.
// -----------------------------------------------------------------------------
static PrecomputedKeyEvents computePrecomputedKeyEvents(std::string_view key_str) {
    PrecomputedKeyEvents evts{};
    bool hasAlt = (key_str.find("alt+") != std::string_view::npos);
    bool hasCtrl = (key_str.find("ctrl+") != std::string_view::npos);

    char lastChar = key_str.empty() ? '\0' : key_str.back();

    const CharInfoRec& info = CHAR_INFO_TABLE[(unsigned char)lastChar];
    WORD main_scan = SCAN_TABLE[(unsigned char)info.canonical];
    bool isShifted = info.shifted;

    int mods = (int)hasAlt | ((int)hasCtrl << 1) | ((int)isShifted << 2);
    const ModSequence& seq = modSequences[mods];

    // build press
    {
        size_t idx = 0;
        for (size_t i = 0; i < seq.downCount; ++i) {
            evts.press[idx++] = makeKeybdInput(seq.down[i].scan, seq.down[i].flags);
        }
        evts.press[idx++] = makeKeybdInput(main_scan, SC_FLAG); // main key down
        for (size_t i = 0; i < seq.upCount; ++i) {
            evts.press[idx++] = makeKeybdInput(seq.up[i].scan, seq.up[i].flags);
        }
        evts.pressCount = idx;
    }
    // build release
    {
        size_t idx = 0;
        for (size_t i = 0; i < seq.downCount; ++i) {
            evts.release[idx++] = makeKeybdInput(seq.down[i].scan, seq.down[i].flags);
        }
        evts.release[idx++] = makeKeybdInput(main_scan, SC_FLAG | KEYEVENTF_KEYUP);
        for (size_t i = 0; i < seq.upCount; ++i) {
            evts.release[idx++] = makeKeybdInput(seq.up[i].scan, seq.up[i].flags);
        }
        evts.releaseCount = idx;
    }
    evts.mainScan = main_scan;
    evts.keySource = lastChar;
    return evts;
}

// -----------------------------------------------------------------------------
// Global maps
// -----------------------------------------------------------------------------
static std::unordered_map<std::string, PrecomputedKeyEvents> g_precomputeMap;

// For each note [0..127], pointers to the "full" or "limited" events
static PrecomputedKeyEvents* g_fullKeyEvents[128];
static PrecomputedKeyEvents* g_limitedKeyEvents[128];

// Velocity -> char
static char g_velocityMapping[128]{};
// adjusted note for out-of-range
static int  g_adjustedNote[128]{};

// For conflict resolution by scancode
static PrecomputedKeyEvents* scancodeOwner[256]{};
static std::atomic<short>    scancodeCount[256];

// -----------------------------------------------------------------------------
// precomputeAllMappings - fill in all global caches
// -----------------------------------------------------------------------------
static void precomputeAllMappings(VirtualPianoPlayer& player) {
    g_precomputeMap.clear();

    std::fill(std::begin(g_fullKeyEvents), std::end(g_fullKeyEvents), nullptr);
    std::fill(std::begin(g_limitedKeyEvents), std::end(g_limitedKeyEvents), nullptr);

    // reset scancode ownership
    for (int i = 0; i < 256; ++i) {
        scancodeOwner[i] = nullptr;
        scancodeCount[i].store(0, std::memory_order_relaxed);
    }

    // velocity char mapping
    for (int vel = 0; vel < 128; ++vel) {
        std::string ks = player.getVelocityKey(vel);
        g_velocityMapping[vel] = ks.empty() ? 0 : ks[0];
    }

    // build adjusted note array
    if (player.eightyEightKeyModeActive || !player.ENABLE_OUT_OF_RANGE_TRANSPOSE) {
        for (int n = 0; n < 128; ++n) {
            g_adjustedNote[n] = n;
        }
    }
    else {
        for (int n = 0; n < 128; ++n) {
            int mod = n % 12;
            int lower = 36 + mod;
            int upper = 96 - (11 - mod);
            g_adjustedNote[n] = (n < 36) ? lower : (n > 96 ? upper : n);
        }
    }

    // fill fullKeyEvents + limitedKeyEvents
    for (int midi_n = 0; midi_n < 128; ++midi_n) {
        const char* noteStr = NOTE_NAME_CACHE[midi_n];
        if (!noteStr || !noteStr[0]) continue;

        // full mapping
        const std::string& fullKey = player.full_key_mappings[noteStr];
        if (!fullKey.empty()) {
            auto it = g_precomputeMap.find(fullKey);
            if (it == g_precomputeMap.end()) {
                auto [newIt, _] = g_precomputeMap.emplace(fullKey, computePrecomputedKeyEvents(fullKey));
                it = newIt;
            }
            g_fullKeyEvents[midi_n] = &it->second;
        }
        // limited mapping
        const std::string& limitedKey = player.limited_key_mappings[noteStr];
        if (!limitedKey.empty()) {
            auto it2 = g_precomputeMap.find(limitedKey);
            if (it2 == g_precomputeMap.end()) {
                auto [newIt2, _] = g_precomputeMap.emplace(limitedKey, computePrecomputedKeyEvents(limitedKey));
                it2 = newIt2;
            }
            g_limitedKeyEvents[midi_n] = &it2->second;
        }
    }
    std::atomic_thread_fence(std::memory_order_release);
}

// -----------------------------------------------------------------------------
// MIDI2Key Implementation
// -----------------------------------------------------------------------------
MIDI2Key::MIDI2Key(VirtualPianoPlayer* player)
    : m_midiInPort(nullptr)
    , m_selectedDevice(-1)
    , m_selectedChannel(-1)
    , m_isActive(false)
    , m_player(player)
{
    for (auto& b : pressed) {
        b.store(false, std::memory_order_relaxed);
    }
    init_apartment(apartment_type::multi_threaded);
    setThreadToRealTime();
}

MIDI2Key::~MIDI2Key() {
    CloseDevice();
}

void MIDI2Key::OpenDevice(int deviceIndex) {
    CloseDevice();
    if (deviceIndex < 0) return;

    auto selector = MidiInPort::GetDeviceSelector();
    auto devices = DeviceInformation::FindAllAsync(selector).get();
    if (devices.Size() == 0) {
        std::wcerr << L"No MIDI input devices found.\n";
        return;
    }
    if (deviceIndex >= devices.Size()) {
        deviceIndex = 0;
    }
    auto deviceInfo = devices.GetAt(deviceIndex);
    try {
        m_midiInPort = MidiInPort::FromIdAsync(deviceInfo.Id()).get();
    }
    catch (hresult_error const& ex) {
        std::wcerr << L"Failed to open MIDI device: " << ex.message().c_str() << std::endl;
        m_midiInPort = nullptr;
        return;
    }
    if (!m_midiInPort) return;

    // Attach callback
    m_messageToken = m_midiInPort.MessageReceived(
        [this](MidiInPort const&, MidiMessageReceivedEventArgs const& args) {
            this->ProcessMidiMessage(args.Message());
        }
    );
    m_selectedDevice = deviceIndex;
}

void MIDI2Key::CloseDevice() {
    if (m_midiInPort) {
        m_midiInPort.MessageReceived(m_messageToken);
        m_midiInPort.Close();
        m_midiInPort = nullptr;
    }
}

void MIDI2Key::SetMidiChannel(int channel) {
    m_selectedChannel = channel;
}

bool MIDI2Key::IsActive() const {
    return m_isActive.load(std::memory_order_relaxed);
}

void MIDI2Key::SetActive(bool active) {
    m_isActive.store(active, std::memory_order_release);
    if (active && m_player) {
        precomputeAllMappings(*m_player);
    }
}

int MIDI2Key::GetSelectedDevice() const {
    return m_selectedDevice;
}

int MIDI2Key::GetSelectedChannel() const {
    return m_selectedChannel;
}

void MIDI2Key::ProcessMidiMessage(IMidiMessage const& midiMessage) {
    if (!m_isActive.load(std::memory_order_acquire)) return;
    auto rawData = midiMessage.RawData();
    if (!rawData || rawData.Length() < 3) return;
    uint8_t* bytes = rawData.data();
    uint8_t status = bytes[0];
    uint8_t cmd = status & 0xF0;
    uint8_t channel = status & 0x0F;
    if (m_selectedChannel >= 0 && channel != (uint8_t)m_selectedChannel) return;

    VirtualPianoPlayer& p = *m_player;

    // Handle Note On (0x90 with velocity > 0)
    if (cmd == 0x90 && bytes[2] > 0) {
        uint8_t note = bytes[1];
        uint8_t velocity = bytes[2];
        if (p.enable_velocity_keypress.load(std::memory_order_relaxed)) {
            char newVelKey = g_velocityMapping[velocity];
            if (newVelKey != m_lastVelocityKey) {
                m_lastVelocityKey = newVelKey;
                WORD sc = SCAN_TABLE[(unsigned char)newVelKey];
                m_velocityInputs[0] = makeKeybdInput(0x38, KEYEVENTF_SCANCODE);                // Alt down
                m_velocityInputs[1] = makeKeybdInput(sc, KEYEVENTF_SCANCODE);                  // Key down
                m_velocityInputs[2] = makeKeybdInput(sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP); // Key up
                m_velocityInputs[3] = makeKeybdInput(0x38, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP); // Alt up
                NtUserSendInputCall(4, m_velocityInputs, sizeof(INPUT));
            }
        }
        if (p.enable_volume_adjustment.load(std::memory_order_relaxed)) {
            int target_vol = p.volume_lookup[velocity];
            int curr_vol = p.current_volume.load(std::memory_order_relaxed);
            int diff = target_vol - curr_vol;
            int step_size = midi::Config::getInstance().volume.VOLUME_STEP;
            if (std::abs(diff) >= step_size) {
                constexpr WORD VOL_UP_SCAN = 0x4D;   // Right arrow
                constexpr WORD VOL_DOWN_SCAN = 0x4B; // Left arrow
                WORD sc = (diff > 0) ? VOL_UP_SCAN : VOL_DOWN_SCAN;
                const int steps = (std::min)(std::abs(diff) / step_size, 20);
                INPUT inputs[40];
                for (int i = 0; i < steps; ++i) {
                    inputs[i * 2] = makeKeybdInput(sc, KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY);
                    inputs[i * 2 + 1] = makeKeybdInput(sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY);
                }
                NtUserSendInputCall(steps * 2, inputs, sizeof(INPUT));
                p.current_volume.store(target_vol, std::memory_order_relaxed);
            }
        }

        // Press the note
        int midi_n = g_adjustedNote[note];
        PrecomputedKeyEvents* evPtr = p.eightyEightKeyModeActive ? g_fullKeyEvents[midi_n] : g_limitedKeyEvents[midi_n];
        if (evPtr) {
            bool wasNotPressed = !pressed[midi_n].exchange(true, std::memory_order_relaxed);
            if (wasNotPressed) {
                WORD sc = evPtr->mainScan & 0xFF;
                _mm_prefetch((const char*)&scancodeOwner[sc], _MM_HINT_T0);
                _mm_prefetch((const char*)&scancodeCount[sc], _MM_HINT_T0);
                PrecomputedKeyEvents* ownerPtr = scancodeOwner[sc];
                if (ownerPtr && ownerPtr != evPtr) {
                    short oldCount = scancodeCount[sc].exchange(0, std::memory_order_relaxed);
                    if (oldCount > 0) {
                        NtUserSendInputCall((UINT)ownerPtr->releaseCount, ownerPtr->release.data(), sizeof(INPUT));
                    }
                    scancodeOwner[sc] = nullptr;
                }
                short cnt = scancodeCount[sc].load(std::memory_order_relaxed);
                if (cnt == 0) {
                    NtUserSendInputCall((UINT)evPtr->pressCount, evPtr->press.data(), sizeof(INPUT));
                    scancodeOwner[sc] = evPtr;
                    scancodeCount[sc].store(1, std::memory_order_relaxed);
                }
                else {
                    scancodeCount[sc].fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }
    // Handle Note Off (0x80 or 0x90 with velocity == 0)
    else if ((cmd == 0x90 && bytes[2] == 0) || cmd == 0x80) {
        uint8_t note = bytes[1];
        int midi_n = g_adjustedNote[note];
        PrecomputedKeyEvents* evPtr = p.eightyEightKeyModeActive ? g_fullKeyEvents[midi_n] : g_limitedKeyEvents[midi_n];
        if (evPtr) {
            bool wasPressed = pressed[midi_n].exchange(false, std::memory_order_relaxed);
            if (wasPressed) {
                WORD sc = evPtr->mainScan & 0xFF;
                PrecomputedKeyEvents* ownerPtr = scancodeOwner[sc];
                if (ownerPtr == evPtr) {
                    short c = scancodeCount[sc].fetch_sub(1, std::memory_order_relaxed);
                    if (c == 1) {
                        NtUserSendInputCall((UINT)evPtr->releaseCount, evPtr->release.data(), sizeof(INPUT));
                        scancodeOwner[sc] = nullptr;
                    }
                }
            }
        }
    }
    // Handle Control Change (sustain pedal, controller 64)
    else if (cmd == 0xB0 && bytes[1] == 64 && p.currentSustainMode != SustainMode::IG) {
        bool pedal_on = (bytes[2] >= g_sustainCutoff);
        bool shouldPress = (p.currentSustainMode == SustainMode::SPACE_DOWN) ? pedal_on : !pedal_on;
        if (shouldPress != p.isSustainPressed) {
            constexpr WORD spaceScan = 0x39;
            m_sustainInput[0] = makeKeybdInput(spaceScan, shouldPress ? 0 : KEYEVENTF_KEYUP);
            NtUserSendInputCall(1, m_sustainInput, sizeof(INPUT));
            p.isSustainPressed = shouldPress;
        }
    }
}