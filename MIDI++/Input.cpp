#include "Input.hpp"
#include <avrt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <thread>
#include <vector>

#pragma comment(lib, "avrt.lib")

namespace input {
namespace {

struct CharKey {
    uint8_t scan;
    uint8_t shift;
};

constexpr std::array<CharKey, 128> make_chars() {
    std::array<CharKey, 128> t{};
    auto row = [&t](const char* plain, const char* shifted, uint8_t first) {
        for (int i = 0; plain[i]; ++i) {
            t[static_cast<uint8_t>(plain[i])] = { static_cast<uint8_t>(first + i), 0 };
            t[static_cast<uint8_t>(shifted[i])] = { static_cast<uint8_t>(first + i), 1 };
        }
    };
    row("1234567890-=", "!@#$%^&*()_+", 0x02);
    row("qwertyuiop[]", "QWERTYUIOP{}", 0x10);
    row("asdfghjkl;'`", "ASDFGHJKL:\"~", 0x1E);
    row("\\zxcvbnm,./", "|ZXCVBNM<>?", 0x2B);
    t[' '] = { 0x39, 0 };
    return t;
}

constexpr auto kChars = make_chars();

struct Named {
    std::string_view name;
    uint16_t scan;
    uint8_t vk;
};

constexpr uint16_t E = 0x100;

constexpr Named kNamed[] = {
    {"space", 0x39, VK_SPACE}, {"enter", 0x1C, VK_RETURN}, {"return", 0x1C, VK_RETURN}, {"tab", 0x0F, VK_TAB},
    {"esc", 0x01, VK_ESCAPE}, {"escape", 0x01, VK_ESCAPE}, {"back", 0x0E, VK_BACK}, {"backspace", 0x0E, VK_BACK},
    {"caps", 0x3A, VK_CAPITAL}, {"capital", 0x3A, VK_CAPITAL}, {"capslock", 0x3A, VK_CAPITAL},
    {"shift", 0x2A, VK_SHIFT}, {"lshift", 0x2A, VK_LSHIFT}, {"rshift", 0x36, VK_RSHIFT},
    {"ctrl", 0x1D, VK_CONTROL}, {"control", 0x1D, VK_CONTROL}, {"lctrl", 0x1D, VK_LCONTROL},
    {"lcontrol", 0x1D, VK_LCONTROL}, {"rctrl", E | 0x1D, VK_RCONTROL}, {"rcontrol", E | 0x1D, VK_RCONTROL},
    {"alt", 0x38, VK_MENU}, {"menu", 0x38, VK_MENU}, {"lalt", 0x38, VK_LMENU}, {"lmenu", 0x38, VK_LMENU},
    {"ralt", E | 0x38, VK_RMENU}, {"rmenu", E | 0x38, VK_RMENU},
    {"up", E | 0x48, VK_UP}, {"down", E | 0x50, VK_DOWN}, {"left", E | 0x4B, VK_LEFT}, {"right", E | 0x4D, VK_RIGHT},
    {"insert", E | 0x52, VK_INSERT}, {"ins", E | 0x52, VK_INSERT}, {"delete", E | 0x53, VK_DELETE},
    {"del", E | 0x53, VK_DELETE}, {"home", E | 0x47, VK_HOME}, {"end", E | 0x4F, VK_END},
    {"pgup", E | 0x49, VK_PRIOR}, {"pageup", E | 0x49, VK_PRIOR}, {"prior", E | 0x49, VK_PRIOR},
    {"pgdn", E | 0x51, VK_NEXT}, {"pagedown", E | 0x51, VK_NEXT}, {"next", E | 0x51, VK_NEXT},
    {"f1", 0x3B, VK_F1}, {"f2", 0x3C, VK_F2}, {"f3", 0x3D, VK_F3}, {"f4", 0x3E, VK_F4}, {"f5", 0x3F, VK_F5},
    {"f6", 0x40, VK_F6}, {"f7", 0x41, VK_F7}, {"f8", 0x42, VK_F8}, {"f9", 0x43, VK_F9}, {"f10", 0x44, VK_F10},
    {"f11", 0x57, VK_F11}, {"f12", 0x58, VK_F12}, {"f13", 0x64, VK_F13}, {"f14", 0x65, VK_F14},
    {"f15", 0x66, VK_F15}, {"f16", 0x67, VK_F16}, {"f17", 0x68, VK_F17}, {"f18", 0x69, VK_F18},
    {"f19", 0x6A, VK_F19}, {"f20", 0x6B, VK_F20}, {"f21", 0x6C, VK_F21}, {"f22", 0x6D, VK_F22},
    {"f23", 0x6E, VK_F23}, {"f24", 0x76, VK_F24},
    {"numpad0", 0x52, VK_NUMPAD0}, {"numpad1", 0x4F, VK_NUMPAD1}, {"numpad2", 0x50, VK_NUMPAD2},
    {"numpad3", 0x51, VK_NUMPAD3}, {"numpad4", 0x4B, VK_NUMPAD4}, {"numpad5", 0x4C, VK_NUMPAD5},
    {"numpad6", 0x4D, VK_NUMPAD6}, {"numpad7", 0x47, VK_NUMPAD7}, {"numpad8", 0x48, VK_NUMPAD8},
    {"numpad9", 0x49, VK_NUMPAD9}, {"multiply", 0x37, VK_MULTIPLY}, {"add", 0x4E, VK_ADD},
    {"subtract", 0x4A, VK_SUBTRACT}, {"decimal", 0x53, VK_DECIMAL}, {"divide", E | 0x35, VK_DIVIDE},
    {"numlock", 0x45, VK_NUMLOCK}, {"scroll", 0x46, VK_SCROLL}, {"pause", 0, VK_PAUSE},
    {"prtsc", E | 0x37, VK_SNAPSHOT}, {"snapshot", E | 0x37, VK_SNAPSHOT}, {"apps", E | 0x5D, VK_APPS},
    {"volume_mute", E | 0x20, VK_VOLUME_MUTE}, {"volume_down", E | 0x2E, VK_VOLUME_DOWN},
    {"volume_up", E | 0x30, VK_VOLUME_UP}, {"media_next", E | 0x19, VK_MEDIA_NEXT_TRACK},
    {"media_prev", E | 0x10, VK_MEDIA_PREV_TRACK}, {"media_play", E | 0x22, VK_MEDIA_PLAY_PAUSE},
    {"oem_1", 0x27, VK_OEM_1}, {"oem_plus", 0x0D, VK_OEM_PLUS}, {"oem_comma", 0x33, VK_OEM_COMMA},
    {"oem_minus", 0x0C, VK_OEM_MINUS}, {"oem_period", 0x34, VK_OEM_PERIOD}, {"oem_2", 0x35, VK_OEM_2},
    {"oem_3", 0x29, VK_OEM_3}, {"oem_4", 0x1A, VK_OEM_4}, {"oem_5", 0x2B, VK_OEM_5}, {"oem_6", 0x1B, VK_OEM_6},
    {"oem_7", 0x28, VK_OEM_7},
};

struct Key {
    uint8_t refs;
    uint8_t down;
    uint8_t mods;
    uint8_t dirty;
    uint8_t strikes;
    uint8_t latched;
    uint8_t latchMods;
    std::array<uint32_t, 8> hold;
    int64_t since;
};

using SendFn = UINT(WINAPI*)(UINT, LPINPUT, int);

constexpr uint64_t kWindow = 2000;
constexpr double kRoomNs = 20'000'000.0;
constexpr double kChunkNs = 4'000'000.0;

SRWLOCK g_lock = SRWLOCK_INIT;
std::array<Key, 512> g_keys{};
std::array<uint16_t, 512> g_dirty{};
size_t g_ndirty = 0;
size_t g_mark = 0;
std::vector<INPUT> g_buf;
std::vector<size_t> g_cuts;
std::vector<INPUT> g_forced;
std::vector<INPUT>* g_out = &g_buf;
bool g_was_live = true;
bool g_was_free = true;
int64_t g_stamp = 0;
SendFn g_send = &SendInput;
SHORT(WINAPI* g_keyState)(int) = &GetAsyncKeyState;
std::atomic<uint8_t> g_user{ 0 };
std::atomic<HWND> g_target{ nullptr };
std::atomic<bool> g_guard{ true };
std::atomic<uint64_t> g_sent{ 0 };
std::atomic<uint64_t> g_calls{ 0 };
std::atomic<uint64_t> g_acked{ 0 };
std::atomic<bool> g_watching{ false };
std::atomic<uint64_t> g_strikes{ 0 };
std::atomic<uint64_t> g_merged{ 0 };
std::atomic<uint64_t> g_held{ 0 };
std::atomic<uint64_t> g_lost{ 0 };
std::atomic<uint64_t> g_costNs{ 200'000 };
std::atomic<uint64_t> g_pingUs{ 0 };
std::atomic<int64_t> g_active{ 0 };
std::atomic<uint64_t> g_latN{ 0 }, g_latSum{ 0 }, g_latMax{ 0 };
std::atomic<uint64_t> g_seenN{ 0 }, g_seenSum{ 0 };
std::atomic<bool> g_quit{ false };
HANDLE g_wake = nullptr, g_ack = nullptr, g_room = nullptr, g_ping = nullptr;
std::thread g_thread, g_watch;
int64_t g_freq = 1;

int64_t now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double to_ns(int64_t ticks) {
    return static_cast<double>(ticks) * 1e9 / static_cast<double>(g_freq);
}

void name_thread(const wchar_t* name) {
    using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        if (auto fn = reinterpret_cast<Fn>(GetProcAddress(k, "SetThreadDescription")))
            fn(GetCurrentThread(), name);
}

void put(uint16_t scan, bool up) {
    INPUT& in = g_out->emplace_back();
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan & 0xFF;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | ((scan & E) ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
}

void mods(uint8_t m, bool up) {
    static constexpr uint16_t scans[3] = { 0x38, 0x1D, 0x2A };
    static constexpr uint8_t bits[3] = { Alt, Ctrl, Shift };
    if (!up) {
        for (int i = 0; i < 3; ++i)
            if (m & bits[i])
                put(scans[i], false);
    }
    else {
        for (int i = 2; i >= 0; --i)
            if (m & bits[i])
                put(scans[i], true);
    }
}

void lift(Key& k, uint16_t i) {
    const uint8_t m = static_cast<uint8_t>(k.mods & (Shift | Ctrl));
    mods(m, false);
    put(i, true);
    mods(m, true);
    k.down = 0;
}

void mark(uint16_t i) {
    Key& k = g_keys[i];
    if (!k.dirty) {
        k.dirty = 1;
        k.since = g_stamp;
        g_dirty[g_ndirty++] = i;
    }
}

void cut() {
    if (g_out == &g_buf && !g_buf.empty() && (g_cuts.empty() || g_cuts.back() != g_buf.size()))
        g_cuts.push_back(g_buf.size());
}

template <class Pick, class Act>
void grouped(size_t n, Pick pick, Act act, uint8_t groups) {
    for (uint8_t m = 0; m < groups; ++m) {
        bool open = false;
        for (size_t d = 0; d < n; ++d) {
            const uint16_t i = g_dirty[d];
            Key& k = g_keys[i];
            if (!pick(k, m))
                continue;
            if (!open) {
                mods(m, false);
                open = true;
            }
            act(k, i);
        }
        if (open)
            mods(m, true);
    }
}

void settle(size_t n, bool live) {
    n = std::min(n, g_ndirty);
    if (!n)
        return;
    if (live) {
        grouped(n, [](const Key& k, uint8_t m) { return k.down && k.strikes && (k.mods & 3) == m; },
            [](Key& k, uint16_t i) { put(i, true); k.down = 0; }, 4);
        for (uint8_t m = 0; m < 8; ++m) {
            const uint8_t bit = static_cast<uint8_t>(1u << m);
            bool open = false;
            for (size_t d = 0; d < n; ++d) {
                const uint16_t i = g_dirty[d];
                Key& k = g_keys[i];
                if (k.strikes != bit)
                    continue;
                if (!open) {
                    mods(m, false);
                    open = true;
                }
                put(i, false);
                k.down = 1;
                k.mods = m;
            }
            if (open)
                mods(m, true);
        }
        for (size_t d = 0; d < n; ++d) {
            const uint16_t i = g_dirty[d];
            Key& k = g_keys[i];
            if (!(k.strikes & (k.strikes - 1)))
                continue;
            std::array<uint8_t, 8> order{};
            size_t c = 0;
            for (uint8_t m = 0; m < 8; ++m)
                if (k.strikes & (1u << m))
                    order[c++] = m;
            std::stable_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(c),
                [&k](uint8_t a, uint8_t b) { return k.hold[a] < k.hold[b]; });
            for (size_t j = 0; j < c; ++j) {
                if (k.down)
                    lift(k, i);
                mods(order[j], false);
                put(i, false);
                mods(order[j], true);
                k.down = 1;
                k.mods = order[j];
            }
        }
        grouped(n, [](const Key& k, uint8_t m) { return !k.refs && k.down && (k.mods & 3) == m; },
            [](Key& k, uint16_t i) { put(i, true); k.down = 0; }, 4);
    }
    for (size_t d = 0; d < n; ++d) {
        Key& k = g_keys[g_dirty[d]];
        k.dirty = 0;
        k.strikes = 0;
    }
    std::copy(g_dirty.begin() + static_cast<std::ptrdiff_t>(n), g_dirty.begin() + static_cast<std::ptrdiff_t>(g_ndirty), g_dirty.begin());
    g_ndirty -= n;
    g_mark = g_mark > n ? g_mark - n : 0;
}

void lift_released() {
    grouped(g_ndirty, [](const Key& k, uint8_t m) { return !k.refs && k.down && (k.mods & 3) == m; },
        [](Key& k, uint16_t i) { put(i, true); k.down = 0; }, 4);
}

void drop_all() {
    for (uint16_t i = 0; i < g_keys.size(); ++i)
        if (g_keys[i].down)
            lift(g_keys[i], i);
}

void relatch() {
    for (uint16_t i = 0; i < g_keys.size(); ++i) {
        Key& k = g_keys[i];
        if (k.latched && k.refs && !k.down) {
            mark(i);
            k.strikes |= static_cast<uint8_t>(1u << k.latchMods);
            k.hold[k.latchMods] = 0;
        }
    }
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string_view strip_vk(std::string_view s) {
    if (s.size() > 3 && (s[0] == 'v' || s[0] == 'V') && (s[1] == 'k' || s[1] == 'K') && s[2] == '_')
        s.remove_prefix(3);
    return s;
}

const Named* find_named(std::string_view name) {
    std::string n = lower(strip_vk(name));
    for (const Named& k : kNamed)
        if (k.name == n)
            return &k;
    return nullptr;
}

size_t room() {
    const double cost = std::max(1.0, static_cast<double>(g_costNs.load(std::memory_order_relaxed)));
    return static_cast<size_t>(std::clamp(kRoomNs / cost, 64.0, 2048.0));
}

bool allowed() {
    if (!g_guard.load(std::memory_order_relaxed))
        return true;
    HWND t = g_target.load(std::memory_order_relaxed);
    if (!t)
        return true;
    HWND fg = GetForegroundWindow();
    return fg == t || (fg && GetAncestor(fg, GA_ROOTOWNER) == t);
}

uint8_t user_mods() {
    uint8_t m = 0;
    if (g_keyState(VK_CONTROL) & 0x8000)
        m |= Ctrl;
    if (g_keyState(VK_MENU) & 0x8000)
        m |= Alt;
    if ((g_keyState(VK_LWIN) | g_keyState(VK_RWIN)) & 0x8000)
        m |= 8;
    return m;
}

bool take(std::vector<INPUT>& out, size_t keys, size_t events, int64_t& origin) {
    AcquireSRWLockExclusive(&g_lock);
    out.clear();
    out.insert(out.end(), g_forced.begin(), g_forced.end());
    g_forced.clear();
    g_out = &out;
    g_stamp = 0;
    const bool ok = allowed();
    const uint8_t user = ok ? user_mods() : 0;
    g_user.store(user, std::memory_order_relaxed);
    if (!ok && g_was_live)
        drop_all();
    if (ok && !user && (!g_was_live || !g_was_free))
        relatch();
    g_was_live = ok;
    g_was_free = !user;
    origin = 0;
    if (ok && user) {
        g_buf.clear();
        g_cuts.clear();
        lift_released();
        settle(g_ndirty, false);
    }
    else if (ok) {
        if (!g_buf.empty()) {
            size_t n = g_cuts.empty() ? g_buf.size() : g_cuts.front();
            for (size_t c : g_cuts) {
                if (c > events)
                    break;
                n = c;
            }
            out.insert(out.end(), g_buf.begin(), g_buf.begin() + static_cast<std::ptrdiff_t>(n));
            g_buf.erase(g_buf.begin(), g_buf.begin() + static_cast<std::ptrdiff_t>(n));
            size_t w = 0;
            for (size_t c : g_cuts)
                if (c > n)
                    g_cuts[w++] = c - n;
            g_cuts.resize(w);
        }
        if (g_buf.empty() && out.size() < events && g_ndirty) {
            origin = g_keys[g_dirty[0]].since;
            settle(keys, true);
        }
    }
    else {
        g_buf.clear();
        g_cuts.clear();
        settle(g_ndirty, false);
    }
    g_out = &g_buf;
    const bool more = g_ndirty || !g_buf.empty() || !g_forced.empty();
    const bool roomy = g_buf.size() < room();
    ReleaseSRWLockExclusive(&g_lock);
    if (roomy)
        SetEvent(g_room);
    return more;
}

void send(std::vector<INPUT>& v) {
    const int64_t t0 = now();
    const UINT done = g_send(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
    const int64_t t1 = now();
    if (done < v.size())
        g_lost.fetch_add(v.size() - done, std::memory_order_relaxed);
    g_sent.fetch_add(done, std::memory_order_relaxed);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const double per = std::min(to_ns(t1 - t0) / static_cast<double>(v.size()), 5'000'000.0);
    const double old = static_cast<double>(g_costNs.load(std::memory_order_relaxed));
    g_costNs.store(static_cast<uint64_t>(old + (per - old) / 8.0), std::memory_order_relaxed);
    if (t1 - g_active.exchange(t1, std::memory_order_relaxed) > g_freq)
        SetEvent(g_ping);
}

void pump() {
    std::vector<INPUT> out;
    int64_t origin = 0;
    for (bool more = true; more;) {
        more = take(out, 512, SIZE_MAX, origin);
        if (!out.empty())
            send(out);
    }
}

bool full() {
    return g_watching.load(std::memory_order_relaxed)
        && g_sent.load(std::memory_order_relaxed) - g_acked.load(std::memory_order_relaxed) > kWindow;
}

void run() {
    name_thread(L"MIDI++ keys");
    DWORD task = 0;
    HANDLE av = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    if (av)
        AvSetMmThreadPriority(av, AVRT_PRIORITY_HIGH);
    else
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    std::vector<INPUT> out;
    out.reserve(4096);
    bool more = false, holding = false;
    for (;;) {
        if (!more)
            WaitForSingleObject(g_wake, INFINITE);
        const bool quitting = g_quit.load(std::memory_order_acquire);
        if (!quitting && full()) {
            if (!holding)
                g_held.fetch_add(1, std::memory_order_relaxed);
            holding = true;
            g_active.store(now(), std::memory_order_relaxed);
            WaitForSingleObject(g_ack, 5);
            more = true;
            continue;
        }
        holding = false;
        const double cost = std::max(1.0, static_cast<double>(g_costNs.load(std::memory_order_relaxed)));
        const size_t events = static_cast<size_t>(std::clamp(kChunkNs / cost, 12.0, 1024.0));
        int64_t origin = 0;
        more = take(out, std::max<size_t>(4, events / 3), events, origin);
        if (!out.empty()) {
            send(out);
            if (origin) {
                const uint64_t us = static_cast<uint64_t>(std::max(0.0, to_ns(now() - origin) / 1000.0));
                g_latN.fetch_add(1, std::memory_order_relaxed);
                g_latSum.fetch_add(us, std::memory_order_relaxed);
                uint64_t best = g_latMax.load(std::memory_order_relaxed);
                while (us > best && !g_latMax.compare_exchange_weak(best, us, std::memory_order_relaxed)) {
                }
            }
        }
        if (quitting && !more)
            break;
    }
    if (av)
        AvRevertMmThreadCharacteristics(av);
}

HWND watched() {
    HWND t = g_guard.load(std::memory_order_relaxed) ? g_target.load(std::memory_order_relaxed) : nullptr;
    if (!t)
        t = GetForegroundWindow();
    if (!t || !IsWindow(t))
        return nullptr;
    GUITHREADINFO gi{ sizeof(gi) };
    if (GetGUIThreadInfo(GetWindowThreadProcessId(t, nullptr), &gi) && gi.hwndFocus)
        return gi.hwndFocus;
    return t;
}

void watch() {
    name_thread(L"MIDI++ game watch");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HWND blind = nullptr;
    while (!g_quit.load(std::memory_order_acquire)) {
        HWND t = watched();
        if (!t || t == blind || now() - g_active.load(std::memory_order_relaxed) > 2 * g_freq) {
            g_watching.store(false, std::memory_order_relaxed);
            g_acked.store(g_sent.load(std::memory_order_relaxed), std::memory_order_relaxed);
            SetEvent(g_ack);
            WaitForSingleObject(g_ping, 250);
            continue;
        }
        g_watching.store(true, std::memory_order_relaxed);
        const uint64_t s = g_sent.load(std::memory_order_relaxed);
        const int64_t t0 = now();
        DWORD_PTR r = 0;
        if (SendMessageTimeoutW(t, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 250, &r)) {
            uint64_t a = g_acked.load(std::memory_order_relaxed);
            while (s > a && !g_acked.compare_exchange_weak(a, s, std::memory_order_relaxed)) {
            }
            SetEvent(g_ack);
            g_pingUs.store(static_cast<uint64_t>(to_ns(now() - t0) / 1000.0), std::memory_order_relaxed);
        }
        else if (GetLastError() != ERROR_TIMEOUT) {
            blind = t;
            continue;
        }
        else {
            g_pingUs.store(static_cast<uint64_t>(to_ns(now() - t0) / 1000.0), std::memory_order_relaxed);
        }
        WaitForSingleObject(g_ping, 4);
    }
    g_watching.store(false, std::memory_order_relaxed);
}

}

std::optional<Stroke> parse_key(std::string_view spec) {
    uint8_t mods = 0;
    for (;;) {
        std::string l = lower(spec.substr(0, 8));
        if (l.starts_with("ctrl+") && spec.size() > 5) { mods |= Ctrl; spec.remove_prefix(5); }
        else if (l.starts_with("control+") && spec.size() > 8) { mods |= Ctrl; spec.remove_prefix(8); }
        else if (l.starts_with("alt+") && spec.size() > 4) { mods |= Alt; spec.remove_prefix(4); }
        else if (l.starts_with("shift+") && spec.size() > 6) { mods |= Shift; spec.remove_prefix(6); }
        else break;
    }
    if (spec.empty())
        return std::nullopt;
    if (spec.size() == 1) {
        unsigned char c = static_cast<unsigned char>(spec[0]);
        if (c >= 128 || !kChars[c].scan)
            return std::nullopt;
        return Stroke{ kChars[c].scan, static_cast<uint8_t>(mods | (kChars[c].shift ? Shift : 0)) };
    }
    const Named* k = find_named(spec);
    if (!k || !k->scan)
        return std::nullopt;
    return Stroke{ k->scan, mods };
}

std::optional<Stroke> parse_typed(std::string_view spec, HKL layout) {
    uint8_t mods = 0;
    std::string_view rest = spec;
    for (;;) {
        std::string l = lower(rest.substr(0, 8));
        if (l.starts_with("ctrl+") && rest.size() > 5) { mods |= Ctrl; rest.remove_prefix(5); }
        else if (l.starts_with("control+") && rest.size() > 8) { mods |= Ctrl; rest.remove_prefix(8); }
        else if (l.starts_with("alt+") && rest.size() > 4) { mods |= Alt; rest.remove_prefix(4); }
        else if (l.starts_with("shift+") && rest.size() > 6) { mods |= Shift; rest.remove_prefix(6); }
        else break;
    }
    wchar_t wc[4] = {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rest.data(), static_cast<int>(rest.size()), wc, 4);
    if (n != 1)
        return parse_key(spec);
    SHORT r = VkKeyScanExW(wc[0], layout);
    if (r == -1)
        return parse_key(spec);
    const UINT vk = LOBYTE(r);
    const UINT state = HIBYTE(r);
    if (state & 1) mods |= Shift;
    if (state & 2) mods |= Ctrl;
    if (state & 4) mods |= Alt;
    const UINT sc = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout);
    if (!(sc & 0xFF))
        return parse_key(spec);
    return Stroke{ static_cast<uint16_t>((sc & 0xFF) | ((sc & 0xFF00) == 0xE000 ? E : 0)), mods };
}

Stroke stroke_for_vk(int vk) {
    for (const Named& k : kNamed)
        if (k.vk == vk && k.scan)
            return { k.scan, 0 };
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        char c = static_cast<char>(vk >= 'A' ? vk + 32 : vk);
        return { kChars[static_cast<uint8_t>(c)].scan, 0 };
    }
    UINT sc = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
    return { static_cast<uint16_t>((sc & 0xFF) | ((sc & 0xFF00) == 0xE000 ? E : 0)), 0 };
}

int parse_vk(std::string_view name) {
    std::string_view s = strip_vk(name);
    if (s.size() == 1) {
        unsigned char c = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(s[0])));
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
            return c;
    }
    if (const Named* k = find_named(s))
        return k->vk;
    std::string l = lower(s);
    if (l.size() > 1 && l[0] == 'f' && l.find_first_not_of("0123456789", 1) == std::string::npos) {
        int n = std::atoi(l.c_str() + 1);
        if (n >= 1 && n <= 24)
            return VK_F1 + n - 1;
    }
    if (!l.empty() && l.find_first_not_of("0123456789abcdefx") == std::string::npos) {
        int v = static_cast<int>(std::strtol(l.c_str(), nullptr, 0));
        if (v > 0 && v < 256)
            return v;
    }
    return 0;
}

std::string key_label(std::string_view name) {
    std::string r(strip_vk(name));
    for (char& c : r)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

void init() {
    if (HMODULE m = GetModuleHandleW(L"win32u.dll"))
        if (FARPROC p = GetProcAddress(m, "NtUserSendInput"))
            g_send = reinterpret_cast<SendFn>(p);
    if (g_thread.joinable())
        return;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ack = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_room = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ping = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_buf.reserve(16384);
    g_forced.reserve(1024);
    g_quit.store(false);
    g_thread = std::thread(run);
    g_watch = std::thread(watch);
}

void shutdown() {
    if (!g_thread.joinable())
        return;
    g_quit.store(true, std::memory_order_release);
    for (HANDLE h : { g_wake, g_ack, g_room, g_ping })
        SetEvent(h);
    g_thread.join();
    if (g_watch.joinable())
        g_watch.join();
}

void set_sender(UINT(WINAPI* fn)(UINT, LPINPUT, int)) { g_send = fn ? fn : &SendInput; }
void set_key_state(SHORT(WINAPI* fn)(int)) { g_keyState = fn ? fn : &GetAsyncKeyState; }

void set_guard(bool on) { g_guard.store(on); }
bool guard() { return g_guard.load(); }
void set_target(HWND hwnd) { g_target.store(hwnd); }
HWND target() { return g_target.load(); }
bool target_in_front() { return allowed(); }
uint64_t sent_events() { return g_sent.load(std::memory_order_relaxed); }
uint64_t send_calls() { return g_calls.load(std::memory_order_relaxed); }

Health health() {
    Health h;
    h.costUs = g_calls.load(std::memory_order_relaxed) ? static_cast<double>(g_costNs.load(std::memory_order_relaxed)) / 1000.0 : 0.0;
    h.pingMs = static_cast<double>(g_pingUs.load(std::memory_order_relaxed)) / 1000.0;
    h.strikes = g_strikes.load(std::memory_order_relaxed);
    h.merged = g_merged.load(std::memory_order_relaxed);
    h.held = g_held.load(std::memory_order_relaxed);
    h.lost = g_lost.load(std::memory_order_relaxed);
    h.user = g_user.load(std::memory_order_relaxed);
    return h;
}

Delivery delivery() {
    const uint64_t n = g_latN.load(std::memory_order_relaxed);
    const uint64_t sum = g_latSum.load(std::memory_order_relaxed);
    const uint64_t mx = g_latMax.exchange(0, std::memory_order_relaxed);
    const uint64_t dn = n - g_seenN.exchange(n, std::memory_order_relaxed);
    const uint64_t ds = sum - g_seenSum.exchange(sum, std::memory_order_relaxed);
    Delivery d;
    d.count = dn;
    d.avgUs = dn ? static_cast<double>(ds) / static_cast<double>(dn) : 0.0;
    d.maxUs = dn ? static_cast<double>(mx) : 0.0;
    return d;
}

void focus_lost() {
    Batch b(true);
    b.drop();
}

void refresh() {
    if (g_thread.joinable())
        SetEvent(g_wake);
    else
        pump();
}

void toggle_numlock() {
    Batch b(true);
    b.flush();
    for (int i = 0; i < 2; ++i) {
        INPUT& in = g_forced.emplace_back();
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = VK_NUMLOCK;
        in.ki.wScan = 0x45;
        in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (i ? KEYEVENTF_KEYUP : 0);
    }
}

bool wait_room(DWORD ms) {
    AcquireSRWLockExclusive(&g_lock);
    const bool ok = g_buf.size() < room();
    ReleaseSRWLockExclusive(&g_lock);
    if (ok || g_quit.load(std::memory_order_relaxed) || !g_room)
        return true;
    WaitForSingleObject(g_room, ms);
    return false;
}

Batch::Batch(bool force) : force_(force) {
    AcquireSRWLockExclusive(&g_lock);
    g_mark = g_ndirty;
    g_stamp = 0;
    if (force) {
        g_out = &g_forced;
        live_ = true;
        return;
    }
    live_ = allowed();
}

Batch::~Batch() {
    bool kick;
    if (force_) {
        lift_released();
        g_out = &g_buf;
        kick = !g_forced.empty() || g_ndirty;
    }
    else {
        kick = g_ndirty || !g_buf.empty();
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!kick)
        return;
    if (g_thread.joinable())
        SetEvent(g_wake);
    else
        pump();
}

void Batch::origin(int64_t qpc) {
    g_stamp = qpc;
}

void Batch::press(Stroke s, uint32_t hold) {
    const uint16_t i = s.scan & 0x1FF;
    Key& k = g_keys[i];
    mark(i);
    if (k.refs < 255)
        ++k.refs;
    const uint8_t m = static_cast<uint8_t>(s.mods & 7);
    const uint8_t bit = static_cast<uint8_t>(1u << m);
    g_strikes.fetch_add(1, std::memory_order_relaxed);
    if (k.strikes & bit)
        g_merged.fetch_add(1, std::memory_order_relaxed);
    k.hold[m] = (k.strikes & bit) ? std::max(k.hold[m], hold) : hold;
    k.strikes |= bit;
}

void Batch::latch(Stroke s, bool down) {
    Key& k = g_keys[s.scan & 0x1FF];
    if (down) {
        press(s);
        k.latched = 1;
        k.latchMods = static_cast<uint8_t>(s.mods & 7);
        return;
    }
    release(s);
    if (!k.refs)
        k.latched = 0;
}

void Batch::release(Stroke s) {
    const uint16_t i = s.scan & 0x1FF;
    Key& k = g_keys[i];
    if (!k.refs)
        return;
    mark(i);
    --k.refs;
}

void Batch::tap(Stroke s) {
    if (!live_)
        return;
    if (!force_ && g_mark) {
        settle(g_mark, true);
        cut();
    }
    const uint16_t i = s.scan & 0x1FF;
    if (g_keys[i].down)
        lift(g_keys[i], i);
    mods(s.mods, false);
    put(s.scan, false);
    put(s.scan, true);
    mods(s.mods, true);
    cut();
}

void Batch::tap_scan(uint16_t scan) {
    if (!live_)
        return;
    if (!force_ && g_mark) {
        settle(g_mark, true);
        cut();
    }
    const uint16_t i = scan & 0x1FF;
    if (g_keys[i].down)
        lift(g_keys[i], i);
    put(scan, false);
    put(scan, true);
    cut();
}

void Batch::release_all() {
    for (size_t d = 0; d < g_ndirty; ++d)
        g_keys[g_dirty[d]].strikes = 0;
    for (uint16_t i = 0; i < g_keys.size(); ++i) {
        Key& k = g_keys[i];
        if (!k.refs && !k.down)
            continue;
        mark(i);
        k.refs = 0;
        k.strikes = 0;
        k.latched = 0;
    }
}

void Batch::drop() {
    drop_all();
    cut();
}

void Batch::flush() {
    if (force_)
        lift_released();
}

}
