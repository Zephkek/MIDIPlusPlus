#pragma once
#include "Output.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace live {

struct Device {
    UINT id = 0;
    std::wstring name;
    bool ks = false;
};

std::vector<Device> devices();
bool open(const Device& device, out::Mode mode, std::wstring& error);
void close();
bool active();
out::Mode mode();
const wchar_t* path();
void set_channel(int channel);
void set_key(int semitones);
void set_channel_enabled(int channel, bool on);
std::array<uint64_t, 2> held();
std::array<uint32_t, 128> hits();
const out::Activity* channels();
uint64_t messages();
void resync();
void attach(out::Mode mode);
void feed(const uint8_t* data, size_t size);

}
