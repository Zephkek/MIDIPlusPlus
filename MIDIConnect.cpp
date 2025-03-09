// Henry never announced MIDI++, shame

#include "MIDIConnect.hpp"
#include "InputHeader.h"
#include <windows.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
#include <array>

namespace {
    // 128 entries: 0 for [0,11], 1 for [12,23], etc.
    // rest of this spaghetti nightmare
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
}

// MIDIConnect Implementation

MIDIConnect::MIDIConnect()
    : m_rtMidiIn(nullptr)
    , m_selectedDevice(-1)
    , m_isActive(false)
{
    // Preinitialize the key buffer.
    for (int i = 0; i < 10; ++i) {
        INPUT& inp = m_keyBuffer.inputs[i];
        inp.type = INPUT_KEYBOARD;
        inp.ki.wScan = 0;
        inp.ki.time = 0;
        inp.ki.dwExtraInfo = 0;
        // Even indices: key press; odd indices: key release.
        inp.ki.dwFlags = (i & 1) ? (KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP) : KEYEVENTF_SCANCODE;
    }
    m_keyBuffer.initialized = true;

    // black piano rooms magic i guess
    for (int note = 0; note < 128; ++note) {
        for (int vel = 0; vel < 128; ++vel) {
            // Get pointer to the 10-element mapping array for this note & velocity
            INPUT* mapping = m_noteMapping[note][vel].data();
            mapping[0].type = INPUT_KEYBOARD;
            mapping[0].ki.wScan = 0x37;
            mapping[0].ki.time = 0;
            mapping[0].ki.dwExtraInfo = 0;
            mapping[0].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[1].type = INPUT_KEYBOARD;
            mapping[1].ki.wScan = 0x37;
            mapping[1].ki.time = 0;
            mapping[1].ki.dwExtraInfo = 0;
            mapping[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            // Map note value.
            auto noteOct = NUMPAD_SCANCODES[div12[note]];
            auto noteVal = NUMPAD_SCANCODES[mod12[note]];

            mapping[2].type = INPUT_KEYBOARD;
            mapping[2].ki.wScan = noteOct.down;
            mapping[2].ki.time = 0;
            mapping[2].ki.dwExtraInfo = 0;
            mapping[2].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[3].type = INPUT_KEYBOARD;
            mapping[3].ki.wScan = noteOct.up;
            mapping[3].ki.time = 0;
            mapping[3].ki.dwExtraInfo = 0;
            mapping[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            mapping[4].type = INPUT_KEYBOARD;
            mapping[4].ki.wScan = noteVal.down;
            mapping[4].ki.time = 0;
            mapping[4].ki.dwExtraInfo = 0;
            mapping[4].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[5].type = INPUT_KEYBOARD;
            mapping[5].ki.wScan = noteVal.up;
            mapping[5].ki.time = 0;
            mapping[5].ki.dwExtraInfo = 0;
            mapping[5].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            auto velOct = NUMPAD_SCANCODES[div12[vel]];
            auto velVal = NUMPAD_SCANCODES[mod12[vel]];

            mapping[6].type = INPUT_KEYBOARD;
            mapping[6].ki.wScan = velOct.down;
            mapping[6].ki.time = 0;
            mapping[6].ki.dwExtraInfo = 0;
            mapping[6].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[7].type = INPUT_KEYBOARD;
            mapping[7].ki.wScan = velOct.up;
            mapping[7].ki.time = 0;
            mapping[7].ki.dwExtraInfo = 0;
            mapping[7].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

            mapping[8].type = INPUT_KEYBOARD;
            mapping[8].ki.wScan = velVal.down;
            mapping[8].ki.time = 0;
            mapping[8].ki.dwExtraInfo = 0;
            mapping[8].ki.dwFlags = KEYEVENTF_SCANCODE;

            mapping[9].type = INPUT_KEYBOARD;
            mapping[9].ki.wScan = velVal.up;
            mapping[9].ki.time = 0;
            mapping[9].ki.dwExtraInfo = 0;
            mapping[9].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        }
    }
    constexpr BYTE SUSTAIN_NOTE = 143; // ok
    auto sOct = NUMPAD_SCANCODES[SUSTAIN_NOTE / 12];
    auto sVal = NUMPAD_SCANCODES[SUSTAIN_NOTE % 12];
    for (int val = 0; val < 128; ++val) {
        INPUT* mapping = m_sustainMapping[val].data();
        // First two entries: fixed key code.
        mapping[0].type = INPUT_KEYBOARD;
        mapping[0].ki.wScan = 0x37;
        mapping[0].ki.time = 0;
        mapping[0].ki.dwExtraInfo = 0;
        mapping[0].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[1].type = INPUT_KEYBOARD;
        mapping[1].ki.wScan = 0x37;
        mapping[1].ki.time = 0;
        mapping[1].ki.dwExtraInfo = 0;
        mapping[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        // Sustain note mapping.
        mapping[2].type = INPUT_KEYBOARD;
        mapping[2].ki.wScan = sOct.down;
        mapping[2].ki.time = 0;
        mapping[2].ki.dwExtraInfo = 0;
        mapping[2].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[3].type = INPUT_KEYBOARD;
        mapping[3].ki.wScan = sOct.up;
        mapping[3].ki.time = 0;
        mapping[3].ki.dwExtraInfo = 0;
        mapping[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[4].type = INPUT_KEYBOARD;
        mapping[4].ki.wScan = sVal.down;
        mapping[4].ki.time = 0;
        mapping[4].ki.dwExtraInfo = 0;
        mapping[4].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[5].type = INPUT_KEYBOARD;
        mapping[5].ki.wScan = sVal.up;
        mapping[5].ki.time = 0;
        mapping[5].ki.dwExtraInfo = 0;
        mapping[5].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        // Map sustain pedal value.
        auto valOct = NUMPAD_SCANCODES[div12[val]];
        auto valVal = NUMPAD_SCANCODES[mod12[val]];

        mapping[6].type = INPUT_KEYBOARD;
        mapping[6].ki.wScan = valOct.down;
        mapping[6].ki.time = 0;
        mapping[6].ki.dwExtraInfo = 0;
        mapping[6].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[7].type = INPUT_KEYBOARD;
        mapping[7].ki.wScan = valOct.up;
        mapping[7].ki.time = 0;
        mapping[7].ki.dwExtraInfo = 0;
        mapping[7].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        mapping[8].type = INPUT_KEYBOARD;
        mapping[8].ki.wScan = valVal.down;
        mapping[8].ki.time = 0;
        mapping[8].ki.dwExtraInfo = 0;
        mapping[8].ki.dwFlags = KEYEVENTF_SCANCODE;

        mapping[9].type = INPUT_KEYBOARD;
        mapping[9].ki.wScan = valVal.up;
        mapping[9].ki.time = 0;
        mapping[9].ki.dwExtraInfo = 0;
        mapping[9].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    }
}

MIDIConnect::~MIDIConnect() {
    CloseDevice();
}

void MIDIConnect::OpenDevice(int deviceIndex) {
    // If a device is already open, close it.
    if (m_rtMidiIn) {
        try {
            CloseDevice();
        }
        catch (...) {
            m_rtMidiIn = nullptr;
        }
    }

    if (deviceIndex < 0) {
        std::cerr << "Invalid device index: " << deviceIndex << std::endl;
        return;
    }

    try {
        // Try creating the RtMidiIn instance with the Windows multimedia API.
        try {
            m_rtMidiIn = new RtMidiIn(RtMidi::WINDOWS_MM);
        }
        catch (RtMidiError& e) {
            std::cerr << "Failed to initialize MIDI (Windows MM): " << e.getMessage() << std::endl;
            std::cerr << "Attempting fallback to default API..." << std::endl;
            try {
                m_rtMidiIn = new RtMidiIn();
            }
            catch (RtMidiError& e2) {
                std::cerr << "Failed to initialize MIDI with default API: " << e2.getMessage() << std::endl;
                m_rtMidiIn = nullptr;
                return;
            }
        }

        // Check for available ports.
        unsigned int nPorts = m_rtMidiIn->getPortCount();
        if (nPorts == 0) {
            std::cerr << "No MIDI input ports available" << std::endl;
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
            return;
        }

        // Validate device index.
        if (static_cast<unsigned int>(deviceIndex) >= nPorts) {
            std::cerr << "Requested device index " << deviceIndex
                << " exceeds available ports (" << nPorts << ")" << std::endl;
            if (nPorts > 0) {
                std::cout << "Falling back to first available device (index 0)" << std::endl;
                deviceIndex = 0;
            }
            else {
                delete m_rtMidiIn;
                m_rtMidiIn = nullptr;
                return;
            }
        }

        // Try opening the port.
        try {
            m_rtMidiIn->openPort(deviceIndex);
        }
        catch (RtMidiError& e) {
            std::cerr << "Failed to open MIDI port " << deviceIndex
                << ": " << e.getMessage() << std::endl;
            if (deviceIndex != 0 && nPorts > 0) {
                std::cout << "Attempting to fall back to port 0..." << std::endl;
                try {
                    m_rtMidiIn->openPort(0);
                    deviceIndex = 0;
                }
                catch (RtMidiError& e2) {
                    std::cerr << "Fallback also failed: " << e2.getMessage() << std::endl;
                    delete m_rtMidiIn;
                    m_rtMidiIn = nullptr;
                    return;
                }
            }
            else {
                delete m_rtMidiIn;
                m_rtMidiIn = nullptr;
                return;
            }
        }

        // Set up the callback and configure the port.
        try {
            m_rtMidiIn->setCallback(&MIDIConnect::RtMidiCallback, this);
            m_rtMidiIn->ignoreTypes(false, false, false);
        }
        catch (RtMidiError& e) {
            std::cerr << "Failed to configure MIDI port: " << e.getMessage() << std::endl;
            m_rtMidiIn->closePort();
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
            return;
        }

        m_selectedDevice = deviceIndex;
        std::cout << "Successfully opened MIDI device " << deviceIndex << std::endl;
        try {
            std::string portName = m_rtMidiIn->getPortName(deviceIndex);
            std::cout << "Connected to: " << portName << std::endl;
        }
        catch (...) {
            // Non-critical: ignore failures to retrieve the port name.
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Unexpected error while opening MIDI device: " << e.what() << std::endl;
        if (m_rtMidiIn) {
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
        }
    }
    catch (...) {
        std::cerr << "Unknown error while opening MIDI device" << std::endl;
        if (m_rtMidiIn) {
            delete m_rtMidiIn;
            m_rtMidiIn = nullptr;
        }
    }
}

void MIDIConnect::CloseDevice() {
    if (m_rtMidiIn) {
        m_rtMidiIn->cancelCallback();
        m_rtMidiIn->closePort();
        delete m_rtMidiIn;
        m_rtMidiIn = nullptr;
    }
}

void MIDIConnect::SetActive(bool active) {
    m_isActive.store(active, std::memory_order_release);
    if (active) {
        // Example: Load the syscall number if needed.
        if (SyscallNumber == 0) {
            try {
                SyscallNumber = GetNtUserSendInputSyscallNumber();
            }
            catch (const std::exception& e) {
                std::cout << "Failed to load MidiIn device: " << e.what() << std::endl;
                exit(1);
            }
        }
    }
}

void MIDIConnect::ReleaseAllNumpadKeys() {
    // Build a local array for releasing each numpad key.
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

// -----------------------------------------------------------------------------
// The MIDI callback: ultra-fast thanks to precomputed mappings.

void MIDIConnect::RtMidiCallback(double /*deltaTime*/,
    std::vector<unsigned char>* message,
    void* userData)
{
    if (!message || message->size() < 3)
        return;

    MIDIConnect* self = reinterpret_cast<MIDIConnect*>(userData);
    if (!self || !self->IsActive())
        return;

    const unsigned char* msg = message->data();
    const unsigned char status = msg[0];
    const unsigned char data1 = msg[1];
    const unsigned char data2 = msg[2];
    const unsigned char cmd = status & 0xF0;

    // For note on/off events.
    if (cmd == 0x90 || cmd == 0x80) {
        // For note-off, force velocity to 0.
        const BYTE velocity = (cmd == 0x90) ? data2 : 0;
        // Lookup precomputed mapping.
        const auto& mapping = self->m_noteMapping[data1][velocity];
        // NtUserSendInputCall expects LPINPUT (non-const), so cast away const butttttt if this fails, bad.. but that won't happen right?
        NtUserSendInputCall(10, const_cast<INPUT*>(mapping.data()), sizeof(INPUT));
    }
    // For sustain pedal events (controller 64).
    else if (cmd == 0xB0 && data1 == 64) {
        const auto& mapping = self->m_sustainMapping[data2];
        NtUserSendInputCall(10, const_cast<INPUT*>(mapping.data()), sizeof(INPUT));
    }
}