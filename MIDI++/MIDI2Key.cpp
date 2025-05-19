#include "MIDI2Key.hpp"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <timeapi.h>

// Static member initialization
char MIDI2Key::s_lastVelocityKey = '\0';
HANDLE MIDI2Key::s_mmcssHandle = NULL;
DWORD MIDI2Key::s_mmcssTaskIndex = 0;
DWORD_PTR MIDI2Key::s_originalAffinity = 0;
ULONG MIDI2Key::s_timerResolution = 0;

namespace MIDITables {
    alignas(CACHE_LINE_SIZE) std::array<NoteData, 128> g_fullKeyNotes = {};
    alignas(CACHE_LINE_SIZE) std::array<NoteData, 128> g_limitedKeyNotes = {};
    alignas(CACHE_LINE_SIZE) std::array<MIDIKeyEvent*, 256> g_scancodeOwner = {};
    alignas(CACHE_LINE_SIZE) std::array<std::atomic<short>, 256> g_scancodeCount = {};
    alignas(CACHE_LINE_SIZE) std::array<VelocityKeyData, 128> g_velocityData = {};
    alignas(CACHE_LINE_SIZE) SustainPedalData g_sustainData = {};
    alignas(CACHE_LINE_SIZE) std::array<const char*, 128> g_noteNameCache = {};
    alignas(CACHE_LINE_SIZE) std::array<int, 128> g_adjustedNoteMapping = {};
    alignas(CACHE_LINE_SIZE) std::array<WORD, 256> g_scanCodeTable = {};
    alignas(CACHE_LINE_SIZE) std::array<CharInfo, 256> g_charInfoTable = {};
    alignas(CACHE_LINE_SIZE) std::array<ModSequence, 8> g_modSequences = {};
    std::vector<std::unique_ptr<MIDIKeyEvent>> g_eventPool;

    __forceinline INPUT MakeKeyboardInput(WORD scanCode, DWORD flags) {
        INPUT inp{};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wScan = scanCode;
        inp.ki.dwFlags = flags;
        return inp;
    }

    void InitializeNoteNameCache() {
        static constexpr std::array<const char[3], 12> NOTES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static char noteBuffers[128][8] = {};
        for (int n = 0; n < 128; ++n) {
            int pitch = n % 12;
            int octave = (n / 12) - 1;
            int len = (NOTES[pitch][1] == '#') ? 2 : 1;
            std::memcpy(noteBuffers[n], NOTES[pitch], len);
            sprintf_s(noteBuffers[n] + len, sizeof(noteBuffers[n]) - len, "%d", octave);
            g_noteNameCache[n] = noteBuffers[n];
        }
    }

    void InitializeScanCodeTable() {
        g_scanCodeTable.fill(0);
        for (int i = 0; i < 10; i++) g_scanCodeTable[(unsigned char)('1' + i)] = 0x02 + i;
        g_scanCodeTable[(unsigned char)'0'] = 0x0B;
        const char* qwerty = "qwertyuiop"; for (int i = 0; i < 10; i++) g_scanCodeTable[(unsigned char)qwerty[i]] = 0x10 + i;
        const char* asdf = "asdfghjkl"; for (int i = 0; i < 9; i++) g_scanCodeTable[(unsigned char)asdf[i]] = 0x1E + i;
        const char* zxcv = "zxcvbnm"; for (int i = 0; i < 7; i++) g_scanCodeTable[(unsigned char)zxcv[i]] = 0x2C + i;
        g_scanCodeTable[(unsigned char)'!'] = 0x02; g_scanCodeTable[(unsigned char)'@'] = 0x03;
        g_scanCodeTable[(unsigned char)'#'] = 0x04; g_scanCodeTable[(unsigned char)'$'] = 0x05;
        g_scanCodeTable[(unsigned char)'%'] = 0x06; g_scanCodeTable[(unsigned char)'^'] = 0x07;
        g_scanCodeTable[(unsigned char)'&'] = 0x08; g_scanCodeTable[(unsigned char)'*'] = 0x09;
        g_scanCodeTable[(unsigned char)'('] = 0x0A; g_scanCodeTable[(unsigned char)')'] = 0x0B;
    }

    void InitializeCharInfoTable() {
        for (int i = 0; i < 256; ++i) {
            char c = static_cast<char>(i);
            g_charInfoTable[i] = { c, false };
            if (c >= 'A' && c <= 'Z') g_charInfoTable[i] = { static_cast<char>(c + 32), true };
        }
        g_charInfoTable[(unsigned char)'!'] = { '1', true }; g_charInfoTable[(unsigned char)'@'] = { '2', true };
        g_charInfoTable[(unsigned char)'#'] = { '3', true }; g_charInfoTable[(unsigned char)'$'] = { '4', true };
        g_charInfoTable[(unsigned char)'%'] = { '5', true }; g_charInfoTable[(unsigned char)'^'] = { '6', true };
        g_charInfoTable[(unsigned char)'&'] = { '7', true }; g_charInfoTable[(unsigned char)'*'] = { '8', true };
        g_charInfoTable[(unsigned char)'('] = { '9', true }; g_charInfoTable[(unsigned char)')'] = { '0', true };
    }

    void InitializeModSequences() {
        g_modSequences[0] = { 0, {}, 0, {} };
        g_modSequences[1] = { 1, {{{ALT_SCAN, SC_FLAG}}}, 1, {{{ALT_SCAN, KU_FLAG}}} };
        g_modSequences[2] = { 1, {{{CTRL_SCAN, SC_FLAG}}}, 1, {{{CTRL_SCAN, KU_FLAG}}} };
        g_modSequences[3] = { 2, {{{ALT_SCAN, SC_FLAG}, {CTRL_SCAN, SC_FLAG}}}, 2, {{{CTRL_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}}} };
        g_modSequences[4] = { 1, {{{SHIFT_SCAN, SC_FLAG}}}, 1, {{{SHIFT_SCAN, KU_FLAG}}} };
        g_modSequences[5] = { 2, {{{ALT_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}}}, 2, {{{SHIFT_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}}} };
        g_modSequences[6] = { 2, {{{CTRL_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}}}, 2, {{{SHIFT_SCAN, KU_FLAG}, {CTRL_SCAN, KU_FLAG}}} };
        g_modSequences[7] = { 3, {{{ALT_SCAN, SC_FLAG}, {CTRL_SCAN, SC_FLAG}, {SHIFT_SCAN, SC_FLAG}}}, 3, {{{SHIFT_SCAN, KU_FLAG}, {CTRL_SCAN, KU_FLAG}, {ALT_SCAN, KU_FLAG}}} };
    }

    MIDIKeyEvent* PrecomputeKeyEvents(std::string_view keyStr) {
        auto eventData = std::make_unique<MIDIKeyEvent>();
        std::memset(eventData.get(), 0, sizeof(MIDIKeyEvent));
        bool hasCtrl = keyStr.find("ctrl+") != std::string_view::npos;
        char lastChar = keyStr.back();
        const CharInfo& charInfo = g_charInfoTable[(unsigned char)lastChar];
        WORD mainScan = g_scanCodeTable[(unsigned char)charInfo.canonical];
        bool isShifted = charInfo.shifted;
        int mods = (hasCtrl ? 2 : 0) | (isShifted ? 4 : 0);
        const ModSequence& modSeq = g_modSequences[mods];
        size_t idx = 0;
        for (size_t i = 0; i < modSeq.downCount; ++i)
            eventData->press[idx++] = MakeKeyboardInput(modSeq.down[i].first, modSeq.down[i].second);
        eventData->press[idx++] = MakeKeyboardInput(mainScan, SC_FLAG);
        for (size_t i = 0; i < modSeq.upCount; ++i)
            eventData->press[idx++] = MakeKeyboardInput(modSeq.up[i].first, modSeq.up[i].second);
        eventData->pressCount = idx;
        idx = 0;
        for (size_t i = 0; i < modSeq.downCount; ++i)
            eventData->release[idx++] = MakeKeyboardInput(modSeq.down[i].first, modSeq.down[i].second);
        eventData->release[idx++] = MakeKeyboardInput(mainScan, KU_FLAG);
        for (size_t i = 0; i < modSeq.upCount; ++i)
            eventData->release[idx++] = MakeKeyboardInput(modSeq.up[i].first, modSeq.up[i].second);
        eventData->releaseCount = idx;
        eventData->mainScan = mainScan;
        eventData->keySource = lastChar;
        eventData->hasModifiers = (mods != 0);
        g_eventPool.push_back(std::move(eventData));
        return g_eventPool.back().get();
    }

    void PrecomputeVelocityKeyData(VirtualPianoPlayer& player) {
        for (int vel = 0; vel < 128; ++vel) {
            std::string keyStr = player.getVelocityKey(vel);
            VelocityKeyData& data = g_velocityData[vel];
            std::memset(&data, 0, sizeof(VelocityKeyData));
            if (!keyStr.empty()) {
                char keyChar = keyStr[0];
                WORD scanCode = g_scanCodeTable[(unsigned char)keyChar];
                data.keyChar = keyChar;
                data.hasValidKey = true;
                data.inputs[0] = MakeKeyboardInput(ALT_SCAN, SC_FLAG);
                data.inputs[1] = MakeKeyboardInput(scanCode, SC_FLAG);
                data.inputs[2] = MakeKeyboardInput(scanCode, KU_FLAG);
                data.inputs[3] = MakeKeyboardInput(ALT_SCAN, KU_FLAG);
                data.inputCount = 4;
            }
        }
    }

    void PrecomputeSustainPedalData() {
        g_sustainData.pressInput = MakeKeyboardInput(SPACE_SCAN, SC_FLAG);
        g_sustainData.releaseInput = MakeKeyboardInput(SPACE_SCAN, KU_FLAG);
    }

    void Initialize(VirtualPianoPlayer& player) {
        static bool initialized = false;
        if (!initialized) {
            InitializeNoteNameCache();
            InitializeScanCodeTable();
            InitializeCharInfoTable();
            InitializeModSequences();
            initialized = true;
        }
        for (auto& note : g_fullKeyNotes) {
            note.isPressed.store(false, std::memory_order_relaxed);
            note.keyEvent = nullptr;
            note.scanCode = 0;
        }
        for (auto& note : g_limitedKeyNotes) {
            note.isPressed.store(false, std::memory_order_relaxed);
            note.keyEvent = nullptr;
            note.scanCode = 0;
        }
        g_scancodeOwner.fill(nullptr);
        for (auto& count : g_scancodeCount) count = 0;
        PrecomputeVelocityKeyData(player);
        PrecomputeSustainPedalData();
        g_eventPool.clear();
        for (int n = 0; n < 128; ++n) g_adjustedNoteMapping[n] = n; // Simplified: no transpose for now
        std::unordered_map<std::string, MIDIKeyEvent*> eventCache;
        for (int midi_n = 0; midi_n < 128; ++midi_n) {
            const char* noteStr = g_noteNameCache[midi_n];
            if (!noteStr || !*noteStr) continue;
            const std::string& fullKey = player.full_key_mappings[noteStr];
            if (!fullKey.empty()) {
                auto it = eventCache.find(fullKey);
                MIDIKeyEvent* event = it != eventCache.end() ? it->second : (eventCache[fullKey] = PrecomputeKeyEvents(fullKey));
                g_fullKeyNotes[midi_n].isPressed.store(false, std::memory_order_relaxed);
                g_fullKeyNotes[midi_n].keyEvent = event;
                g_fullKeyNotes[midi_n].scanCode = event->mainScan;
            }
            const std::string& limitedKey = player.limited_key_mappings[noteStr];
            if (!limitedKey.empty()) {
                auto it = eventCache.find(limitedKey);
                MIDIKeyEvent* event = it != eventCache.end() ? it->second : (eventCache[limitedKey] = PrecomputeKeyEvents(limitedKey));
                g_limitedKeyNotes[midi_n].isPressed.store(false, std::memory_order_relaxed);
                g_limitedKeyNotes[midi_n].keyEvent = event;
                g_limitedKeyNotes[midi_n].scanCode = event->mainScan;
            }
        }
    }

    void Cleanup() {
        for (auto& note : g_fullKeyNotes) note.keyEvent = nullptr;
        for (auto& note : g_limitedKeyNotes) note.keyEvent = nullptr;
        g_scancodeOwner.fill(nullptr);
        g_eventPool.clear();
    }
}

// MIDI2Key implementation
MIDI2Key::MIDI2Key(VirtualPianoPlayer* player)
    : m_rtMidiIn(nullptr), m_selectedDevice(-1), m_selectedChannel(-1), m_isActive(false), m_player(player), m_inCallback(false) {}

MIDI2Key::~MIDI2Key() {
    SetActive(false);
    CloseDevice();
    if (s_mmcssHandle) { AvRevertMmThreadCharacteristics(s_mmcssHandle); s_mmcssHandle = NULL; }
    if (s_timerResolution) { timeEndPeriod(s_timerResolution); s_timerResolution = 0; }
}

bool MIDI2Key::OptimizeSystem() {
    TIMECAPS tc;
    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) return false;
    s_timerResolution = std::min(std::max(tc.wPeriodMin, (UINT)1), tc.wPeriodMax);
    if (timeBeginPeriod(s_timerResolution) != TIMERR_NOERROR) return false;
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    return true;
}

void MIDI2Key::RestoreSystemDefaults() {
    if (s_timerResolution) { timeEndPeriod(s_timerResolution); s_timerResolution = 0; }
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}

void MIDI2Key::SetCallbackThreadPriority() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    DWORD taskIndex = 0;
    if (HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex)) {
        if (AvSetMmThreadPriority(h, AVRT_PRIORITY_CRITICAL)) { s_mmcssHandle = h; s_mmcssTaskIndex = taskIndex; }
        else AvRevertMmThreadCharacteristics(h);
    }
}

void MIDI2Key::OpenDevice(int deviceIndex) {
    if (m_rtMidiIn) CloseDevice();
    if (deviceIndex < 0) return;
    try {
        m_rtMidiIn = new RtMidiIn(RtMidi::Api::WINDOWS_MM, "MIDI2Key", 100);
        if (m_rtMidiIn->getPortCount() == 0) { delete m_rtMidiIn; m_rtMidiIn = nullptr; return; }
        m_rtMidiIn->setCallback(&RtMidiCallback, this);
        m_rtMidiIn->ignoreTypes(true, true, true);
        m_rtMidiIn->setBufferSize(256, 1);
        m_rtMidiIn->openPort(deviceIndex < (int)m_rtMidiIn->getPortCount() ? deviceIndex : 0);
        SetCallbackThreadPriority();
        m_selectedDevice = deviceIndex;
    }
    catch (...) {
        if (m_rtMidiIn) { delete m_rtMidiIn; m_rtMidiIn = nullptr; }
    }
}

void MIDI2Key::CloseDevice() {
    if (m_rtMidiIn) { m_rtMidiIn->cancelCallback(); m_rtMidiIn->closePort(); delete m_rtMidiIn; m_rtMidiIn = nullptr; }
}

void MIDI2Key::SetMidiChannel(int channel) { m_selectedChannel = channel; }
bool MIDI2Key::IsActive() const { return m_isActive.load(std::memory_order_relaxed); }
int MIDI2Key::GetSelectedDevice() const { return m_selectedDevice; }
int MIDI2Key::GetSelectedChannel() const { return m_selectedChannel; }

void MIDI2Key::SetActive(bool active) {
    bool wasActive = m_isActive.exchange(active);
    if (active && !wasActive && m_player) {
        if (SyscallNumber == 0) {
            try {
                SyscallNumber = GetNtUserSendInputSyscallNumber();
                InitializeNtUserSendInputCall();
            }
            catch (...) {
                m_isActive = false;
                return;
            }
        }
        MIDITables::Initialize(*m_player);
    }
}

void __stdcall MIDI2Key::RtMidiCallback(double, std::vector<unsigned char>* message, void* userData) {
    if (!message || message->size() < 3) return;
    auto* self = static_cast<MIDI2Key*>(userData);
    if (!self || !self->m_player || !self->IsActive() || self->m_inCallback.exchange(true)) {
        if (self) self->m_inCallback = false;
        return;
    }

    unsigned char status = (*message)[0], data1 = (*message)[1], data2 = (*message)[2];
    unsigned char cmd = status & 0xF0, chan = status & 0x0F;
    if (self->m_selectedChannel >= 0 && chan != (unsigned char)self->m_selectedChannel) {
        self->m_inCallback = false;
        return;
    }

    size_t inputCount = 0;
    INPUT* batched = self->m_batchedInputs.data();
    VirtualPianoPlayer& player = *self->m_player;
    constexpr int SUSTAIN_CUTOFF = 64;

    int midi_n;
    MIDITables::NoteData* noteData;

    switch (cmd) {
    case MIDI_NOTE_ON:
        if (data2 > 0) {
            if (player.enable_velocity_keypress) {
                const auto& velData = MIDITables::g_velocityData[data2];
                if (velData.hasValidKey && velData.keyChar != s_lastVelocityKey) {
                    s_lastVelocityKey = velData.keyChar;
                    size_t toCopy = std::min(velData.inputCount, MAX_BATCH_INPUTSS - inputCount);
                    std::memcpy(batched + inputCount, velData.inputs, toCopy * sizeof(INPUT));
                    inputCount += toCopy;
                }
            }
            midi_n = MIDITables::g_adjustedNoteMapping[data1];
            auto& noteData = player.eightyEightKeyModeActive ?
                MIDITables::g_fullKeyNotes[midi_n] : MIDITables::g_limitedKeyNotes[midi_n];

            if (noteData.keyEvent && !noteData.isPressed.exchange(true)) {
                WORD sc = noteData.scanCode;
                auto* owner = MIDITables::g_scancodeOwner[sc];
                if (owner && owner != noteData.keyEvent) {
                    short oldCount = MIDITables::g_scancodeCount[sc].exchange(0);
                    if (oldCount > 0) {
                        size_t toCopy = std::min(owner->releaseCount, MAX_BATCH_INPUTSS - inputCount);
                        std::memcpy(batched + inputCount, owner->release, toCopy * sizeof(INPUT));
                        inputCount += toCopy;
                    }
                    MIDITables::g_scancodeOwner[sc] = nullptr;
                }
                short count = MIDITables::g_scancodeCount[sc];
                if (count == 0) {
                    size_t toCopy = std::min(noteData.keyEvent->pressCount, MAX_BATCH_INPUTSS - inputCount);
                    std::memcpy(batched + inputCount, noteData.keyEvent->press, toCopy * sizeof(INPUT));
                    inputCount += toCopy;
                    MIDITables::g_scancodeOwner[sc] = noteData.keyEvent;
                    MIDITables::g_scancodeCount[sc] = 1;
                }
                else {
                    MIDITables::g_scancodeCount[sc]++;
                }
            }
        }
        else {
            midi_n = MIDITables::g_adjustedNoteMapping[data1];
            auto& noteData = player.eightyEightKeyModeActive ?
                MIDITables::g_fullKeyNotes[midi_n] : MIDITables::g_limitedKeyNotes[midi_n];

            if (noteData.keyEvent && noteData.isPressed.exchange(false)) {
                WORD sc = noteData.scanCode;
                if (MIDITables::g_scancodeOwner[sc] == noteData.keyEvent) {
                    short count = MIDITables::g_scancodeCount[sc]--;
                    if (count == 1) {
                        size_t toCopy = std::min(noteData.keyEvent->releaseCount, MAX_BATCH_INPUTSS - inputCount);
                        std::memcpy(batched + inputCount, noteData.keyEvent->release, toCopy * sizeof(INPUT));
                        inputCount += toCopy;
                        MIDITables::g_scancodeOwner[sc] = nullptr;
                    }
                }
            }
        }
        break;

    case MIDI_NOTE_OFF: {
        midi_n = MIDITables::g_adjustedNoteMapping[data1];
        auto& noteData = player.eightyEightKeyModeActive ?
            MIDITables::g_fullKeyNotes[midi_n] : MIDITables::g_limitedKeyNotes[midi_n];

        if (noteData.keyEvent && noteData.isPressed.exchange(false)) {
            WORD sc = noteData.scanCode;
            if (MIDITables::g_scancodeOwner[sc] == noteData.keyEvent) {
                short count = MIDITables::g_scancodeCount[sc]--;
                if (count == 1) {
                    size_t toCopy = std::min(noteData.keyEvent->releaseCount, MAX_BATCH_INPUTSS - inputCount);
                    std::memcpy(batched + inputCount, noteData.keyEvent->release, toCopy * sizeof(INPUT));
                    inputCount += toCopy;
                    MIDITables::g_scancodeOwner[sc] = nullptr;
                }
            }
        }
        break;
    }

    case MIDI_CONTROL_CHANGE:
        if (data1 == 64 && player.currentSustainMode != SustainMode::IG) {
            bool pedalOn = data2 >= SUSTAIN_CUTOFF;
            bool shouldPress = (player.currentSustainMode == SustainMode::SPACE_DOWN) ? pedalOn : !pedalOn;
            if (shouldPress != player.isSustainPressed) {
                if (inputCount < MAX_BATCH_INPUTSS) {
                    batched[inputCount++] = shouldPress ?
                        MIDITables::g_sustainData.pressInput : MIDITables::g_sustainData.releaseInput;
                    player.isSustainPressed = shouldPress;
                }
            }
        }
        break;
    }

    if (inputCount) NtUserSendInputCall(inputCount, batched, sizeof(INPUT));
    self->m_inCallback = false;
}