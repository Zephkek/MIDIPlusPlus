#pragma once
#include "Input.hpp"
#include "config.hpp"
#include <array>
#include <atomic>
#include <string>

namespace out {

using Mode = midi::OutputMode;

struct Activity {
    std::atomic<uint64_t> mask[2];
    std::atomic<uint32_t> hit;
};

class Sink {
public:
    virtual ~Sink() = default;
    virtual void on(input::Batch& b, int ch, int note, int vel, uint32_t hold = 0) = 0;
    virtual void off(input::Batch& b, int ch, int note) = 0;
    virtual void pedal(input::Batch& b, int value, bool smooth = false) = 0;
    virtual void reset(input::Batch& b) = 0;
    virtual void commit(input::Batch&) {}
    virtual void restart(input::Batch&) {}
    virtual void sostenuto(input::Batch&, int, bool = false) {}
    virtual void resume(input::Batch&, int, int) {}
    virtual void begin() {}
    virtual void end() {}
};

Sink& sink(Mode m);
void reload(bool reset = true);
void set_layout(HKL layout);
void calibrate_volume();
const char* mode_name(Mode m);
uint64_t dropped();
std::array<std::wstring, 128> labels();
std::array<int, 32> builtin_curve(int index);

}
