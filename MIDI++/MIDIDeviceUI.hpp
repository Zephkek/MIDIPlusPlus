#pragma once
#include "PlaybackSystem.hpp"
class MIDIDeviceUI {
public:
    static void PopulateMidiInDevices(HWND combo, int& selectedDevice);
    static void PopulateChannelList(HWND combo, int& selectedChannel);

    // Helper to get number of available MIDI devices
    static UINT GetMidiDeviceCount();
    static bool TestDeviceAccess(UINT deviceIndex);

    // Helper to get device name
    static bool GetMidiDeviceName(UINT deviceIndex, wchar_t* name, UINT nameSize);
};