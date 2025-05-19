#include "MIDIDeviceUI.hpp"
#include <mmsystem.h>
// MIDIDeviceUI.cpp modifications

bool MIDIDeviceUI::TestDeviceAccess(UINT deviceIndex) {
    HMIDIIN hMidiIn;
    MMRESULT result = midiInOpen(&hMidiIn, deviceIndex, 0, 0, CALLBACK_NULL);
    if (result == MMSYSERR_NOERROR) {
        midiInClose(hMidiIn);
        return true;
    }
    return false;
}

void MIDIDeviceUI::PopulateMidiInDevices(HWND combo, int& selectedDevice) {
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    UINT numDevs = midiInGetNumDevs();

    bool foundValidDevice = false;
    int validDeviceCount = 0;

    for (UINT i = 0; i < numDevs; i++) {
        MIDIINCAPS mic{};
        if (midiInGetDevCaps(i, &mic, sizeof(mic)) == MMSYSERR_NOERROR) {
            if (TestDeviceAccess(i)) {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)mic.szPname);
                validDeviceCount++;

                if (i == selectedDevice) {
                    foundValidDevice = true;
                    SendMessage(combo, CB_SETCURSEL, validDeviceCount - 1, 0);
                }
            }
        }
    }

    if (validDeviceCount > 0) {
        if (!foundValidDevice) {
            SendMessage(combo, CB_SETCURSEL, 0, 0);
            selectedDevice = 0;
        }
    }
    else {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"No Devices");
        SendMessage(combo, CB_SETCURSEL, 0, 0);
        selectedDevice = -1;
    }
}
void MIDIDeviceUI::PopulateChannelList(HWND combo, int& selectedChannel) {
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"All Channels");

    for (int ch = 0; ch < 16; ch++) {
        wchar_t buf[32];
        swprintf_s(buf, L"Channel %d", ch);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
    }

    SendMessage(combo, CB_SETCURSEL, 0, 0);
    selectedChannel = -1;
}

UINT MIDIDeviceUI::GetMidiDeviceCount() {
    return midiInGetNumDevs();
}

bool MIDIDeviceUI::GetMidiDeviceName(UINT deviceIndex, wchar_t* name, UINT nameSize) {
    if (!name || nameSize == 0) return false;

    MIDIINCAPS mic{};
    if (midiInGetDevCaps(deviceIndex, &mic, sizeof(mic)) == MMSYSERR_NOERROR) {
        wcsncpy_s(name, nameSize, mic.szPname, _TRUNCATE);
        return true;
    }
    return false;
}