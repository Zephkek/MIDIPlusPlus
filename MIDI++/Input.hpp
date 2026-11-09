#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace input {

enum Mod : uint8_t { Shift = 1, Ctrl = 2, Alt = 4 };

struct Stroke {
    uint16_t scan = 0;
    uint8_t mods = 0;
    explicit operator bool() const { return scan != 0; }
    bool operator==(const Stroke&) const = default;
};

struct Health {
    double costUs = 0.0;
    double pingMs = 0.0;
    uint64_t strikes = 0;
    uint64_t merged = 0;
    uint64_t held = 0;
    uint64_t lost = 0;
    uint8_t user = 0;
};

struct Delivery {
    uint64_t count = 0;
    double avgUs = 0.0;
    double maxUs = 0.0;
};

std::optional<Stroke> parse_key(std::string_view spec);
std::optional<Stroke> parse_typed(std::string_view spec, HKL layout);
Stroke stroke_for_vk(int vk);
int parse_vk(std::string_view name);
std::string key_label(std::string_view name);

void init();
void shutdown();
void set_sender(UINT(WINAPI* fn)(UINT, LPINPUT, int));
void set_key_state(SHORT(WINAPI* fn)(int));
void set_guard(bool on);
bool guard();
void set_target(HWND hwnd);
HWND target();
bool target_in_front();
void focus_lost();
void refresh();
void toggle_numlock();
bool wait_room(DWORD ms);
uint64_t sent_events();
uint64_t send_calls();
Health health();
Delivery delivery();

class Batch {
public:
    explicit Batch(bool force = false);
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

    bool live() const { return live_; }
    void origin(int64_t qpc);
    void press(Stroke s, uint32_t hold = 0);
    void release(Stroke s);
    void latch(Stroke s, bool down);
    void tap(Stroke s);
    void tap_scan(uint16_t scan);
    void release_all();
    void drop();
    void flush();

private:
    bool live_;
    bool force_;
};

}
