#include "MIDIConnect.hpp"
#include "InputHeader.h"
#include <windows.h>
#include <iostream>
#include <winrt/Windows.Devices.Midi.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
namespace {
    static const uint8_t div12[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,
        8,8,8,8,8,8,8,8,8,8,8,8,
        9,9,9,9,9,9,9,9,9,9,9,9,
       10,10,10,10,10,10,10,10
    };
    static const uint8_t mod12[128] = {
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7,8,9,10,11,
        0,1,2,3,4,5,6,7
    };
    static void setThreadToRealTime() {
        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
        DWORD taskIndex = 0;
        HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (hTask) {
            AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL);
        }
    }
}

MIDIConnect::MIDIConnect()
    : m_midiInPort(nullptr)
    , m_selectedDevice(-1)
    , m_isActive(false)
{
    // Initialize WinRT apartment
    init_apartment(apartment_type::multi_threaded);

    setThreadToRealTime();

    // Preinitialize the key buffer
    for (int i = 0; i < 10; ++i) {
        INPUT& inp = m_keyBuffer.inputs[i];
        inp.type = INPUT_KEYBOARD;
        inp.ki.wScan = 0;
        inp.ki.time = 0;
        inp.ki.dwExtraInfo = 0;
        inp.ki.dwFlags = (i & 1) ? (KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP) : KEYEVENTF_SCANCODE;
    }
    m_keyBuffer.initialized = true;

    // Precompute note mappings
    for (int note = 0; note < 128; ++note) {
        for (int vel = 0; vel < 128; ++vel) {
            INPUT* mapping = m_noteMapping[note][vel].data();
            mapping[0].type = INPUT_KEYBOARD;
            mapping[0].ki.wScan = 0x37;
            mapping[0].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[1].type = INPUT_KEYBOARD;
            mapping[1].ki.wScan = 0x37;
            mapping[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            auto noteOct = NUMPAD_SCANCODES[div12[note]];
            auto noteVal = NUMPAD_SCANCODES[mod12[note]];

            mapping[2].type = INPUT_KEYBOARD;
            mapping[2].ki.wScan = noteOct.down;
            mapping[2].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[3].type = INPUT_KEYBOARD;
            mapping[3].ki.wScan = noteOct.up;
            mapping[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            mapping[4].type = INPUT_KEYBOARD;
            mapping[4].ki.wScan = noteVal.down;
            mapping[4].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[5].type = INPUT_KEYBOARD;
            mapping[5].ki.wScan = noteVal.up;
            mapping[5].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            auto velOct = NUMPAD_SCANCODES[div12[vel]];
            auto velVal = NUMPAD_SCANCODES[mod12[vel]];

            mapping[6].type = INPUT_KEYBOARD;
            mapping[6].ki.wScan = velOct.down;
            mapping[6].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[7].type = INPUT_KEYBOARD;
            mapping[7].ki.wScan = velOct.up;
            mapping[7].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            mapping[8].type = INPUT_KEYBOARD;
            mapping[8].ki.wScan = velVal.down;
            mapping[8].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[9].type = INPUT_KEYBOARD;
            mapping[9].ki.wScan = velVal.up;
            mapping[9].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        }
    }

    // Precompute sustain mappings
    constexpr BYTE SUSTAIN_NOTE = 143;
    auto sOct = NUMPAD_SCANCODES[SUSTAIN_NOTE / 12];
    auto sVal = NUMPAD_SCANCODES[SUSTAIN_NOTE % 12];
    for (int val = 0; val < 128; ++val) {
        INPUT* mapping = m_sustainMapping[val].data();
        mapping[0].type = INPUT_KEYBOARD;
        mapping[0].ki.wScan = 0x37;
        mapping[0].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[1].type = INPUT_KEYBOARD;
        mapping[1].ki.wScan = 0x37;
        mapping[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[2].type = INPUT_KEYBOARD;
        mapping[2].ki.wScan = sOct.down;
        mapping[2].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[3].type = INPUT_KEYBOARD;
        mapping[3].ki.wScan = sOct.up;
        mapping[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[4].type = INPUT_KEYBOARD;
        mapping[4].ki.wScan = sVal.down;
        mapping[4].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[5].type = INPUT_KEYBOARD;
        mapping[5].ki.wScan = sVal.up;
        mapping[5].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        auto valOct = NUMPAD_SCANCODES[div12[val]];
        auto valVal = NUMPAD_SCANCODES[mod12[val]];

        mapping[6].type = INPUT_KEYBOARD;
        mapping[6].ki.wScan = valOct.down;
        mapping[6].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[7].type = INPUT_KEYBOARD;
        mapping[7].ki.wScan = valOct.up;
        mapping[7].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[8].type = INPUT_KEYBOARD;
        mapping[8].ki.wScan = valVal.down;
        mapping[8].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[9].type = INPUT_KEYBOARD;
        mapping[9].ki.wScan = valVal.up;
        mapping[9].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    }
}

MIDIConnect::~MIDIConnect() {
    CloseDevice();
}

void MIDIConnect::OpenDevice(int deviceIndex) {
    CloseDevice();
    if (deviceIndex < 0) {
        std::cerr << "Invalid device index: " << deviceIndex << std::endl;
        return;
    }

    auto selector = MidiInPort::GetDeviceSelector();
    auto devices = DeviceInformation::FindAllAsync(selector).get();
    if (devices.Size() == 0) {
        std::wcerr << L"No MIDI input devices found.\n";
        return;
    }
    if (deviceIndex >= static_cast<int>(devices.Size())) {
        std::cerr << "Device index " << deviceIndex << " exceeds available devices (" << devices.Size() << "). Falling back to 0.\n";
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

    m_messageToken = m_midiInPort.MessageReceived(
        [this](MidiInPort const&, MidiMessageReceivedEventArgs const& args) {
            this->ProcessMidiMessage(args.Message());
        }
    );
    m_selectedDevice = deviceIndex;
    std::wcout << L"Successfully opened MIDI device " << deviceIndex << L": " << deviceInfo.Name().c_str() << std::endl;
}

void MIDIConnect::CloseDevice() {
    if (m_midiInPort) {
        m_midiInPort.MessageReceived(m_messageToken);
        m_midiInPort.Close();
        m_midiInPort = nullptr;
    }
    m_selectedDevice = -1;
}

void MIDIConnect::SetActive(bool active) {
    m_isActive.store(active, std::memory_order_release);
    if (active && SyscallNumber == 0) {
        try {
            SyscallNumber = GetNtUserSendInputSyscallNumber();
        }
        catch (const std::exception& e) {
            std::cout << "Failed to load syscall number: " << e.what() << std::endl;
            exit(1);
        }
    }
}

void MIDIConnect::ReleaseAllNumpadKeys() {
    INPUT inputs[12] = {};
    static const WORD numpadScans[12] = {
        0x52, 0x4F, 0x50, 0x51,
        0x4B, 0x4C, 0x4D,
        0x47, 0x48, 0x49,
        0x37,
        0x4A
    };

    for (int i = 0; i < 12; ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wScan = numpadScans[i];
        inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    }
    NtUserSendInputCall(12, inputs, sizeof(INPUT));
}

void MIDIConnect::ProcessMidiMessage(IMidiMessage const& midiMessage) {
    if (!m_isActive.load(std::memory_order_acquire)) return;

    auto rawData = midiMessage.RawData();
    if (!rawData || rawData.Length() < 3) return;

    uint8_t* bytes = rawData.data();
    uint8_t status = bytes[0];
    uint8_t cmd = status & 0xF0;
    uint8_t data1 = bytes[1];
    uint8_t data2 = bytes[2];

    if (cmd == 0x90 || cmd == 0x80) {
        const BYTE velocity = (cmd == 0x90) ? data2 : 0;
        const auto& mapping = m_noteMapping[data1][velocity];
        NtUserSendInputCall(10, const_cast<INPUT*>(mapping.data()), sizeof(INPUT));
    }
    else if (cmd == 0xB0 && data1 == 64) {
        const auto& mapping = m_sustainMapping[data2];
        NtUserSendInputCall(10, const_cast<INPUT*>(mapping.data()), sizeof(INPUT));
    }
}