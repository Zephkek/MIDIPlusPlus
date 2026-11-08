#pragma once
#include "Output.hpp"
#include "Song.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class Player {
public:
    enum Event : WPARAM { Ended = 1 };

    struct Stats {
        double avgLateUs = 0.0;
        double maxLateUs = 0.0;
        uint64_t batches = 0;
    };

    struct Tracks {
        std::shared_ptr<out::Activity[]> live;
        size_t count = 0;
    };

    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void start(HWND notify, UINT message);
    void shutdown();

    void load(std::shared_ptr<const Song> song, int64_t at = 0);
    std::shared_ptr<const Song> song() const;
    void play();
    void pause();
    bool playing() const;
    void seek(int64_t ns);
    void skip(int64_t deltaNs);
    void restart();
    void recalibrate();
    void set_speed(double speed);
    double speed() const;
    int64_t position() const;
    void set_track_enabled(size_t track, bool on);
    void set_output(out::Mode mode);
    out::Mode output() const;
    void set_batching(int ms);
    int batching() const;
    void transpose(int steps);
    void set_key(int semitones);
    int key() const;
    Tracks tracks() const;
    void release();
    Stats stats();
    uint64_t notes_played() const { return notes_.load(std::memory_order_relaxed); }
    std::array<uint64_t, 2> held() const {
        return { mask_[0].load(std::memory_order_relaxed), mask_[1].load(std::memory_order_relaxed) };
    }
    std::array<uint32_t, 128> hits() const {
        std::array<uint32_t, 128> h;
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = hits_[i].load(std::memory_order_relaxed);
        return h;
    }

    static constexpr int64_t kLead = 50'000'000;

private:
    struct State {
        std::shared_ptr<const Song> song;
        int64_t pos = 0;
        int64_t qpc = 0;
        double speed = 1.0;
        bool playing = false;
        bool reseek = true;
        bool release = false;
        bool restart = false;
        int transpose = 0;
        int key = 0;
        int batchMs = -1;
        std::shared_ptr<out::Activity[]> live;
        size_t liveCount = 0;
        out::Mode mode = out::Mode::Keys;
    };

    void run();
    bool wait_until(int64_t target, uint64_t seen);
    void bump();
    int64_t now() const;
    int64_t pos_at(const State& s, int64_t qpc) const;
    static int effective_batch(const State& s);

    mutable std::mutex mu_;
    State st_;
    std::atomic<uint64_t> gen_{ 1 };
    std::atomic<bool> quit_{ false };
    std::atomic<uint64_t> notes_{ 0 };
    std::array<std::atomic<uint64_t>, 2> mask_{};
    std::array<std::atomic<uint32_t>, 128> hits_{};
    std::array<std::atomic<uint8_t>, 65536> enabled_;
    HANDLE wake_ = nullptr;
    HANDLE timer_ = nullptr;
    HWND notify_ = nullptr;
    UINT message_ = 0;
    std::thread thread_;
    int64_t freq_ = 1;
    int64_t margin_ = 0;
    int64_t est_ = 0;
    int64_t minMargin_ = 0;
    int64_t maxMargin_ = 0;
    std::mutex statsMu_;
    Stats stats_;
    double lateSum_ = 0.0;
};

extern Player* g_player;
