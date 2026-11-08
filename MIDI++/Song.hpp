#pragma once
#include "config.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SongTrack {
    std::string name;
    std::string instrument;
    int channel = -1;
    int program = -1;
    uint64_t notes = 0;
    bool drums = false;
};

struct Song {
    enum : uint32_t { Off = 0, On = 1, Pedal = 2 };

    static uint32_t pack(uint32_t kind, uint32_t track, uint32_t note, uint32_t vel) {
        return (note & 127) | ((vel & 127) << 7) | ((kind & 3) << 14) | (track << 16);
    }
    static int note(uint32_t e) { return e & 127; }
    static int vel(uint32_t e) { return (e >> 7) & 127; }
    static uint32_t kind(uint32_t e) { return (e >> 14) & 3; }
    static int track(uint32_t e) { return static_cast<int>(e >> 16); }

    std::vector<int64_t> t;
    std::vector<uint32_t> ev;
    std::vector<SongTrack> tracks;
    std::filesystem::path path;
    int64_t length = 0;
    uint64_t notes = 0;
    uint64_t skipped = 0;
    double peakNotesPerSec = 0.0;
    int format = 0;
    int division = 0;
    double bpm = 120.0;
    size_t tempoChanges = 0;
    int tsNum = 4;
    int tsDen = 4;
    size_t fileBytes = 0;
    std::vector<std::pair<int64_t, double>> tempos;
    std::vector<uint32_t> len;

    size_t size() const { return t.size(); }
    double bpm_at(int64_t ns) const;
    size_t index_at(int64_t ns) const;
    void notes_and_durations(std::vector<int>& notes, std::vector<double>& durations, size_t limit) const;
};

struct LoadOptions {
    midi::NoteHandlingMode pairing = midi::NoteHandlingMode::LIFO;
    midi::LegitModeSettings legit;
    bool detectDrums = true;
};

std::shared_ptr<Song> load_song(const std::filesystem::path& path, const LoadOptions& opt,
    std::atomic<int>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);
const char* gm_name(int program);
