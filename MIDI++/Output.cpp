#include "Output.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace out {
namespace {

constexpr std::array<std::array<int, 32>, 5> kCurves = { {
    {4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,127},
    {2,6,10,14,18,22,26,30,34,38,42,46,50,54,58,62,66,70,74,78,82,86,90,94,98,102,106,110,114,118,122,127},
    {1,3,5,7,10,13,16,20,24,29,34,40,46,53,60,68,76,85,94,104,114,120,123,127,127,127,127,127,127,127,127,127},
    {1,2,3,5,7,10,14,19,25,32,40,49,60,72,85,99,115,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127},
    {1,2,4,8,16,24,32,40,48,56,64,72,80,88,96,104,110,115,120,124,126,127,127,127,127,127,127,127,127,127,127,127}
} };

constexpr char kVelocityKeys[] = "1234567890qwertyuiopasdfghjklzxc";
constexpr uint16_t kDigits[12] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x4A, 0x4E };

using Map = std::array<input::Stroke, 128>;
using Fold = std::array<int8_t, 128>;

using Labels = std::array<std::wstring, 128>;

std::wstring short_label(const std::string& spec) {
    std::string l = spec;
    for (char& c : l)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string s = spec;
    if (l.starts_with("ctrl+") && spec.size() == 6)
        s = "^" + spec.substr(5);
    else if (s.size() > 4)
        s = s.substr(0, 4);
    return std::wstring(s.begin(), s.end());
}

HKL g_layout = nullptr;
bool g_typed = false;

void build(Map& map, Fold& fold, Labels& labels, const std::map<std::string, std::string>* keys) {
    map = {};
    labels = {};
    if (keys)
        for (const auto& [note, key] : *keys) {
            int n = midi::noteNameToMidi(note);
            if (n < 0)
                continue;
            if (auto s = g_typed ? input::parse_typed(key, g_layout ? g_layout : GetKeyboardLayout(0)) : input::parse_key(key)) {
                map[n] = *s;
                labels[n] = short_label(key);
            }
        }
    for (int n = 0; n < 128; ++n) {
        fold[n] = -1;
        if (map[n])
            continue;
        for (int k = 12; k < 128 && fold[n] < 0; k += 12) {
            if (n + k < 128 && map[n + k]) fold[n] = static_cast<int8_t>(n + k);
            else if (n - k >= 0 && map[n - k]) fold[n] = static_cast<int8_t>(n - k);
        }
    }
}

class Keys final : public Sink {
public:
    void configure() {
        const auto& c = midi::Config::getInstance();
        auto find = [&](const char* mode) -> const std::map<std::string, std::string>* {
            auto it = c.key_mappings.find(mode);
            return it == c.key_mappings.end() ? nullptr : &it->second;
        };
        g_typed = c.playback.typedKeys;
        build(m61_, f61_, l61_, find("LIMITED"));
        build(m88_, f88_, l88_, find("FULL"));

        int ci = c.playback.velocityCurveIndex;
        const std::array<int, 32>* table = &kCurves[0];
        if (ci >= 0 && ci < 5)
            table = &kCurves[ci];
        else if (ci >= 5 && ci - 5 < static_cast<int>(c.playback.customVelocityCurves.size()))
            table = &c.playback.customVelocityCurves[ci - 5].velocityValues;
        for (int v = 0; v < 128; ++v) {
            int i = 0;
            while (i < 32 && (*table)[i] < v)
                ++i;
            level_[v] = static_cast<uint8_t>(std::min(i, 31));
        }
        for (int i = 0; i < 32; ++i)
            velKey_[i] = input::parse_key(std::string("alt+") + kVelocityKeys[i]).value_or(input::Stroke{});

        sustain_ = input::stroke_for_vk(input::parse_vk(c.hotkeys.SUSTAIN_KEY));
        volUp_ = input::stroke_for_vk(input::parse_vk(c.hotkeys.VOLUME_UP_KEY));
        volDown_ = input::stroke_for_vk(input::parse_vk(c.hotkeys.VOLUME_DOWN_KEY));

        int mx = c.volume.MAX_VOLUME, mn = c.volume.MIN_VOLUME;
        for (int v = 0; v < 128; ++v) {
            int tv = static_cast<int>(v / 127.0 * mx);
            tv = std::clamp(tv, std::min(mn, mx), std::max(mn, mx));
            volFor_[v] = static_cast<uint8_t>(((tv + 5) / 10) * 10);
        }
        step_ = std::max(1, c.volume.VOLUME_STEP);
        initial_ = c.volume.INITIAL_VOLUME;
        min_ = mn;

        keys88_ = c.playback.keys88;
        fold_ = c.playback.outOfRange;
        velocity_ = c.playback.velocityKeys;
        autovol_ = c.playback.autoVolume;
        sustainMode_ = c.playback.sustain;
        cutoff_ = c.playback.sustainCutoff;
    }

    void on(input::Batch& b, int ch, int note, int vel, uint32_t hold) override {
        input::Stroke s = stroke(note);
        if (!s)
            return;
        batchVel_ = std::max(batchVel_, vel & 127);
        uint8_t& h = held_[ch & 15][note & 127];
        if (h < 255)
            ++h;
        b.press(s, hold);
    }

    void restart(input::Batch& b) override {
        if (autovol_)
            calibrate(b);
    }

    void commit(input::Batch& b) override {
        if (!batchVel_)
            return;
        if (!b.live()) {
            batchVel_ = 0;
            return;
        }
        if (velocity_) {
            int lv = level_[batchVel_];
            if (lv != lastLevel_ && velKey_[lv]) {
                b.tap(velKey_[lv]);
                lastLevel_ = lv;
            }
        }
        if (autovol_)
            volume(b, volFor_[batchVel_]);
        batchVel_ = 0;
    }

    void off(input::Batch& b, int ch, int note) override {
        uint8_t& h = held_[ch & 15][note & 127];
        if (!h)
            return;
        --h;
        if (input::Stroke s = stroke(note))
            b.release(s);
    }

    void pedal(input::Batch& b, int value) override {
        if (sustainMode_ == midi::SustainMode::Ignore || !sustain_)
            return;
        bool down = value >= cutoff_;
        bool want = sustainMode_ == midi::SustainMode::Down ? down : !down;
        if (want == pedalDown_)
            return;
        b.latch(sustain_, want);
        pedalDown_ = want;
    }

    void reset(input::Batch& b) override {
        b.release_all();
        held_ = {};
        pedalDown_ = false;
        lastLevel_ = -1;
        batchVel_ = 0;
    }

    Labels labels() const {
        const Labels& base = keys88_ ? l88_ : l61_;
        if (!fold_)
            return base;
        const Fold& f = keys88_ ? f88_ : f61_;
        Labels l = base;
        for (int n = 0; n < 128; ++n)
            if (l[n].empty() && f[n] >= 0)
                l[n] = base[f[n]];
        return l;
    }

    void calibrate(input::Batch& b) {
        if (!volUp_ || !volDown_ || !b.live())
            return;
        for (int i = 0; i < 50; ++i)
            b.tap(volDown_);
        double cf = initial_ > 150 ? 1.1 : 1.0;
        int steps = static_cast<int>(std::ceil(((initial_ - min_) / step_) * cf));
        for (int i = 0; i < steps; ++i)
            b.tap(volUp_);
        volume_ = initial_;
    }

private:
    input::Stroke stroke(int note) const {
        if (note < 0 || note > 127)
            return {};
        const Map& m = keys88_ ? m88_ : m61_;
        if (m[note] || !fold_)
            return m[note];
        int f = (keys88_ ? f88_ : f61_)[note];
        return f >= 0 ? m[f] : input::Stroke{};
    }

    void volume(input::Batch& b, int target) {
        int diff = target - volume_;
        if (std::abs(diff) < step_ || !volUp_ || !volDown_)
            return;
        for (int i = 0, n = std::abs(diff) / step_; i < n; ++i)
            b.tap(diff > 0 ? volUp_ : volDown_);
        volume_ = target;
    }

    Map m61_{}, m88_{};
    Fold f61_{}, f88_{};
    Labels l61_{}, l88_{};
    std::array<uint8_t, 128> level_{};
    std::array<input::Stroke, 32> velKey_{};
    std::array<uint8_t, 128> volFor_{};
    std::array<std::array<uint8_t, 128>, 16> held_{};
    input::Stroke sustain_{}, volUp_{}, volDown_{};
    bool keys88_ = false, fold_ = false, velocity_ = false, autovol_ = false, pedalDown_ = false;
    midi::SustainMode sustainMode_ = midi::SustainMode::Ignore;
    int cutoff_ = 64, lastLevel_ = -1, volume_ = 100, initial_ = 100, min_ = 10, step_ = 10, batchVel_ = 0;
};

class MidiConnect final : public Sink {
public:
    void on(input::Batch& b, int, int note, int vel, uint32_t) override {
        if (note < 0 || note > 127)
            return;
        if (batchOns_ >= kMaxOns) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ++batchOns_;
        if (held_[note] < 255)
            ++held_[note];
        vel = std::clamp(vel, 0, 127);
        frame(b, note / 12, note % 12, vel / 12, vel % 12);
    }

    void off(input::Batch& b, int, int note) override {
        if (note < 0 || note > 127 || !held_[note])
            return;
        --held_[note];
        frame(b, note / 12, note % 12, 0, 0);
    }

    void pedal(input::Batch& b, int value) override {
        value = std::clamp(value, 0, 127);
        frame(b, 11, 11, value / 12, value % 12);
        pedalOn_ = value >= 64;
    }

    void reset(input::Batch& b) override {
        for (int n = 0; n < 128; ++n)
            if (held_[n]) {
                frame(b, n / 12, n % 12, 0, 0);
                held_[n] = 0;
            }
        if (pedalOn_)
            frame(b, 11, 11, 0, 0);
        pedalOn_ = false;
    }

    void commit(input::Batch&) override {
        batchOns_ = 0;
    }

    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    void begin() override {
        if (users_++)
            return;
        forced_ = !(GetKeyState(VK_NUMLOCK) & 1);
        if (forced_)
            input::toggle_numlock();
    }

    void end() override {
        if (!users_ || --users_)
            return;
        if (forced_ && (GetKeyState(VK_NUMLOCK) & 1))
            input::toggle_numlock();
        forced_ = false;
    }

private:
    static void frame(input::Batch& b, int a, int c, int d, int e) {
        b.tap_scan(0x37);
        b.tap_scan(kDigits[a]);
        b.tap_scan(kDigits[c]);
        b.tap_scan(kDigits[d]);
        b.tap_scan(kDigits[e]);
    }

    static constexpr int kMaxOns = 48;
    std::array<uint8_t, 128> held_{};
    bool pedalOn_ = false, forced_ = false;
    int users_ = 0, batchOns_ = 0;
    std::atomic<uint64_t> dropped_{ 0 };
};

Keys g_keys;
MidiConnect g_connect;

}

Sink& sink(Mode m) {
    if (m == Mode::MidiConnect)
        return g_connect;
    return g_keys;
}

void reload(bool reset) {
    input::Batch b(true);
    if (reset) {
        g_keys.reset(b);
        g_connect.reset(b);
    }
    g_keys.configure();
}

void set_layout(HKL layout) {
    input::Batch b(true);
    if (layout == g_layout)
        return;
    g_layout = layout;
    if (g_typed) {
        g_keys.reset(b);
        g_keys.configure();
    }
}

void calibrate_volume() {
    input::Batch b;
    g_keys.calibrate(b);
}

std::array<std::wstring, 128> labels() {
    input::Batch b(true);
    return g_keys.labels();
}

uint64_t dropped() {
    return g_connect.dropped();
}

std::array<int, 32> builtin_curve(int index) {
    return kCurves[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(kCurves.size()) - 1))];
}

const char* mode_name(Mode m) {
    return m == Mode::MidiConnect ? "MidiConnect" : "Keys";
}

}
