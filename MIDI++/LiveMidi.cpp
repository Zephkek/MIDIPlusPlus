#include "LiveMidi.hpp"
#include "KSMidi.h"
#include <mmsystem.h>
#include <avrt.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <memory>
#include <mutex>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "avrt.lib")

namespace live {
namespace {

HMIDIIN g_in = nullptr;
std::unique_ptr<ksmidi::MidiIn> g_ks;
const wchar_t* g_path = L"";
out::Mode g_mode = out::Mode::Keys;
std::atomic<bool> g_active{ false };
std::atomic<int> g_channel{ -1 };
std::atomic<int> g_key{ 0 };
std::atomic<uint32_t> g_enabled{ 0xFFFF };
std::array<std::atomic<uint64_t>, 2> g_mask{};
std::array<uint8_t, 128> g_count{};
std::array<std::array<uint8_t, 128>, 16> g_sent{};
std::array<std::atomic<uint32_t>, 128> g_hits{};
std::array<out::Activity, 16> g_channels{};
std::array<std::array<uint8_t, 128>, 16> g_chanCount{};
std::atomic<uint64_t> g_messages{ 0 };
std::atomic<uint64_t> g_sumTicks{ 0 };
std::atomic<uint64_t> g_maxTicks{ 0 };
std::atomic<uint64_t> g_seenMessages{ 0 };
std::atomic<uint64_t> g_seenTicks{ 0 };
double g_tick = 0.0;
uint8_t g_status = 0;
uint8_t g_data[2] = {};
int g_have = 0;
thread_local bool t_boosted = false;

int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

std::wstring widen(const std::string& s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring folded(std::wstring s) {
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

void clear_state() {
    g_count.fill(0);
    for (auto& c : g_sent)
        c.fill(0);
    for (auto& c : g_chanCount)
        c.fill(0);
    g_mask[0].store(0);
    g_mask[1].store(0);
    for (auto& c : g_channels) {
        c.mask[0].store(0, std::memory_order_relaxed);
        c.mask[1].store(0, std::memory_order_relaxed);
    }
}

void note_off(input::Batch& b, out::Sink& s, int ch, int note);

void note_on(input::Batch& b, out::Sink& s, int ch, int note, int vel) {
    if (!(g_enabled.load(std::memory_order_relaxed) & (1u << ch)))
        return;
    const int n = note + g_key.load(std::memory_order_relaxed);
    if (n < 0 || n > 127)
        return;
    if (g_sent[ch][note])
        note_off(b, s, ch, note);
    g_sent[ch][note] = static_cast<uint8_t>(n + 1);
    s.on(b, ch, n, vel);
    g_hits[n].store((((g_hits[n].load(std::memory_order_relaxed) >> 8) + 1) << 8) | static_cast<uint32_t>(vel), std::memory_order_relaxed);
    if (g_count[n]++ == 0)
        g_mask[n >> 6].fetch_or(1ull << (n & 63), std::memory_order_relaxed);
    out::Activity& a = g_channels[ch];
    a.hit.store((((a.hit.load(std::memory_order_relaxed) >> 8) + 1) << 8) | static_cast<uint32_t>(vel), std::memory_order_relaxed);
    if (g_chanCount[ch][n]++ == 0)
        a.mask[n >> 6].fetch_or(1ull << (n & 63), std::memory_order_relaxed);
}

void note_off(input::Batch& b, out::Sink& s, int ch, int note) {
    const int sent = g_sent[ch][note];
    if (!sent)
        return;
    g_sent[ch][note] = 0;
    const int n = sent - 1;
    s.off(b, ch, n);
    if (g_count[n] && --g_count[n] == 0)
        g_mask[n >> 6].fetch_and(~(1ull << (n & 63)), std::memory_order_relaxed);
    if (g_chanCount[ch][n] && --g_chanCount[ch][n] == 0)
        g_channels[ch].mask[n >> 6].fetch_and(~(1ull << (n & 63)), std::memory_order_relaxed);
}

void message(uint8_t status, uint8_t d1, uint8_t d2, int64_t t0) {
    const uint8_t type = status & 0xF0;
    const int ch = status & 0x0F;
    if (type != 0x80 && type != 0x90 && type != 0xB0)
        return;
    const int want = g_channel.load(std::memory_order_relaxed);
    if (want >= 0 && ch != want)
        return;
    {
        input::Batch b;
        if (!g_active.load(std::memory_order_relaxed))
            return;
        out::Sink& s = out::sink(g_mode);
        if (type == 0x90 && d2) {
            note_on(b, s, ch, d1, d2);
        }
        else if (type != 0xB0) {
            note_off(b, s, ch, d1);
        }
        else if (d1 == 64) {
            if (g_enabled.load(std::memory_order_relaxed) & (1u << ch))
                s.pedal(b, d2);
        }
        else if (d1 == 120 || d1 == 123) {
            s.reset(b);
            clear_state();
        }
        s.commit(b);
    }
    const uint64_t ticks = static_cast<uint64_t>(qpc() - t0);
    g_messages.fetch_add(1, std::memory_order_relaxed);
    g_sumTicks.fetch_add(ticks, std::memory_order_relaxed);
    uint64_t best = g_maxTicks.load(std::memory_order_relaxed);
    while (ticks > best && !g_maxTicks.compare_exchange_weak(best, ticks, std::memory_order_relaxed)) {
    }
}

void boost() {
    if (t_boosted)
        return;
    t_boosted = true;
    DWORD task = 0;
    if (HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task))
        AvSetMmThreadPriority(h, AVRT_PRIORITY_HIGH);
}

void ks_input(const BYTE* data, size_t size, double, void*) {
    feed(data, size);
}

void CALLBACK mm_input(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR p1, DWORD_PTR) {
    const int64_t t0 = qpc();
    if (msg != MIM_DATA || !g_active.load(std::memory_order_acquire))
        return;
    boost();
    message(static_cast<uint8_t>(p1 & 0xFF), static_cast<uint8_t>((p1 >> 8) & 0x7F), static_cast<uint8_t>((p1 >> 16) & 0x7F), t0);
}

std::wstring error_text(MMRESULT r) {
    wchar_t text[MAXERRORLENGTH] = {};
    if (midiInGetErrorTextW(r, text, MAXERRORLENGTH) != MMSYSERR_NOERROR)
        return L"error " + std::to_wstring(r);
    return text;
}

ksmidi::MidiIn::WaitMode wait_mode() {
    switch (midi::Config::getInstance().midi.liveWait) {
    case midi::LiveWait::Spin: return ksmidi::MidiIn::WaitMode::Spin;
    case midi::LiveWait::SpinThenBlock: return ksmidi::MidiIn::WaitMode::SpinThenBlock;
    default: return ksmidi::MidiIn::WaitMode::Block;
    }
}

}

std::vector<Device> devices() {
    std::vector<Device> list;
    std::vector<std::wstring> taken;
    if (midi::Config::getInstance().midi.kernelStreaming) {
        try {
            for (const ksmidi::DeviceInfo& p : ksmidi::Api::getPortsIn()) {
                std::wstring name = widen(p.windowsName.empty() ? p.name : p.windowsName);
                list.push_back({ p.id, name, true });
                taken.push_back(folded(name));
                taken.push_back(folded(widen(p.name)));
            }
        }
        catch (...) {
        }
    }
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;
        const std::wstring name = folded(caps.szPname);
        const bool dup = std::any_of(taken.begin(), taken.end(), [&](const std::wstring& t) {
            return !t.empty() && (t == name || (name.size() >= MAXPNAMELEN - 1 && t.compare(0, name.size(), name) == 0));
        });
        if (!dup)
            list.push_back({ i, caps.szPname, false });
    }
    return list;
}

void attach(out::Mode mode) {
    if (g_tick == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_tick = static_cast<double>(f.QuadPart) / 1e6;
    }
    g_mode = mode;
    clear_state();
    g_status = 0;
    g_have = 0;
    g_active.store(true, std::memory_order_release);
}

void feed(const uint8_t* data, size_t size) {
    const int64_t t0 = qpc();
    if (!g_active.load(std::memory_order_acquire))
        return;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t b = data[i];
        if (b >= 0xF8)
            continue;
        if (b & 0x80) {
            g_status = b < 0xF0 ? b : 0;
            g_have = 0;
            continue;
        }
        if (!g_status)
            continue;
        g_data[g_have++] = b;
        const uint8_t type = g_status & 0xF0;
        if (g_have < (type == 0xC0 || type == 0xD0 ? 1 : 2))
            continue;
        g_have = 0;
        message(g_status, g_data[0], g_data[1], t0);
    }
}

bool open(const Device& device, out::Mode mode, std::wstring& error) {
    close();
    attach(mode);
    if (device.ks) {
        try {
            auto in = std::make_unique<ksmidi::MidiIn>();
            in->setDirectCallback(&ks_input, nullptr);
            ksmidi::MidiIn::Settings s;
            s.transport = ksmidi::Transport::Auto;
            s.timestampMode = ksmidi::MidiIn::TimestampMode::None;
            s.waitMode = wait_mode();
            s.preferPerformanceCores = true;
            s.disablePowerThrottling = true;
            in->openPort(device.id, s);
            g_path = in->activeTransport() == ksmidi::Transport::Direct ? L"kernel streaming" : L"Windows MIDI";
            g_ks = std::move(in);
            return true;
        }
        catch (const std::exception& e) {
            error = widen(e.what());
            g_active.store(false, std::memory_order_release);
            return false;
        }
    }
    HMIDIIN h = nullptr;
    MMRESULT r = midiInOpen(&h, device.id, reinterpret_cast<DWORD_PTR>(&mm_input), 0, CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR) {
        error = error_text(r);
        g_active.store(false, std::memory_order_release);
        return false;
    }
    g_in = h;
    g_path = L"WinMM";
    r = midiInStart(h);
    if (r != MMSYSERR_NOERROR) {
        error = error_text(r);
        close();
        return false;
    }
    return true;
}

void close() {
    if (!g_in && !g_ks) {
        g_active.store(false, std::memory_order_release);
        return;
    }
    g_active.store(false, std::memory_order_release);
    if (g_ks) {
        try {
            g_ks->cancelDirectCallback();
            g_ks->closePort();
        }
        catch (...) {
        }
        g_ks.reset();
    }
    if (g_in) {
        midiInStop(g_in);
        midiInReset(g_in);
        midiInClose(g_in);
        g_in = nullptr;
    }
    input::Batch b(true);
    out::sink(g_mode).reset(b);
    clear_state();
}

bool active() {
    return g_in != nullptr || g_ks != nullptr;
}

out::Mode mode() {
    return g_mode;
}

const wchar_t* path() {
    return g_path;
}

void set_channel(int channel) {
    g_channel.store(channel, std::memory_order_relaxed);
}

void set_key(int semitones) {
    g_key.store(std::clamp(semitones, -48, 48), std::memory_order_relaxed);
}

void set_channel_enabled(int channel, bool on) {
    if (channel < 0 || channel > 15)
        return;
    if (on)
        g_enabled.fetch_or(1u << channel, std::memory_order_relaxed);
    else
        g_enabled.fetch_and(~(1u << channel), std::memory_order_relaxed);
}

std::array<uint64_t, 2> held() {
    return { g_mask[0].load(std::memory_order_relaxed), g_mask[1].load(std::memory_order_relaxed) };
}

std::array<uint32_t, 128> hits() {
    std::array<uint32_t, 128> h;
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = g_hits[i].load(std::memory_order_relaxed);
    return h;
}

const out::Activity* channels() {
    return g_channels.data();
}

Latency latency() {
    const uint64_t n = g_messages.load(std::memory_order_relaxed);
    const uint64_t sum = g_sumTicks.load(std::memory_order_relaxed);
    const uint64_t mx = g_maxTicks.exchange(0, std::memory_order_relaxed);
    Latency l;
    const uint64_t dn = n - g_seenMessages.exchange(n, std::memory_order_relaxed);
    const uint64_t ds = sum - g_seenTicks.exchange(sum, std::memory_order_relaxed);
    l.messages = dn;
    l.avgUs = dn && g_tick > 0.0 ? static_cast<double>(ds) / static_cast<double>(dn) / g_tick : 0.0;
    l.maxUs = g_tick > 0.0 ? static_cast<double>(mx) / g_tick : 0.0;
    return l;
}

}
