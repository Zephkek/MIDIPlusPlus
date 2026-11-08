#include "Player.hpp"
#include <avrt.h>
#include <timeapi.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")

Player* g_player = nullptr;

namespace {
    void name_thread(const wchar_t* name) {
        using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
            if (auto fn = reinterpret_cast<Fn>(GetProcAddress(k, "SetThreadDescription")))
                fn(GetCurrentThread(), name);
    }
}

Player::Player() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq_ = f.QuadPart;
    for (auto& e : enabled_)
        e.store(1, std::memory_order_relaxed);
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

Player::~Player() {
    shutdown();
    if (wake_)
        CloseHandle(wake_);
}

void Player::start(HWND notify, UINT message) {
    notify_ = notify;
    message_ = message;
    if (!thread_.joinable())
        thread_ = std::thread(&Player::run, this);
}

void Player::shutdown() {
    if (!thread_.joinable())
        return;
    quit_.store(true);
    bump();
    thread_.join();
}

int64_t Player::now() const {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int64_t Player::pos_at(const State& s, int64_t qpc) const {
    if (!s.playing)
        return s.pos;
    return s.pos + static_cast<int64_t>(static_cast<double>(qpc - s.qpc) * s.speed * 1e9 / static_cast<double>(freq_));
}

int Player::effective_batch(const State& s) {
    if (s.batchMs >= 0)
        return s.batchMs;
    return s.song && s.song->peakNotesPerSec >= 2000.0 ? 4 : 0;
}

void Player::bump() {
    gen_.fetch_add(1, std::memory_order_acq_rel);
    SetEvent(wake_);
}

void Player::load(std::shared_ptr<const Song> song, int64_t at) {
    const size_t count = song ? song->tracks.size() : 0;
    std::shared_ptr<out::Activity[]> live = count ? std::make_shared<out::Activity[]>(count) : nullptr;
    std::lock_guard l(mu_);
    st_.live = std::move(live);
    st_.liveCount = count;
    st_.song = std::move(song);
    st_.pos = at;
    st_.qpc = now();
    st_.playing = false;
    st_.reseek = true;
    st_.transpose = 0;
    for (auto& e : enabled_)
        e.store(1, std::memory_order_relaxed);
    bump();
}

std::shared_ptr<const Song> Player::song() const {
    std::lock_guard l(mu_);
    return st_.song;
}

void Player::play() {
    std::lock_guard l(mu_);
    if (!st_.song || st_.playing)
        return;
    if (st_.pos >= st_.song->length)
        st_.pos = 0;
    if (st_.pos <= 0) {
        st_.pos = -kLead;
        st_.restart = true;
    }
    st_.qpc = now();
    st_.playing = true;
    st_.reseek = true;
    bump();
}

void Player::pause() {
    std::lock_guard l(mu_);
    if (!st_.playing)
        return;
    st_.pos = pos_at(st_, now());
    st_.playing = false;
    bump();
}

bool Player::playing() const {
    std::lock_guard l(mu_);
    return st_.playing;
}

void Player::seek(int64_t ns) {
    std::lock_guard l(mu_);
    if (!st_.song)
        return;
    st_.pos = std::clamp<int64_t>(ns, 0, st_.song->length);
    st_.qpc = now();
    st_.reseek = true;
    bump();
}

void Player::skip(int64_t deltaNs) {
    std::lock_guard l(mu_);
    if (!st_.song)
        return;
    int64_t q = now();
    st_.pos = std::clamp<int64_t>(pos_at(st_, q) + deltaNs, 0, st_.song->length);
    st_.qpc = q;
    st_.reseek = true;
    bump();
}

void Player::restart() {
    std::lock_guard l(mu_);
    if (!st_.song)
        return;
    st_.pos = -kLead;
    st_.qpc = now();
    st_.playing = true;
    st_.reseek = true;
    st_.restart = true;
    bump();
}

void Player::recalibrate() {
    std::lock_guard l(mu_);
    st_.restart = true;
    bump();
}

void Player::set_speed(double speed) {
    std::lock_guard l(mu_);
    int64_t q = now();
    st_.pos = pos_at(st_, q);
    st_.qpc = q;
    st_.speed = std::clamp(speed, 0.1, 4.0);
    bump();
}

double Player::speed() const {
    std::lock_guard l(mu_);
    return st_.speed;
}

int64_t Player::position() const {
    std::lock_guard l(mu_);
    return pos_at(st_, now());
}

void Player::set_track_enabled(size_t track, bool on) {
    if (track < enabled_.size())
        enabled_[track].store(on ? 1 : 0, std::memory_order_relaxed);
}

void Player::set_output(out::Mode mode) {
    std::lock_guard l(mu_);
    if (st_.mode == mode)
        return;
    st_.mode = mode;
    bump();
}

out::Mode Player::output() const {
    std::lock_guard l(mu_);
    return st_.mode;
}

void Player::set_batching(int ms) {
    std::lock_guard l(mu_);
    st_.batchMs = ms;
    bump();
}

int Player::batching() const {
    std::lock_guard l(mu_);
    return effective_batch(st_);
}

void Player::transpose(int steps) {
    std::lock_guard l(mu_);
    st_.transpose = steps;
    bump();
}

void Player::set_key(int semitones) {
    std::lock_guard l(mu_);
    st_.key = std::clamp(semitones, -48, 48);
    bump();
}

int Player::key() const {
    std::lock_guard l(mu_);
    return st_.key;
}

Player::Tracks Player::tracks() const {
    std::lock_guard l(mu_);
    return { st_.live, st_.liveCount };
}

void Player::release() {
    std::lock_guard l(mu_);
    st_.release = true;
    bump();
}

Player::Stats Player::stats() {
    std::lock_guard l(statsMu_);
    Stats s = stats_;
    s.avgLateUs = s.batches ? lateSum_ / static_cast<double>(s.batches) : 0.0;
    stats_ = {};
    lateSum_ = 0.0;
    return s;
}

bool Player::wait_until(int64_t target, uint64_t seen) {
    for (;;) {
        int64_t t0 = now();
        int64_t rem = target - t0;
        if (rem <= 0)
            return true;
        if (gen_.load(std::memory_order_relaxed) != seen)
            return false;
        if (rem > margin_) {
            int64_t sleep = rem - margin_;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<int64_t>(1, sleep * 10'000'000 / freq_);
            SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0);
            HANDLE hs[2] = { wake_, timer_ };
            DWORD r = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            if (r != WAIT_OBJECT_0 + 1) {
                CancelWaitableTimer(timer_);
                return false;
            }
            int64_t late = now() - (t0 + sleep);
            est_ = late > est_ ? late : est_ - (est_ - late) / 64;
            margin_ = std::clamp(est_ + freq_ * 150 / 1'000'000, minMargin_, maxMargin_);
            continue;
        }
        while (now() < target) {
            if (gen_.load(std::memory_order_relaxed) != seen)
                return false;
            YieldProcessor();
        }
        return true;
    }
}

void Player::run() {
    name_thread(L"MIDI++ playback");
    DWORD task = 0;
    HANDLE av = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    if (av)
        AvSetMmThreadPriority(av, AVRT_PRIORITY_HIGH);
    else
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const bool coarse = !timer_;
    if (coarse) {
        timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        timeBeginPeriod(1);
    }
    const int64_t us = freq_ / 1'000'000 ? freq_ / 1'000'000 : 1;
    minMargin_ = (coarse ? 2000 : 300) * us;
    maxMargin_ = (coarse ? 16000 : 2000) * us;
    est_ = (coarse ? 2000 : 850) * us;
    margin_ = std::clamp(est_ + 150 * us, minMargin_, maxMargin_);

    std::shared_ptr<const Song> song;
    std::shared_ptr<out::Activity[]> live;
    size_t liveCount = 0;
    std::vector<uint8_t> held;
    std::array<uint32_t, 128> counts{};
    uint64_t bits[2] = {};
    size_t idx = 0;
    State s;
    uint64_t seen = 0;
    out::Mode mode = out::Mode::Keys;
    int64_t gap = 0;
    int64_t lastBatch = 0;

    auto publish = [&]() {
        mask_[0].store(bits[0], std::memory_order_relaxed);
        mask_[1].store(bits[1], std::memory_order_relaxed);
    };
    auto silence = [&]() {
        {
            input::Batch b;
            out::sink(mode).reset(b);
        }
        std::fill(held.begin(), held.end(), uint8_t{ 0 });
        counts.fill(0);
        bits[0] = bits[1] = 0;
        publish();
        for (size_t i = 0; i < liveCount; ++i) {
            live[i].mask[0].store(0, std::memory_order_relaxed);
            live[i].mask[1].store(0, std::memory_order_relaxed);
        }
    };

    while (!quit_.load(std::memory_order_acquire)) {
        uint64_t g = gen_.load(std::memory_order_acquire);
        if (g != seen) {
            State c;
            {
                std::lock_guard l(mu_);
                c = st_;
                st_.reseek = false;
                st_.release = false;
                if (st_.playing)
                    st_.restart = false;
                st_.transpose = 0;
            }
            seen = g;
            const bool fresh = c.song != song;
            if (fresh || c.mode != mode || c.release || c.reseek || c.key != s.key || (s.playing && !c.playing))
                silence();
            mode = c.mode;
            if (fresh) {
                song = c.song;
                live = c.live;
                liveCount = c.liveCount;
                held.assign(song ? song->tracks.size() * 128 : 0, 0);
                idx = 0;
            }
            s = c;
            gap = effective_batch(s) * freq_ / 1000;
            if (c.restart && c.playing && song) {
                input::Batch b;
                out::sink(mode).restart(b);
            }
            if (c.transpose && song) {
                const auto& at = midi::Config::getInstance().auto_transpose;
                input::Stroke key = input::stroke_for_vk(input::parse_vk(c.transpose > 0 ? at.TRANSPOSE_UP_KEY : at.TRANSPOSE_DOWN_KEY));
                for (int i = 0, n = std::abs(c.transpose); i < n && key; ++i) {
                    // sleep before arrowsend to fix off-by-1 error
                    Sleep(50);
                    input::Batch b;
                    b.tap(key);
                }
                std::lock_guard l(mu_);
                if (gen_.load() == seen && st_.playing) {
                    st_.qpc = now();
                    s.qpc = st_.qpc;
                }
            }
            if (song && (c.reseek || fresh))
                idx = song->index_at(pos_at(s, now()));
            continue;
        }

        if (!song || !s.playing) {
            WaitForSingleObject(wake_, INFINITE);
            continue;
        }

        const auto& times = song->t;
        const auto& evs = song->ev;
        const size_t n = times.size();
        if (idx >= n) {
            silence();
            bool ended = false;
            {
                std::lock_guard l(mu_);
                if (gen_.load() == seen) {
                    st_.pos = song->length;
                    st_.playing = false;
                    gen_.fetch_add(1);
                    ended = true;
                }
            }
            if (ended && notify_)
                PostMessageW(notify_, message_, Ended, 0);
            continue;
        }

        int64_t target = s.qpc + static_cast<int64_t>(static_cast<double>(times[idx] - s.pos) / s.speed * static_cast<double>(freq_) / 1e9);
        if (gap && lastBatch)
            target = std::max(target, lastBatch + gap);
        if (!wait_until(target, seen))
            continue;

        const int64_t q = now();
        const int64_t p = pos_at(s, q);
        size_t end = idx;
        while (end < n && times[end] <= p)
            ++end;
        uint64_t played = 0;
        {
            input::Batch b;
            out::Sink& sk = out::sink(mode);
            for (size_t i = idx; i < end; ++i) {
                const uint32_t e = evs[i];
                const uint32_t kind = Song::kind(e);
                const int tr = Song::track(e);
                const int note = Song::note(e);
                if (kind == Song::On) {
                    if (!enabled_[tr].load(std::memory_order_relaxed))
                        continue;
                    uint8_t& h = held[static_cast<size_t>(tr) * 128 + note];
                    if (h == 255)
                        continue;
                    ++h;
                    ++played;
                    const int k = note + s.key;
                    if (k < 0 || k > 127)
                        continue;
                    const uint32_t vel = static_cast<uint32_t>(Song::vel(e));
                    sk.on(b, 0, k, static_cast<int>(vel), i < song->len.size() ? song->len[i] : 0);
                    hits_[k].store((((hits_[k].load(std::memory_order_relaxed) >> 8) + 1) << 8) | vel, std::memory_order_relaxed);
                    if (counts[k]++ == 0)
                        bits[k >> 6] |= 1ull << (k & 63);
                    if (static_cast<size_t>(tr) < liveCount) {
                        out::Activity& a = live[static_cast<size_t>(tr)];
                        a.hit.store((((a.hit.load(std::memory_order_relaxed) >> 8) + 1) << 8) | vel, std::memory_order_relaxed);
                        if (h == 1)
                            a.mask[k >> 6].fetch_or(1ull << (k & 63), std::memory_order_relaxed);
                    }
                }
                else if (kind == Song::Off) {
                    uint8_t& h = held[static_cast<size_t>(tr) * 128 + note];
                    if (!h)
                        continue;
                    --h;
                    const int k = note + s.key;
                    if (k < 0 || k > 127)
                        continue;
                    sk.off(b, 0, k);
                    if (--counts[k] == 0)
                        bits[k >> 6] &= ~(1ull << (k & 63));
                    if (!h && static_cast<size_t>(tr) < liveCount)
                        live[static_cast<size_t>(tr)].mask[k >> 6].fetch_and(~(1ull << (k & 63)), std::memory_order_relaxed);
                }
                else if (enabled_[tr].load(std::memory_order_relaxed)) {
                    sk.pedal(b, Song::vel(e));
                }
            }
            sk.commit(b);
        }
        idx = end;
        lastBatch = q;
        publish();
        notes_.fetch_add(played, std::memory_order_relaxed);

        double late = static_cast<double>(q - target) * 1e6 / static_cast<double>(freq_);
        std::lock_guard l(statsMu_);
        ++stats_.batches;
        lateSum_ += late;
        stats_.maxLateUs = std::max(stats_.maxLateUs, late);
    }

    silence();
    if (coarse)
        timeEndPeriod(1);
    CloseHandle(timer_);
    timer_ = nullptr;
    if (av)
        AvRevertMmThreadCharacteristics(av);
}
