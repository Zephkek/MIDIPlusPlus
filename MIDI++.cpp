#define NOMINMAX
#include <cmath>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <queue>
#include <algorithm>
#include <cstdint>
#include <future>
#include <iomanip>
#include <numeric>
#include <set>
#include "concurrentqueue.h"
#include "json.hpp"
#include <random>

#pragma intrinsic(_mm256_set1_pd, _mm256_mul_pd, _mm256_cvtsd_f64, _mm256_sub_pd, _mm256_add_pd, _mm256_div_pd, _mm256_cmp_pd, _mm256_fmadd_pd)

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
static constexpr size_t CACHE_LINE_SIZE = 64; // now watch someone come to me with a pentium cpu or some fucking amd athlon
static constexpr std::array<const char*, 12> NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static constexpr size_t PREFETCH_DISTANCE = 2;


struct alignas(CACHE_LINE_SIZE) NoteEvent {
    Duration time;
    std::string note;
    bool isPress;
    int velocity;
    bool isSustain;
    int sustainValue;

    NoteEvent() noexcept : time(Duration::zero()), isPress(false), velocity(0), isSustain(false), sustainValue(0) {}
    NoteEvent(Duration t, std::string_view n, bool p, int v, bool s = false, int sv = 0) noexcept
        : time(t), note(n), isPress(p), velocity(v), isSustain(s), sustainValue(sv) {}

    bool operator>(const NoteEvent& other) const noexcept { return time > other.time; }
};
using json = nlohmann::json;
enum class VelocityCurveType {
    LinearCoarse,
    LinearFine,
    ImprovedLowVolume,
    Logarithmic,
    Exponential
};
enum class NoteHandlingMode { // makes no difference at the moment in games where the visualizer isn't advanced enough, stacked notes are generally a midi format problem and displaying them properly is a game specific issue
    FIFO,
    LIFO,
    NoHandling  
};
struct Config {
    struct VolumeSettings { //defaulting to VP2 as that's probably where most of the traffic would come from.
        int MIN_VOLUME = 10;
        int MAX_VOLUME = 200;
        int INITIAL_VOLUME = 100;
        int VOLUME_STEP = 10;
        int ADJUSTMENT_INTERVAL_MS = 50;
    };

    struct SustainSettings {
        int SUSTAIN_CUTOFF = 0;
    };

    struct LegitModeSettings {
        bool ENABLED = false;
        double TIMING_VARIATION = 0.1;
        double NOTE_SKIP_CHANCE = 0.02;
        double EXTRA_DELAY_CHANCE = 0.05;
        double EXTRA_DELAY_MIN = 0.05;
        double EXTRA_DELAY_MAX = 0.2;
    };
    struct MIDISettings {
        bool FILTER_DRUMS = true;
    };
    struct HotkeySettings {
        std::string SUSTAIN_KEY = "VK_SPACE";
        std::string VOLUME_UP_KEY = "VK_RIGHT";
        std::string VOLUME_DOWN_KEY = "VK_LEFT";
    };
    struct PlaybackSettings {
        VelocityCurveType velocityCurve = VelocityCurveType::LinearCoarse;
        NoteHandlingMode noteHandlingMode = NoteHandlingMode::LIFO; 
    };
    MIDISettings midi;
    PlaybackSettings playback;
    VolumeSettings volume;
    SustainSettings sustain;
    LegitModeSettings legit_mode;
    HotkeySettings hotkeys;
    std::map<std::string, std::map<std::string, std::string>> key_mappings;
    std::map<std::string, std::string> controls;
};

Config config;
void from_json(const json& j, Config::HotkeySettings& h) {
    j.at("SUSTAIN_KEY").get_to(h.SUSTAIN_KEY);
    j.at("VOLUME_UP_KEY").get_to(h.VOLUME_UP_KEY);
    j.at("VOLUME_DOWN_KEY").get_to(h.VOLUME_DOWN_KEY);
}

void from_json(const json& j, Config::LegitModeSettings& l) {
    j.at("ENABLED").get_to(l.ENABLED);
    j.at("TIMING_VARIATION").get_to(l.TIMING_VARIATION);
    j.at("NOTE_SKIP_CHANCE").get_to(l.NOTE_SKIP_CHANCE);
    j.at("EXTRA_DELAY_CHANCE").get_to(l.EXTRA_DELAY_CHANCE);
    j.at("EXTRA_DELAY_MIN").get_to(l.EXTRA_DELAY_MIN);
    j.at("EXTRA_DELAY_MAX").get_to(l.EXTRA_DELAY_MAX);
}

void from_json(const json& j, Config::VolumeSettings& v) {
    j.at("MIN_VOLUME").get_to(v.MIN_VOLUME);
    j.at("MAX_VOLUME").get_to(v.MAX_VOLUME);
    j.at("INITIAL_VOLUME").get_to(v.INITIAL_VOLUME);
    j.at("VOLUME_STEP").get_to(v.VOLUME_STEP);
    j.at("ADJUSTMENT_INTERVAL_MS").get_to(v.ADJUSTMENT_INTERVAL_MS);
}

void from_json(const json& j, Config::SustainSettings& s) {
    j.at("SUSTAIN_CUTOFF").get_to(s.SUSTAIN_CUTOFF);
}
void from_json(const json& j, Config::PlaybackSettings& p) {
    std::string curve;
    j.at("VELOCITY_CURVE").get_to(curve);
    if (curve == "LinearCoarse") {
        p.velocityCurve = VelocityCurveType::LinearCoarse;
    }
    else if (curve == "LinearFine") {
        p.velocityCurve = VelocityCurveType::LinearFine;
    }
    else if (curve == "ImprovedLowVolume") {
        p.velocityCurve = VelocityCurveType::ImprovedLowVolume;
    }
    else if (curve == "Logarithmic") {
        p.velocityCurve = VelocityCurveType::Logarithmic;
    }
    else if (curve == "Exponential") {
        p.velocityCurve = VelocityCurveType::Exponential;
    }
    else {
        p.velocityCurve = VelocityCurveType::LinearCoarse; // Default
    }

    std::string handling_mode;
    j.at("STACKED_NOTE_HANDLING_MODE").get_to(handling_mode);
    if (handling_mode == "FIFO") {
        p.noteHandlingMode = NoteHandlingMode::FIFO;
    }
    else if (handling_mode == "LIFO") {
        p.noteHandlingMode = NoteHandlingMode::LIFO;
    }
    else {
        p.noteHandlingMode = NoteHandlingMode::NoHandling; // Default to NoHandling if not specified
    }
}


void from_json(const json& j, Config& c) {
    j.at("VOLUME_SETTINGS").get_to(c.volume);
    j.at("SUSTAIN_SETTINGS").get_to(c.sustain);
    j.at("KEY_MAPPINGS").get_to(c.key_mappings);
    j.at("CONTROLS").get_to(c.controls);
    j.at("LEGIT_MODE_SETTINGS").get_to(c.legit_mode);
    j.at("HOTKEY_SETTINGS").get_to(c.hotkeys);
    j.at("MIDI_SETTINGS").at("FILTER_DRUMS").get_to(c.midi.FILTER_DRUMS);
    j.at("PLAYBACK_SETTINGS").get_to(c.playback);
}
void setDefaultConfig() {
    config.sustain = { 64 };
    config.hotkeys = {
       "VK_SPACE",  // default SUSTAIN_KEY
       "VK_RIGHT", // default VOLUME_UP_KEY
       "VK_LEFT"   // default VOLUME_DOWN_KEY
    };
    config.key_mappings["LIMITED"] = {
        {"C2", "1"}, {"C#2", "!"}, {"D2", "2"}, {"D#2", "@"}, {"E2", "3"}, {"F2", "4"},
        {"F#2", "$"}, {"G2", "5"}, {"G#2", "%"}, {"A2", "6"}, {"A#2", "^"}, {"B2", "7"},
        {"C3", "8"}, {"C#3", "*"}, {"D3", "9"}, {"D#3", "("}, {"E3", "0"}, {"F3", "q"},
        {"F#3", "Q"}, {"G3", "w"}, {"G#3", "W"}, {"A3", "e"}, {"A#3", "E"}, {"B3", "r"},
        {"C4", "t"}, {"C#4", "T"}, {"D4", "y"}, {"D#4", "Y"}, {"E4", "u"}, {"F4", "i"},
        {"F#4", "I"}, {"G4", "o"}, {"G#4", "O"}, {"A4", "p"}, {"A#4", "P"}, {"B4", "a"},
        {"C5", "s"}, {"C#5", "S"}, {"D5", "d"}, {"D#5", "D"}, {"E5", "f"}, {"F5", "g"},
        {"F#5", "G"}, {"G5", "h"}, {"G#5", "H"}, {"A5", "j"}, {"A#5", "J"}, {"B5", "k"},
        {"C6", "l"}, {"C#6", "L"}, {"D6", "z"}, {"D#6", "Z"}, {"E6", "x"}, {"F6", "c"},
        {"F#6", "C"}, {"G6", "v"}, {"G#6", "V"}, {"A6", "b"}, {"A#6", "B"}, {"B6", "n"},
        {"C7", "m"}
    };

    config.key_mappings["FULL"] = {
        {"A0", "ctrl+1"}, {"A#0", "ctrl+2"}, {"B0", "ctrl+3"}, {"C1", "ctrl+4"}, {"C#1", "ctrl+5"},
        {"D1", "ctrl+6"}, {"D#1", "ctrl+7"}, {"E1", "ctrl+8"}, {"F1", "ctrl+9"}, {"F#1", "ctrl+0"},
        {"G1", "ctrl+q"}, {"G#1", "ctrl+w"}, {"A1", "ctrl+e"}, {"A#1", "ctrl+r"}, {"B1", "ctrl+t"},
        {"C2", "1"}, {"C#2", "!"}, {"D2", "2"}, {"D#2", "@"}, {"E2", "3"}, {"F2", "4"},
        {"F#2", "$"}, {"G2", "5"}, {"G#2", "%"}, {"A2", "6"}, {"A#2", "^"}, {"B2", "7"},
        {"C3", "8"}, {"C#3", "*"}, {"D3", "9"}, {"D#3", "("}, {"E3", "0"}, {"F3", "q"},
        {"F#3", "Q"}, {"G3", "w"}, {"G#3", "W"}, {"A3", "e"}, {"A#3", "E"}, {"B3", "r"},
        {"C4", "t"}, {"C#4", "T"}, {"D4", "y"}, {"D#4", "Y"}, {"E4", "u"}, {"F4", "i"},
        {"F#4", "I"}, {"G4", "o"}, {"G#4", "O"}, {"A4", "p"}, {"A#4", "P"}, {"B4", "a"},
        {"C5", "s"}, {"C#5", "S"}, {"D5", "d"}, {"D#5", "D"}, {"E5", "f"}, {"F5", "g"},
        {"F#5", "G"}, {"G5", "h"}, {"G#5", "H"}, {"A5", "j"}, {"A#5", "J"}, {"B5", "k"},
        {"C6", "l"}, {"C#6", "L"}, {"D6", "z"}, {"D#6", "Z"}, {"E6", "x"}, {"F6", "c"},
        {"F#6", "C"}, {"G6", "v"}, {"G#6", "V"}, {"A6", "b"}, {"A#6", "B"}, {"B6", "n"},
        {"C7", "m"}, {"C#7", "ctrl+y"}, {"D7", "ctrl+u"}, {"D#7", "ctrl+i"}, {"E7", "ctrl+o"},
        {"F7", "ctrl+p"}, {"F#7", "ctrl+a"}, {"G7", "ctrl+s"}, {"G#7", "ctrl+d"}, {"A7", "ctrl+f"},
        {"A#7", "ctrl+g"}, {"B7", "ctrl+h"}, {"C8", "ctrl+j"}
    };
    config.playback.velocityCurve = VelocityCurveType::LinearCoarse; // Default velocity curve
    config.playback.noteHandlingMode = NoteHandlingMode::LIFO; // Default to LIFO handling mode

    config.controls = {
        {"PLAY_PAUSE", "VK_DELETE"},
        {"REWIND", "VK_HOME"},
        {"SKIP", "VK_END"},
        {"SPEED_UP", "VK_PRIOR"},
        {"SLOW_DOWN", "VK_NEXT"},
        {"LOAD_NEW_SONG", "VK_F5"},
        {"TOGGLE_88_KEY_MODE", "VK_F6"},
        {"TOGGLE_VOLUME_ADJUSTMENT", "VK_F7"},
        {"TOGGLE_TRANSPOSE_ADJUSTMENT", "VK_F8"},
        {"RESTART_SONG", "VK_F1"},
        {"STOP_AND_EXIT", "VK_ESCAPE"},
        {"TOGGLE_SUSTAIN_MODE", "VK_F10"},
        {"TOGGLE_VELOCITY_KEYPRESS", "VK_F2"},
        { "TOGGLE_TRANSPOSE_KEY","VK_F3" }

    };

}

void loadConfig() {
    std::string configPath = "config.json";
    std::ifstream configFile(configPath);
    if (!configFile.is_open()) {
        throw std::runtime_error("Config file not found at " + configPath);
    }

    try {
        json j;
        configFile >> j;
        j.get_to(config);
    }
    catch (const json::exception& e) {
        throw std::runtime_error("Error parsing config: " + std::string(e.what()));
    }
}

enum class ConsoleColor : int {
    Default = 39, Black = 30, Red = 31, Green = 32, Yellow = 33, Blue = 34, Magenta = 35, Cyan = 36,
    LightGray = 37, DarkGray = 90, LightRed = 91, LightGreen = 92, LightYellow = 93, LightBlue = 94,
    LightMagenta = 95, LightCyan = 96, White = 97
};
inline void setcolor(ConsoleColor color) {
    std::cout << "\033[" << static_cast<int>(color) << "m";
}

enum class SustainMode {
    IG,
    SPACE_DOWN,
    SPACE_UP
};

SustainMode currentSustainMode = SustainMode::IG;

void arrowsend(WORD scanCode, bool extended) {
    INPUT inputs[2] = { 0 };
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = scanCode;
    inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wScan = scanCode;
    inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(2, inputs, sizeof(INPUT));
}
struct MidiEvent {
    uint32_t absoluteTick = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    std::vector<uint8_t> metaData;

    [[nodiscard]] constexpr uint32_t getAbsoluteTick() const noexcept { return absoluteTick; }
    [[nodiscard]] constexpr uint8_t getStatus() const noexcept { return status; }
    [[nodiscard]] constexpr uint8_t getData1() const noexcept { return data1; }
    [[nodiscard]] constexpr uint8_t getData2() const noexcept { return data2; }
    [[nodiscard]] const std::vector<uint8_t>& getMetaData() const noexcept { return metaData; }

    void setMetaData(std::vector<uint8_t>&& md) noexcept { metaData = std::move(md); }
};

struct TempoChange {
    uint32_t tick;
    uint32_t microsecondsPerQuarter;
};

struct TimeSignature {
    uint32_t tick;
    uint8_t numerator;
    uint8_t denominator;
    uint8_t clocksPerClick;
    uint8_t thirtySecondNotesPerQuarter;
};

struct KeySignature {
    uint32_t tick;
    int8_t key;
    uint8_t scale;
};

struct MidiTrack {
    std::string name;
    std::vector<MidiEvent> events;
};

struct MidiFile {
    uint16_t format = 0;
    uint16_t numTracks = 0;
    uint16_t division = 0;
    std::vector<MidiTrack> tracks;
    std::vector<TempoChange> tempoChanges;
    std::vector<TimeSignature> timeSignatures;
    std::vector<KeySignature> keySignatures;
};

class MidiParser {
private:
    mutable std::ifstream file;

    static constexpr uint32_t swapUint32(uint32_t value) noexcept {
        return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) |
            ((value << 8) & 0x00FF0000) | ((value << 24) & 0xFF000000);
    }

    static constexpr uint16_t swapUint16(uint16_t value) noexcept {
        return (value >> 8) | (value << 8);
    }

    [[nodiscard]] bool readVarLen(uint32_t& value) {
        value = 0;
        uint8_t byte;
        do {
            if (!file.read(reinterpret_cast<char*>(&byte), 1)) return false;
            value = (value << 7) | (byte & 0x7F);
        } while (byte & 0x80);
        return true;
    }

    [[nodiscard]] bool readInt32(uint32_t& value) {
        if (!file.read(reinterpret_cast<char*>(&value), 4)) return false;
        value = swapUint32(value);
        return true;
    }

    [[nodiscard]] bool readInt16(uint16_t& value) {
        if (!file.read(reinterpret_cast<char*>(&value), 2)) return false;
        value = swapUint16(value);
        return true;
    }

    [[nodiscard]] bool readChunk(char* buffer, size_t size) {
        return file.read(buffer, size).good();
    }

    [[nodiscard]] bool validateTrackLength(std::streampos trackEnd) const {
        return file.tellg() <= trackEnd;
    }

    [[nodiscard]] bool validateEventLength(uint32_t length, std::streampos trackEnd) const {
        return file.tellg() + std::streampos(length) <= trackEnd;
    }

    void parseMetaEvent(MidiEvent& event, MidiFile& midiFile, uint32_t absoluteTick, std::streampos trackEnd) {
        uint8_t metaType;
        if (!file.read(reinterpret_cast<char*>(&metaType), 1)) return;
        uint32_t length;
        if (!readVarLen(length) || !validateEventLength(length, trackEnd)) return;

        event.metaData.resize(length);
        if (!file.read(reinterpret_cast<char*>(event.metaData.data()), length)) return;

        event.status = 0xFF;
        event.data1 = metaType;

        switch (metaType) {
        case 0x51:
            if (length == 3) {
                uint32_t microsecondsPerQuarter = (event.metaData[0] << 16) | (event.metaData[1] << 8) | event.metaData[2];
                midiFile.tempoChanges.push_back({ absoluteTick, microsecondsPerQuarter });
            }
            break;
        case 0x58:
            if (length == 4) {
                midiFile.timeSignatures.push_back({
                    absoluteTick,
                    event.metaData[0],
                    static_cast<uint8_t>(1 << event.metaData[1]),
                    event.metaData[2],
                    event.metaData[3]
                    });
            }
            break;
        case 0x59:
            if (length == 2) {
                midiFile.keySignatures.push_back({
                    absoluteTick,
                    static_cast<int8_t>(event.metaData[0]),
                    event.metaData[1]
                    });
            }
            break;
        }
    }

public:
    void reset() {
        file.close();
        file.clear();
    }

    [[nodiscard]] MidiFile parse(const std::string& filename) {
        reset();
        file.open(filename, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Unable to open file: " + filename);

        MidiFile midiFile;
        char headerChunk[4];
        if (!readChunk(headerChunk, 4) || std::string(headerChunk, 4) != "MThd")
            throw std::runtime_error("Invalid MIDI file: Missing MThd");

        uint32_t headerLength;
        if (!readInt32(headerLength) || headerLength != 6)
            throw std::runtime_error("Invalid MIDI header length");

        if (!readInt16(midiFile.format) || !readInt16(midiFile.numTracks) || !readInt16(midiFile.division))
            throw std::runtime_error("Error reading MIDI header fields");

        if (midiFile.format > 2)
            throw std::runtime_error("Unsupported MIDI format: " + std::to_string(midiFile.format));

        for (int i = 0; i < midiFile.numTracks; ++i) {
            char trackChunk[4];
            if (!readChunk(trackChunk, 4) || std::string(trackChunk, 4) != "MTrk")
                throw std::runtime_error("Invalid MIDI file: Missing MTrk");

            uint32_t trackLength;
            if (!readInt32(trackLength)) throw std::runtime_error("Error reading track length");

            MidiTrack track;
            uint32_t absoluteTick = 0;
            uint8_t lastStatus = 0;
            std::streampos trackEnd = file.tellg() + std::streampos(trackLength);

            while (file.tellg() < trackEnd) {
                uint32_t deltaTime;
                if (!readVarLen(deltaTime)) break;
                absoluteTick += deltaTime;
                uint8_t status;
                if (!file.read(reinterpret_cast<char*>(&status), 1)) break;

                if (status < 0x80) {
                    if (lastStatus == 0) break;
                    status = lastStatus;
                    file.seekg(-1, std::ios::cur);
                }
                else {
                    lastStatus = status;
                }

                MidiEvent event;
                event.absoluteTick = absoluteTick;
                event.status = status;

                if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90 ||
                    (status & 0xF0) == 0xA0 || (status & 0xF0) == 0xB0 ||
                    (status & 0xF0) == 0xE0) {
                    if (!file.read(reinterpret_cast<char*>(&event.data1), 1) ||
                        !file.read(reinterpret_cast<char*>(&event.data2), 1)) break;
                    event.data1 = std::min(event.data1, static_cast<uint8_t>(127)); 
                    event.data2 = std::min(event.data2, static_cast<uint8_t>(127));
                    track.events.push_back(event);
                }
                else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) {
                    if (!file.read(reinterpret_cast<char*>(&event.data1), 1)) break;
                    event.data2 = 0;
                    event.data1 = std::min(event.data1, static_cast<uint8_t>(127));
                    track.events.push_back(event);
                }
                else if (status == 0xF0 || status == 0xF7) {
                    uint32_t length;
                    if (!readVarLen(length) || !validateEventLength(length, trackEnd)) break;
                    event.metaData.resize(length);
                    if (!file.read(reinterpret_cast<char*>(event.metaData.data()), length)) break;
                    track.events.push_back(event);
                }
                else if (status == 0xFF) {
                    parseMetaEvent(event, midiFile, absoluteTick, trackEnd);
                    track.events.push_back(event);
                }
                else {
                    continue;
                }

                if (!validateTrackLength(trackEnd)) {
                    std::cerr << "This MIDI file is potentially corrupted, only valid data were loaded." << std::endl;
                    break;
                }
            }
            midiFile.tracks.push_back(std::move(track));
        }
        file.close();
        return midiFile;
    }
};

MidiFile midi_file;
class TransposeSuggestion {
    static constexpr std::array<std::string_view, 12> NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

public:
    struct VectorHash {
        std::size_t operator()(const std::vector<int>& v) const noexcept {
            std::size_t seed = v.size();
            for (int i : v) {
                seed ^= static_cast<size_t>(i) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
    [[nodiscard]] static constexpr int getPitchClass(int midiNote) noexcept {
        return midiNote % 12;
    }

    std::vector<double> durations;

    [[nodiscard]] std::string estimateKey(const std::vector<int>& notes, const std::vector<double>& durations) const {
        // forgot to add this but, this is the Krumhansl-Schmuckler key finding algorithm 
        std::array<double, 12> major_profile = { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
        std::array<double, 12> minor_profile = { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

        std::vector<double> pitch_class_weighted(12, 0.0);
        double total_duration = std::accumulate(durations.begin(), durations.end(), 0.0);

        for (size_t i = 0; i < notes.size(); ++i) {
            int pc = getPitchClass(notes[i]);
            pitch_class_weighted[pc] += durations[i];
        }

        for (double& value : pitch_class_weighted) {
            value /= total_duration;
        }

        const double third_weight = 1.4;
        const double fifth_weight = 1.2;
        const double seventh_weight = 1.1;

        auto apply_weights = [&](const std::array<double, 12>& profile) {
            std::array<double, 12> weighted_profile = profile;
            for (int i = 0; i < 12; ++i) {
                weighted_profile[(i + 4) % 12] *= third_weight;  // Major third
                weighted_profile[(i + 3) % 12] *= third_weight;  // Minor third
                weighted_profile[(i + 7) % 12] *= fifth_weight;  // Perfect fifth
                weighted_profile[(i + 11) % 12] *= seventh_weight;  // Major seventh
                weighted_profile[(i + 10) % 12] *= seventh_weight;  // Minor seventh
            }
            return weighted_profile;
            };

        auto weighted_major_profile = apply_weights(major_profile);
        auto weighted_minor_profile = apply_weights(minor_profile);

        std::string best_mode;
        double max_correlation = -1.0;
        int best_tonic = -1;

        for (int i = 0; i < 12; ++i) {
            double major_corr = calculateCorrelation(pitch_class_weighted, std::vector<double>(weighted_major_profile.begin(), weighted_major_profile.end()));
            double minor_corr = calculateCorrelation(pitch_class_weighted, std::vector<double>(weighted_minor_profile.begin(), weighted_minor_profile.end()));

            major_corr *= 1.02;

            if (major_corr > max_correlation) {
                max_correlation = major_corr;
                best_tonic = i;
                best_mode = "Major";
            }
            if (minor_corr > max_correlation) {
                max_correlation = minor_corr;
                best_tonic = i;
                best_mode = "Minor";
            }

            std::rotate(weighted_major_profile.begin(), weighted_major_profile.begin() + 11, weighted_major_profile.end());
            std::rotate(weighted_minor_profile.begin(), weighted_minor_profile.begin() + 11, weighted_minor_profile.end());
        }

        keySpecialCases(pitch_class_weighted, best_tonic, best_mode);

        return std::string(NOTE_NAMES[best_tonic]) + " " + best_mode;
    }

    [[nodiscard]] std::string detectGenre(const MidiFile& midiFile, const std::vector<int>& notes, const std::vector<double>& durations) const {
        double tempo = midiFile.tempoChanges.empty() ? 0 : 60000000.0 / midiFile.tempoChanges[0].microsecondsPerQuarter;
        int timeSignatureNumerator = midiFile.timeSignatures.empty() ? 4 : midiFile.timeSignatures[0].numerator;
        int timeSignatureDenominator = midiFile.timeSignatures.empty() ? 4 : midiFile.timeSignatures[0].denominator;

        int instrumentDiversity = calculateInstrumentDiversity(midiFile);
        double totalDuration = std::accumulate(durations.begin(), durations.end(), 0.0);
        double noteDensity = notes.size() / totalDuration;
        double rhythmComplexity = calculateRhythmComplexity(durations);
        double pitchRange = notes.empty() ? 0 : (*std::max_element(notes.begin(), notes.end()) - *std::min_element(notes.begin(), notes.end()));
        double syncopation = calculateSyncopation(durations, notes.size());
        double harmonicComplexity = calculateHarmonicComplexity(notes, durations);

        return determineGenre(tempo, timeSignatureNumerator, instrumentDiversity, noteDensity, rhythmComplexity, pitchRange, syncopation, harmonicComplexity);
    }

    [[nodiscard]] int findBestTranspose(const std::vector<int>& notes, const std::vector<double>& durations, const std::string& detectedKey, const std::string& genre) const {
        const std::vector<int> transposeOptions = { -12, -11, -9, -7, -5, -4, -2, 0, 2, 4, 5, 7, 9, 11, 12 };
        const std::map<std::string, int> keyToIndex = { {"C", 0}, {"C#", 1}, {"D", 2}, {"D#", 3}, {"E", 4}, {"F", 5}, {"F#", 6}, {"G", 7}, {"G#", 8}, {"A", 9}, {"A#", 10}, {"B", 11} };

        auto keyPos = detectedKey.find(" ");
        if (keyPos == std::string::npos) return 0;

        int detectedKeyIndex = keyToIndex.at(detectedKey.substr(0, keyPos));
        bool isMinor = (detectedKey.find("Minor") != std::string::npos);

        auto calculateTransposeScore = [&](int transpose) -> double {
            double score = 0.0;
            int minNote = *std::min_element(notes.begin(), notes.end()) + transpose;
            int maxNote = *std::max_element(notes.begin(), notes.end()) + transpose;

            if (minNote < 21 || maxNote > 108) return -2000.0;
            double idealCenter = 60.0;
            double actualCenter = (minNote + maxNote) / 2.0;
            double middleRangeScore = -std::abs(idealCenter - actualCenter) * 3.0;
            score += middleRangeScore;

            int newKeyIndex = (detectedKeyIndex + transpose + 12) % 12;
            int keySignatureComplexity = getKeySignatureComplexity(newKeyIndex);
            double keyComplexityScore = (7.0 - keySignatureComplexity) * 1.0;
            score += keyComplexityScore;

            auto chordProgression = analyzeChordProgression(notes, durations);
            double genreScore = calculateGenreSpecificScore(newKeyIndex, keySignatureComplexity, chordProgression, genre, transpose) * 0.5; // fine tune yes
            score += genreScore;

            double entropyScore = calculateNoteDistributionEntropy(notes, transpose) * 6.0;
            score += entropyScore;

            double playabilityScore = calculatePlayabilityScore(newKeyIndex, transpose) * 2.0;
            score += playabilityScore;

            double rhythmComplexity = calculateRhythmComplexity(durations);
            double rhythmScore = rhythmComplexity * 10.0;
            score += rhythmScore;


            double intervalScore = calculateIntervalComplexity(notes, transpose) * 8.0;
            score += intervalScore;
            if (transpose > 0) {
                score += 5.0;
            }
            return score;
            };

        int bestTranspose = 0;
        double bestScore = -std::numeric_limits<double>::infinity();

        for (int transpose : transposeOptions) {
            double score = calculateTransposeScore(transpose);
            if (score > bestScore) {
                bestScore = score;
                bestTranspose = transpose;
            }
        }
        return bestTranspose;
    }

    [[nodiscard]] int getKeySignatureComplexity(int keyIndex) const {
        const std::vector<int> complexityLookup = { 0, 5, 2, 7, 4, 1, 6, 3, 8, 5, 2, 7 };
        return complexityLookup[keyIndex];
    }
    [[nodiscard]] double calculateIntervalComplexity(const std::vector<int>& notes, int transpose) const {
        std::vector<int> intervals;
        for (size_t i = 1; i < notes.size(); ++i) {
            int64_t interval = static_cast<int64_t>(notes[i]) + transpose - (static_cast<int64_t>(notes[i - 1]) + transpose);
            intervals.push_back(std::abs(static_cast<int>(interval)));
        }

        double complexity = 0.0;
        std::set<int> uniqueIntervals;

        for (int interval : intervals) {
            uniqueIntervals.insert(interval);

            if (interval == 1 || interval == 2) complexity += 0.5;  // Minor 2nd  Major 2nd
            else if (interval == 3 || interval == 4) complexity += 1.0;  // Minor 3rd Major 3rd
            else if (interval == 5) complexity += 1.5;  // Perfect 4th
            else if (interval == 6) complexity += 2.0;  // Tritone
            else if (interval == 7) complexity += 1.5;  // Perfect 5th
            else if (interval == 8 || interval == 9) complexity += 1.75;  // Minor 6th Major 6th
            else if (interval == 10 || interval == 11) complexity += 2.0;  // Minor 7th Major 7th
            else complexity += 2.5;  // Octave or larger
        }
        complexity += uniqueIntervals.size() * 1.9; // reduced weight to fix issues with some midi files
        complexity /= notes.size();

        return complexity;
    }
    [[nodiscard]] std::pair<std::vector<int>, std::vector<double>> extractNotesAndDurations(const MidiFile& midiFile) const {
        std::vector<int> notes;
        std::vector<double> durations;

        for (const auto& track : midiFile.tracks) {
            std::map<int, double> activeNotes;
            double currentTime = 0.0;
            double tempo = 500000;
            double ticksPerQuarterNote = midiFile.division;
            double lastTick = 0.0;

            for (const auto& event : track.events) {
                if (event.status == 0xFF && event.data1 == 0x51) {
                    if (event.metaData.size() == 3) {
                        tempo = (event.metaData[0] << 16) | (event.metaData[1] << 8) | event.metaData[2];
                    }
                }

                double deltaTime = static_cast<double>(event.absoluteTick - lastTick);
                currentTime += deltaTime * (tempo / 1000000.0) / ticksPerQuarterNote;
                lastTick = event.absoluteTick;
                if ((event.status & 0xF0) == 0x90 && event.data2 > 0) {
                    activeNotes[event.data1] = currentTime;
                }
                else if ((event.status & 0xF0) == 0x80 || ((event.status & 0xF0) == 0x90 && event.data2 == 0)) {
                    auto it = activeNotes.find(event.data1);
                    if (it != activeNotes.end()) {
                        double duration = currentTime - it->second;
                        notes.push_back(event.data1);
                        durations.push_back(duration);
                        activeNotes.erase(it);
                    }
                }
            }
        }

        return { notes, durations };
    }
private:
    void keySpecialCases(const std::vector<double>& pitch_class_weighted, int& best_tonic, std::string& best_mode) const {
        if (best_tonic == 7) {  // G
            double f_natural = pitch_class_weighted[5];  // F
            double f_sharp = pitch_class_weighted[6];    // F#
            if (f_sharp > f_natural * 1.2) {
                best_mode = "Major";
            }
            else if (f_natural > f_sharp * 1.2) {
                best_mode = "Minor";
            }
        }
        else if (best_tonic == 9) {  // A
            double c_natural = pitch_class_weighted[0];  // C
            double c_sharp = pitch_class_weighted[1];    // C#
            double g_natural = pitch_class_weighted[7];  // G
            double g_sharp = pitch_class_weighted[8];    // G#
            if (c_natural > c_sharp * 1.1 && g_natural > g_sharp * 1.1) {
                best_mode = "Minor";
            }
            else if (c_sharp > c_natural * 1.1 && g_sharp > g_natural * 1.1) {
                best_mode = "Major";
            }
        }
    }

    [[nodiscard]] int calculateInstrumentDiversity(const MidiFile& midiFile) const {
        std::set<uint8_t> uniqueInstruments;
        for (const auto& track : midiFile.tracks) {
            for (const auto& event : track.events) {
                if ((event.status & 0xF0) == 0xC0) {
                    uniqueInstruments.insert(event.data1);
                }
            }
        }
        return uniqueInstruments.size();
    }
    [[nodiscard]] double calculateRhythmComplexity(const std::vector<double>& durations) const {
        if (durations.size() <= 1) {
            return 1.0;
        }

        std::vector<double> intervalRatios;
        for (size_t i = 1; i < durations.size(); ++i) {
            if (durations[i - 1] > 0) {
                intervalRatios.push_back(durations[i] / durations[i - 1]);
            }
            else {
                intervalRatios.push_back(1.0);
            }
        }

        double sum = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0);
        double mean = intervalRatios.empty() ? 0.0 : sum / intervalRatios.size();


        double variance = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0,
            [mean](double acc, double ratio) {
                double diff = ratio - mean;
                return acc + diff * diff;
            }) / intervalRatios.size();

        double stdDev = std::sqrt(variance);
        return std::min(stdDev, 10.0);
    }


    [[nodiscard]] double calculateSyncopation(const std::vector<double>& durations, size_t noteCount) const {
        double syncopation = 0.0;
        for (const auto& duration : durations) {
            double beatPosition = std::fmod(duration, 1.0);
            if (beatPosition > 0.25 && beatPosition < 0.75) {
                syncopation += 1.0;
            }
        }
        return noteCount == 0 ? 0 : syncopation / noteCount;
    }

    [[nodiscard]] double calculateHarmonicComplexity(const std::vector<int>& notes, const std::vector<double>& durations) const {
        auto chordProgression = analyzeChordProgression(notes, durations);
        std::set<std::string> uniqueChords;
        for (const auto& [chord, _] : chordProgression) {
            uniqueChords.insert(chord);
        }
        return chordProgression.empty() ? 0 : static_cast<double>(uniqueChords.size()) / chordProgression.size();
    }

    [[nodiscard]] std::string determineGenre(double tempo, int timeSignatureNumerator, int instrumentDiversity, double noteDensity, double rhythmComplexity, double pitchRange, double syncopation, double harmonicComplexity) const {
        if (tempo >= 60 && tempo <= 80 && timeSignatureNumerator == 4 && pitchRange >= 48 && harmonicComplexity > 0.6) {
            if (harmonicComplexity > 0.8 && rhythmComplexity > 1.3) return "Romantic Piano";
            else if (harmonicComplexity > 0.7) return "Classical Piano";
            else return "Baroque Piano";
        }
        else if (tempo >= 100 && tempo <= 160 && noteDensity > 4 && rhythmComplexity > 1.2 && harmonicComplexity > 0.7) {
            if (tempo >= 140 && syncopation > 0.5) return "Bebop Piano";
            else if (harmonicComplexity > 0.8) return "Modal Jazz Piano";
            else return "Cool Jazz Piano";
        }
        else if (tempo >= 120 && tempo <= 140 && timeSignatureNumerator == 4 && noteDensity <= 3 && harmonicComplexity < 0.5) {
            return "Pop Piano";
        }
        else if (tempo >= 60 && tempo <= 100 && timeSignatureNumerator == 3) {
            return "Waltz Piano";
        }
        else if (tempo >= 140 && noteDensity > 5 && rhythmComplexity > 1.5 && pitchRange >= 36 && syncopation > 0.3) {
            return "Rock Piano";
        }
        else if (tempo >= 70 && tempo <= 130 && syncopation > 0.4 && harmonicComplexity > 0.6) {
            if (tempo < 100) return "Blues Piano";
            else return "Boogie-Woogie Piano";
        }
        else if (tempo >= 120 && tempo <= 135 && timeSignatureNumerator == 4 && syncopation > 0.5) {
            return "Ragtime Piano";
        }
        else if (tempo >= 60 && tempo <= 90 && noteDensity <= 2 && harmonicComplexity < 0.4) {
            return "Ambient Piano";
        }
        else if (tempo >= 100 && tempo <= 130 && rhythmComplexity > 1.3 && syncopation > 0.4) {
            return "Latin Jazz Piano";
        }
        else if (tempo >= 120 && tempo <= 140 && noteDensity > 4 && rhythmComplexity > 1.4 && harmonicComplexity > 0.7) {
            return "Fusion Piano";
        }
        else if (tempo >= 60 && tempo <= 80 && noteDensity <= 2 && harmonicComplexity < 0.3) {
            return "New Age Piano";
        }
        else if (tempo >= 100 && tempo <= 130 && timeSignatureNumerator % 2 != 0 && rhythmComplexity > 1.3) {
            return "Contemporary Classical Piano";
        }
        else if (harmonicComplexity > 0.9 && rhythmComplexity > 1.6) {
            return "Avant-Garde Piano";
        }
        else if (tempo >= 80 && tempo <= 110 && harmonicComplexity > 0.5 && rhythmComplexity > 1.0) {
            return "Impressionist Piano";
        }
        else if (tempo >= 120 && tempo <= 150 && syncopation > 0.6 && harmonicComplexity > 0.5) {
            return "Stride Piano";
        }
        else if (tempo >= 90 && tempo <= 120 && noteDensity > 3 && harmonicComplexity > 0.4) {
            return "Singer-Songwriter Piano";
        }
        else if (tempo >= 60 && tempo <= 100 && harmonicComplexity < 0.4 && rhythmComplexity < 0.8) {
            return "Minimalist Piano";
        }
        else {
            return "Other Piano Style";
        }
    }

    [[nodiscard]] std::string getChord(const std::set<int>& notes) const {
        std::vector<int> intervals;
        for (auto it = std::next(notes.begin()); it != notes.end(); ++it) {
            intervals.push_back((*it - *notes.begin() + 12) % 12);
        }
        std::sort(intervals.begin(), intervals.end());
        //FUCK
        const std::unordered_map<std::vector<int>, std::string, VectorHash> chordTypes = {
            {{3, 7}, "Minor"}, {{4, 7}, "Major"}, {{3, 6}, "Diminished"}, {{4, 8}, "Augmented"},
            {{2, 7}, "Suspended 2nd"}, {{5, 7}, "Suspended 4th"}, {{3, 7, 10}, "Minor 7th"},
            {{4, 7, 10}, "Dominant 7th"}, {{4, 7, 11}, "Major 7th"}, {{3, 6, 9}, "Diminished 7th"},
            {{3, 7, 11}, "Minor Major 7th"}, {{4, 7, 9}, "6th"}, {{3, 7, 9}, "Minor 6th"},
            {{2, 4, 7}, "Major Add 9"}, {{2, 3, 7}, "Minor Add 9"}, {{4, 7, 10, 13}, "9th"},
            {{3, 7, 10, 14}, "Minor 9th"}, {{4, 7, 11, 14}, "Major 9th"}, {{4, 7, 10, 13, 17}, "11th"},
            {{3, 7, 10, 14, 17}, "Minor 11th"}, {{4, 7, 11, 14, 17}, "Major 11th"}, {{4, 7, 10, 13, 17, 21}, "13th"},
            {{3, 7, 10, 14, 17, 21}, "Minor 13th"}, {{4, 7, 11, 14, 17, 21}, "Major 13th"}, {{4, 7, 10, 14}, "Dominant 9th"},
            {{3, 6, 9, 14}, "Diminished 9th"}, {{4, 8, 11}, "Augmented Major 7th"}, {{4, 7, 10, 14, 18}, "Dominant 13th"},
            {{3, 7, 10, 13}, "Minor 7th Flat 5"}, {{3, 6, 9, 12}, "Half Diminished 7th"}, {{4, 7, 9, 14}, "6/9"},
            {{3, 7, 10, 13, 16}, "Minor 11th Flat 5"}, {{3, 6, 9, 13, 16}, "Diminished 11th"}, {{4, 7, 11, 14, 18}, "Major 13th Flat 9"},
            {{4, 7, 10, 13, 16}, "Dominant 7th Sharp 11"}, {{4, 7, 10, 13, 15}, "Dominant 7th Flat 9"},
            {{4, 7, 10, 13, 15, 21}, "Dominant 13th Flat 9"}, {{4, 7, 10, 13, 18, 21}, "Dominant 13th Sharp 11"},
            {{3, 7, 10, 14, 17, 20}, "Minor 13th Flat 5"}, {{4, 8, 10, 14}, "Augmented 9th"}, {{4, 8, 10, 14, 18}, "Augmented 13th"},
            {{3, 6, 10}, "Diminished Major 7th"}, {{2, 5, 7}, "Suspended 2nd 4th"}, {{1, 5, 7}, "Phrygian"},
            {{2, 6, 9}, "Lydian Augmented"}, {{1, 4, 7}, "Neapolitan"}, {{3, 6, 8}, "Whole Tone"},
            {{2, 4, 6, 8}, "Quartal"}, {{2, 5, 7, 11}, "So What"}, {{4, 7, 10, 15}, "Dominant 7th Flat 13"},
            {{4, 7, 10, 16}, "Dominant 7th Sharp 9"}, {{4, 7, 10, 13, 15, 20}, "Dominant 13th Flat 9 Sharp 11"},
            {{4, 7, 10, 13, 17, 20}, "Dominant 13th Sharp 9 Flat 11"}, {{3, 6, 10, 14}, "Diminished 9th"},
            {{3, 6, 9, 14, 17}, "Diminished 11th"}, {{4, 8, 10, 13}, "Augmented 7th"}, {{4, 8, 10, 14, 17}, "Augmented 9th 11th"},
            {{3, 6, 10, 13}, "Diminished 7th 9th"}, {{3, 6, 9, 12, 15}, "Diminished 11th Flat 13"},
            {{4, 7, 10, 13, 15, 18}, "Dominant 7th 9th Sharp 11"}, {{4, 7, 10, 13, 16, 18}, "Dominant 7th 9th Flat 13"},
            {{4, 7, 11, 14, 18, 21}, "Major 9th 13th Sharp 11"}, {{3, 7, 10, 13, 16, 19}, "Minor 7th 11th Flat 13"},
            {{3, 6, 9, 13, 17}, "Diminished 11th Flat 13"}, {{4, 8, 11, 15}, "Augmented 7th Sharp 9"},
            {{4, 7, 11, 14, 17, 20}, "Major 9th 11th Flat 13"}, {{4, 7, 10, 13, 17, 19}, "Dominant 13th 11th Flat 13"},
            {{3, 6, 9, 12, 15, 18}, "Diminished 13th Sharp 11"}, {{4, 8, 10, 13, 16}, "Augmented 11th Flat 13"},
            {{4, 8, 10, 14, 18, 21}, "Augmented 13th 9th Sharp 11"}, {{4, 7, 10, 14, 17, 20}, "Dominant 7th 9th 13th Flat 11"},
            {{3, 7, 10, 14, 18}, "Minor 9th 11th Flat 13"}, {{3, 6, 9, 13, 16, 19}, "Diminished 7th 11th Flat 13"},
            {{4, 7, 11, 15, 18}, "Major 7th 9th Sharp 11"}, {{4, 8, 11, 14, 17}, "Augmented 9th 11th Flat 13"},
            {{4, 7, 10, 14, 17, 21}, "Dominant 7th 9th 13th Sharp 11"}, {{4, 7, 10, 13, 17}, "Dominant 11th Flat 13"},
            {{3, 6, 10, 14, 17, 20}, "Diminished 7th 9th 11th"}, {{4, 8, 11, 15, 18}, "Augmented 7th 9th 11th Flat 13"},
            {{4, 7, 11, 14, 18, 21}, "Major 9th 11th 13th Sharp 11"}, {{3, 6, 9, 13, 17, 21}, "Diminished 7th 11th 13th Sharp 11"},
            {{4, 8, 11, 14, 17, 21}, "Augmented 9th 11th 13th Sharp 11"}, {{4, 7, 10, 14, 17, 21, 24}, "Dominant 13th 9th 11th Flat 13"},
            {{3, 6, 9, 12, 15, 18, 21}, "Diminished 13th 11th Sharp 9"}, {{4, 7, 10, 13, 17, 20}, "Dominant 11th 13th Flat 9 Sharp 11"},
            {{4, 7, 10, 13, 17, 19, 22}, "Dominant 13th 9th 11th Sharp 11"}, {{3, 7, 10, 13, 17, 20}, "Minor 11th 13th Flat 9"},
            {{3, 6, 9, 13, 17, 21, 24}, "Diminished 13th 11th 9th Sharp 11"}, {{4, 8, 11, 14, 17, 21, 24}, "Augmented 13th 11th 9th Sharp 11"},
            {{4, 7, 10, 14, 18, 21, 24}, "Dominant 13th 9th 11th Sharp 13 Flat 9"}, {{3, 7, 11, 14, 17, 21}, "Minor Major 7th 9th 11th 13th"},
            {{3, 6, 9, 12, 16, 20}, "Half Diminished 11th 13th"}, {{4, 7, 10, 13, 18, 21, 24}, "Dominant 13th 11th 9th Sharp 13 Flat 11"},
            {{4, 8, 11, 15, 18, 22}, "Augmented Major 7th 9th 11th Sharp 13"}, {{4, 7, 11, 14, 18, 21, 24}, "Major 9th 11th 13th Sharp 11 Flat 9"},
            {{3, 7, 10, 14, 17, 20, 24}, "Minor 9th 11th 13th Flat 5 Sharp 13"}, {{3, 6, 9, 13, 16, 19, 24}, "Diminished 7th 9th 11th 13th Sharp 11"},
            {{4, 8, 11, 14, 18, 21, 24}, "Augmented 9th 11th 13th Sharp 11 Flat 9"}, {{4, 7, 10, 14, 17, 21, 25}, "Dominant 13th 9th 11th 13th Sharp 11 Flat 9"},
            {{3, 7, 10, 14, 18, 21}, "Minor 9th 11th 13th Flat 5 Sharp 9"}, {{3, 6, 9, 13, 17, 20, 24}, "Diminished 11th 13th 9th Sharp 11"},
            {{4, 7, 11, 14, 17, 20, 24}, "Major 9th 11th 13th Sharp 11 Flat 13"}, {{4, 8, 11, 14, 18, 21, 25}, "Augmented 13th 9th 11th 13th Sharp 11 Flat 9"},
            {{4, 7, 10, 14, 17, 21, 24, 27}, "Dominant 13th 9th 11th 13th Flat 5 Sharp 11 Flat 9"},{{2, 6, 9, 13}, "Lydian 7th"},
            //MORE FUCKING chords

            {{1, 5, 8, 12}, "Phrygian 9th"},
            {{3, 7, 11, 14, 17, 21, 25}, "Minor Major 13th"},
            {{4, 7, 11, 14, 17, 20, 24}, "Major 13th Flat 5"},
            {{4, 8, 11, 15, 18, 22, 26}, "Augmented Major 13th"},
            {{3, 6, 9, 12, 15, 18, 21, 24}, "Diminished 13th Flat 9"},
            {{4, 7, 10, 13, 16, 19, 22, 25}, "Dominant 13th Flat 9 Flat 11"},
            {{3, 7, 10, 13, 17, 20, 24, 27}, "Minor 13th Sharp 11"},
            {{2, 5, 8, 11, 14, 17, 21}, "Quartal Major 13th"},
            {{3, 6, 9, 12, 16, 19, 23}, "Half Diminished 13th Flat 9"},
            {{4, 7, 11, 14, 17, 21, 24, 27}, "Major 13th Flat 9 Sharp 11"},
            {{3, 7, 10, 14, 18, 21, 25}, "Minor 13th 9th Sharp 11"},
            {{2, 5, 8, 11, 14, 17, 20, 23}, "Suspended 13th 9th"},
            {{1, 4, 7, 10, 13, 16, 19, 22}, "Phrygian 13th"},
            {{3, 6, 9, 13, 17, 20, 23, 27}, "Diminished 13th Sharp 11 Flat 9"},
            {{4, 8, 11, 14, 18, 21, 24, 27}, "Augmented 13th Sharp 11"},
            {{4, 7, 10, 14, 17, 21, 25, 28}, "Dominant 13th 9th Flat 5 Sharp 11"},
            {{3, 7, 10, 14, 18, 21, 25, 28}, "Minor 13th 11th Sharp 9"},
            {{2, 5, 8, 12, 15, 19}, "Quartal 11th 13th"},
            {{4, 8, 11, 15, 18, 21, 25, 28}, "Augmented 13th 11th 9th Flat 5"},
            {{4, 7, 11, 14, 18, 21, 25, 29}, "Major 13th 11th Flat 9 Sharp 13"},
            {{3, 7, 10, 14, 18, 22, 25, 29}, "Minor 13th 11th 9th Flat 5"},
            {{4, 7, 10, 13, 17, 21, 24, 27}, "Dominant 13th Sharp 9 Flat 11"},
            {{3, 6, 9, 12, 16, 20, 23, 26}, "Half Diminished 13th Sharp 11"},
            {{4, 8, 11, 14, 18, 21, 25, 28}, "Augmented 13th 9th Sharp 5"},
            {{4, 7, 11, 15, 18, 21, 25, 28}, "Major 13th 11th Sharp 9 Flat 5"},
            {{3, 6, 10, 13, 17, 21, 25, 28}, "Diminished 13th 11th 9th"},
            {{4, 8, 11, 15, 19, 22, 25, 29}, "Augmented 13th 11th Sharp 9 Flat 5"},
            {{3, 7, 10, 13, 17, 20, 24, 27}, "Minor 13th 11th Sharp 9 Flat 5"},
            {{4, 7, 10, 13, 17, 21, 25, 28}, "Dominant 13th 11th 9th Flat 5 Sharp 13"},
            {{4, 8, 11, 15, 18, 22, 25, 29}, "Augmented Major 13th 11th Sharp 9"},
            {{3, 7, 11, 14, 17, 21, 24, 27}, "Minor Major 13th 11th 9th Sharp 5"},
            {{4, 7, 11, 14, 18, 22, 25, 29}, "Major 13th 11th 9th Flat 5 Sharp 13"},
            {{3, 7, 10, 14, 17, 21, 24, 27}, "Minor 13th 9th Sharp 11 Flat 5"},
            {{4, 7, 10, 13, 17, 21, 25, 28, 31}, "Dominant 13th 11th 9th Sharp 13 Flat 5"},
            {{3, 6, 9, 12, 15, 18, 21, 25, 28}, "Diminished 13th 11th 9th Flat 5 Sharp 11"},
            {{4, 8, 11, 14, 18, 21, 25, 29, 32}, "Augmented 13th 11th 9th Sharp 13 Flat 5"},
            {{3, 7, 10, 13, 17, 20, 24, 27, 31}, "Minor 13th 11th 9th Flat 5 Sharp 11"},
            {{4, 7, 11, 14, 18, 21, 25, 28, 32}, "Major 13th 11th 9th Sharp 13 Flat 5"},
            {{3, 7, 11, 14, 17, 21, 24, 27, 31}, "Minor Major 13th 11th 9th Flat 5 Sharp 11"},
            {{4, 7, 10, 13, 17, 21, 25, 28, 32}, "Dominant 13th 11th 9th Sharp 5 Flat 13"},
            {{4, 8, 11, 14, 18, 21, 25, 29, 32}, "Augmented 13th 11th 9th Flat 5 Sharp 13"},
            {{3, 7, 10, 13, 17, 21, 24, 28, 32}, "Minor 13th 11th 9th Sharp 5 Flat 13"},
            {{4, 7, 11, 14, 18, 22, 25, 29, 32}, "Major 13th 11th 9th Flat 5 Sharp 11 Flat 13"},
            {{4, 8, 11, 14, 18, 21, 25, 29, 32, 36}, "Augmented 13th 11th 9th Sharp 5 Flat 9"},
            {{3, 7, 10, 14, 17, 21, 24, 27, 31, 34}, "Minor 13th 11th 9th Flat 5 Sharp 11 Flat 13"},
            {{4, 7, 10, 13, 17, 21, 24, 28, 32, 36}, "Dominant 13th 11th 9th Sharp 5 Flat 11 Sharp 13"},
            {{4, 7, 11, 14, 18, 21, 25, 29, 32, 36}, "Major 13th 11th 9th Flat 5 Sharp 11 Sharp 13"},
            {{3, 7, 11, 14, 17, 21, 24, 28, 32, 36}, "Minor Major 13th 11th 9th Flat 5 Sharp 11 Sharp 13"},
            {{4, 8, 11, 14, 18, 21, 25, 29, 32, 36}, "Augmented Major 13th 11th 9th Flat 5 Sharp 11 Sharp 13"},
            {{3, 6, 10, 13, 17, 21, 24, 28, 32, 36}, "Diminished 13th 11th 9th Sharp 5 Flat 11"},
            {{4, 7, 11, 14, 18, 22, 25, 29, 32, 36}, "Major 13th 11th 9th Flat 5 Sharp 11 Flat 9 Sharp 13"},
            {{3, 7, 10, 14, 17, 21, 24, 28, 32, 36}, "Minor 13th 11th 9th Flat 5 Sharp 11 Flat 13 Sharp 5"},
            {{4, 8, 11, 14, 18, 22, 25, 29, 32, 36, 40}, "Augmented Major 13th 11th 9th Flat 5 Sharp 11 Flat 9"},
            {{4, 7, 10, 13, 17, 21, 24, 28, 32, 36, 40}, "Dominant 13th 11th 9th Flat 5 Sharp 11 Sharp 13 Flat 9"},
            {{3, 6, 9, 12, 16, 20, 23, 27, 31, 34}, "Half Diminished 13th 11th 9th Flat 5 Sharp 11"},
            {{4, 7, 10, 13, 16, 19, 22, 25, 29, 32, 36}, "Dominant 13th 11th 9th Sharp 5 Flat 13 Flat 11"},
            {{4, 8, 11, 14, 18, 22, 25, 29, 32, 36, 40}, "Augmented Major 13th 11th 9th Flat 5 Sharp 11 Sharp 13 Flat 9"},
            {{3, 7, 10, 13, 17, 20, 24, 27, 31, 34}, "Minor 13th 11th 9th Flat 5 Sharp 11 Flat 13 Sharp 11"},
            {{4, 7, 11, 14, 18, 21, 25, 29, 32, 36, 40}, "Major 13th 11th 9th Flat 5 Sharp 11 Sharp 13 Flat 9"},
            {{3, 6, 10, 13, 17, 20, 24, 28, 32, 36, 40}, "Diminished 13th 11th 9th Sharp 5 Flat 11 Sharp 13 Flat 9"}
        };

        auto it = chordTypes.find(intervals);
        if (it != chordTypes.end()) {
            return it->second;
        }

        int bestMatch = 0;
        std::string bestChord = "Unknown";
        for (const auto& [chordIntervals, chordName] : chordTypes) {
            int matches = std::count_if(intervals.begin(), intervals.end(), [&](int interval) {
                return std::find(chordIntervals.begin(), chordIntervals.end(), interval) != chordIntervals.end();
                });
            if (matches > bestMatch) {
                bestMatch = matches;
                bestChord = chordName;
            }
        }
        return bestChord;
    }

    [[nodiscard]] std::vector<std::pair<std::string, double>> analyzeChordProgression(const std::vector<int>& notes, const std::vector<double>& durations) const {
        std::vector<std::pair<std::string, double>> progression;
        std::set<int> currentChord;
        double chordDuration = 0.0;
        const double chordThreshold = 0.25;

        for (size_t i = 0; i < notes.size(); ++i) {
            int pitchClass = getPitchClass(notes[i]);
            currentChord.insert(pitchClass);
            chordDuration += durations[i];

            if (chordDuration >= chordThreshold || i == notes.size() - 1) {
                progression.push_back({ getChord(currentChord), chordDuration });
                currentChord.clear();
                chordDuration = 0.0;
            }
        }

        return progression;
    }

    [[nodiscard]] double calculateCorrelation(const std::vector<double>& v1, const std::vector<double>& v2) const {
        double mean_v1 = std::accumulate(v1.begin(), v1.end(), 0.0) / v1.size();
        double mean_v2 = std::accumulate(v2.begin(), v2.end(), 0.0) / v2.size();

        double numerator = 0.0;
        double denom_v1 = 0.0;
        double denom_v2 = 0.0;

        for (size_t i = 0; i < v1.size(); ++i) {
            numerator += (v1[i] - mean_v1) * (v2[i] - mean_v2);
            denom_v1 += (v1[i] - mean_v1) * (v1[i] - mean_v1);
            denom_v2 += (v2[i] - mean_v2) * (v2[i] - mean_v2);
        }

        return numerator / std::sqrt(denom_v1 * denom_v2);
    }


    [[nodiscard]] double calculateGenreSpecificScore(int newKeyIndex, int keySignatureComplexity, const std::vector<std::pair<std::string, double>>& chordProgression, const std::string& genre, int transpose) const {
        double score = 0.0;

        std::vector<int> commonClassicalKeys = { 0, 2, 4, 5, 7, 9, 11 };
        std::vector<int> commonRomanticKeys = { 0, 4, 5, 7, 11 };
        std::vector<int> commonJazzKeys = { 0, 5, 7, 10, 3 };
        std::vector<int> commonPopKeys = { 0, 5, 7, 2, 9 };
        std::vector<int> commonBluesKeys = { 0, 5, 7, 10 };
        std::vector<int> commonImpressionistKeys = { 0, 5, 7, 10, 2, 4 };

        auto scoreCommonKeys = [&](const std::vector<int>& keys, double baseScore) {
            if (std::find(keys.begin(), keys.end(), newKeyIndex) != keys.end()) {
                score += baseScore;
            }
            };

        auto scoreKeyComplexity = [&](int idealComplexity, double weight) {
            score += (4 - std::abs(keySignatureComplexity - idealComplexity)) * weight;
            };

        auto scoreChordProgression = [&](int idealLength, double weight) {
            score += (8 - std::abs(static_cast<int>(chordProgression.size()) - idealLength)) * weight;
            };
        if (genre == "Baroque Piano" || genre == "Bach-style") {
            scoreCommonKeys(commonClassicalKeys, 8.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Classical Piano" || genre == "Mozart-style") {
            scoreCommonKeys(commonClassicalKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(5, 2.0);
        }
        else if (genre == "Early Romantic Piano" || genre == "Beethoven-style") {
            scoreCommonKeys(commonRomanticKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Romantic Piano" || genre == "Chopin-style") {
            scoreCommonKeys(commonRomanticKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(7, 1.2);
        }
        else if (genre == "Late Romantic Piano" || genre == "Liszt-style") {
            scoreCommonKeys(commonRomanticKeys, 6.5);
            scoreKeyComplexity(4, 1.5);
            scoreChordProgression(8, 1.0);
        }
        else if (genre == "Impressionist Piano" || genre == "Debussy-style") {
            scoreCommonKeys(commonImpressionistKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "20th Century Classical Piano") {
            scoreKeyComplexity(5, 1.0);
            scoreChordProgression(9, 0.8);
        }
        else if (genre == "Minimalist Piano") {
            scoreCommonKeys(commonClassicalKeys, 7.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(3, 2.5);
        }
        else if (genre == "Ragtime Piano") {
            scoreCommonKeys(commonJazzKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Stride Piano") {
            scoreCommonKeys(commonJazzKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Boogie-Woogie Piano") {
            scoreCommonKeys(commonBluesKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(3, 2.5);
        }
        else if (genre == "Bebop Piano") {
            scoreCommonKeys(commonJazzKeys, 6.5);
            scoreKeyComplexity(4, 1.5);
            scoreChordProgression(8, 1.0);
        }
        else if (genre == "Cool Jazz Piano") {
            scoreCommonKeys(commonJazzKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Modal Jazz Piano") {
            scoreCommonKeys(commonJazzKeys, 6.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Free Jazz Piano") {
            scoreKeyComplexity(5, 1.0);
            scoreChordProgression(10, 0.5);
        }
        else if (genre == "Latin Jazz Piano") {
            scoreCommonKeys(commonJazzKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Blues Piano") {
            scoreCommonKeys(commonBluesKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(3, 2.5);
        }
        else if (genre == "New Orleans Piano") {
            scoreCommonKeys(commonBluesKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Soul Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "R&B Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Pop Piano") {
            scoreCommonKeys(commonPopKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Rock Piano") {
            scoreCommonKeys(commonPopKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Power Ballad Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Singer-Songwriter Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Contemporary Classical Piano") {
            scoreKeyComplexity(4, 1.5);
            scoreChordProgression(8, 1.0);
        }
        else if (genre == "Avant-Garde Piano") {
            scoreKeyComplexity(5, 1.0);
            scoreChordProgression(10, 0.5);
        }
        else if (genre == "New Age Piano") {
            scoreCommonKeys(commonImpressionistKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Ambient Piano") {
            scoreCommonKeys(commonImpressionistKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(3, 2.5);
        }
        else if (genre == "Crossover Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Latin Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Afro-Cuban Piano") {
            scoreCommonKeys(commonJazzKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Bossa Nova Piano") {
            scoreCommonKeys(commonJazzKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Celtic Piano") {
            scoreCommonKeys(commonClassicalKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Tango Piano") {
            scoreCommonKeys(commonClassicalKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Educational Piano") {
            scoreCommonKeys(commonClassicalKeys, 8.0);
            scoreKeyComplexity(1, 3.0);
            scoreChordProgression(4, 2.0);
        }
        else if (genre == "Sight-Reading Exercise") {
            scoreCommonKeys(commonClassicalKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Piano Etude") {
            scoreCommonKeys(commonClassicalKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Accompaniment Piano") {
            scoreCommonKeys(commonPopKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "Film Score Piano") {
            scoreCommonKeys(commonImpressionistKeys, 7.0);
            scoreKeyComplexity(3, 2.0);
            scoreChordProgression(6, 1.5);
        }
        else if (genre == "Video Game Piano") {
            scoreCommonKeys(commonPopKeys, 7.0);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(5, 1.8);
        }
        else if (genre == "TV Theme Piano") {
            scoreCommonKeys(commonPopKeys, 7.5);
            scoreKeyComplexity(2, 2.5);
            scoreChordProgression(4, 2.0);
        }
        score = std::max(score, -100.0);
        if (transpose > 0) {
            score += 2.0;
        }
        return score;
    }

    [[nodiscard]] double calculateNoteDistributionEntropy(const std::vector<int>& notes, int transpose) const {
        std::vector<int> noteDistribution(12, 0);
        for (int note : notes) {
            noteDistribution[(note + transpose) % 12]++;
        }

        double entropy = 0.0;
        for (int count : noteDistribution) {
            if (count > 0) {
                double p = static_cast<double>(count) / notes.size();
                entropy -= p * std::log2(p);
            }
        }

        return entropy * 5.0;
    }
    [[nodiscard]] double calculatePlayabilityScore(int newKeyIndex, int transpose) const {
        double score = 0.0;
        std::vector<int> easyKeys = { 0, 7, 5, 2, 9, 4 }; // C, G, F, D, A, E
        if (std::find(easyKeys.begin(), easyKeys.end(), newKeyIndex) != easyKeys.end()) {
            score += 15.0;
        }
        std::vector<int> commonTranspositions = { -5, -4, -2, 0, 2, 4, 5 };
        if (std::find(commonTranspositions.begin(), commonTranspositions.end(), transpose) != commonTranspositions.end()) {
            score += 10.0;
        }
        if (transpose > 0) {
            score += 5.0;
        }

        return score;
    }
    // staying up till 4am fine tuning this was def worth it..
    [[nodiscard]] double calculateHarmonicTension(const std::string& chord1, const std::string& chord2) const {
        const std::unordered_map<std::string, std::unordered_map<std::string, double>> tensionValues = {
                {"Major", {{"Major", 0.0}, {"Minor", 0.2}, {"Diminished", 0.5}, {"Augmented", 0.7}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.6}, {"Dominant 7th", 0.4}, {"Major 7th", 0.1}, {"Diminished 7th", 0.8}, {"Minor Major 7th", 0.5}, {"6th", 0.2}, {"Minor 6th", 0.3}, {"Major Add 9", 0.1}, {"Minor Add 9", 0.3}, {"9th", 0.4}, {"Minor 9th", 0.6}, {"Major 9th", 0.2}, {"11th", 0.5}, {"Minor 11th", 0.7}, {"Major 11th", 0.3}, {"13th", 0.5}, {"Minor 13th", 0.7}, {"Major 13th", 0.3}, {"Dominant 9th", 0.4}, {"Diminished 9th", 0.8}, {"Augmented Major 7th", 0.9}, {"Dominant 13th", 0.6}, {"Minor 7th Flat 5", 0.6}, {"Half Diminished 7th", 0.5}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.7}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.6}}},
                {"Minor", {{"Major", 0.2}, {"Minor", 0.0}, {"Diminished", 0.3}, {"Augmented", 0.6}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.1}, {"Dominant 7th", 0.5}, {"Major 7th", 0.4}, {"Diminished 7th", 0.6}, {"Minor Major 7th", 0.2}, {"6th", 0.5}, {"Minor 6th", 0.2}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.5}, {"Minor 9th", 0.2}, {"Major 9th", 0.6}, {"11th", 0.4}, {"Minor 11th", 0.1}, {"Major 11th", 0.7}, {"13th", 0.6}, {"Minor 13th", 0.2}, {"Major 13th", 0.7}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.7}, {"Augmented Major 7th", 0.9}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.6}, {"Major 13th Flat 9", 0.8}}},
                {"Diminished", {{"Major", 0.5}, {"Minor", 0.3}, {"Diminished", 0.0}, {"Augmented", 0.8}, {"Suspended 2nd", 0.6}, {"Suspended 4th", 0.7}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.6}, {"Major 7th", 0.5}, {"Diminished 7th", 0.1}, {"Minor Major 7th", 0.3}, {"6th", 0.7}, {"Minor 6th", 0.5}, {"Major Add 9", 0.6}, {"Minor Add 9", 0.3}, {"9th", 0.7}, {"Minor 9th", 0.5}, {"Major 9th", 0.8}, {"11th", 0.6}, {"Minor 11th", 0.4}, {"Major 11th", 0.9}, {"13th", 0.8}, {"Minor 13th", 0.5}, {"Major 13th", 0.9}, {"Dominant 9th", 0.7}, {"Diminished 9th", 0.1}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.8}, {"Minor 7th Flat 5", 0.2}, {"Half Diminished 7th", 0.1}, {"6/9", 0.7}, {"Minor 11th Flat 5", 0.2}, {"Diminished 11th", 0.1}, {"Major 13th Flat 9", 0.9}}},
                {"Augmented", {{"Major", 0.7}, {"Minor", 0.6}, {"Diminished", 0.8}, {"Augmented", 0.0}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.6}, {"Minor 7th", 0.7}, {"Dominant 7th", 0.8}, {"Major 7th", 0.6}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.7}, {"6th", 0.8}, {"Minor 6th", 0.6}, {"Major Add 9", 0.7}, {"Minor Add 9", 0.6}, {"9th", 0.8}, {"Minor 9th", 0.7}, {"Major 9th", 0.9}, {"11th", 0.7}, {"Minor 11th", 0.6}, {"Major 11th", 1.0}, {"13th", 0.9}, {"Minor 13th", 0.7}, {"Major 13th", 1.0}, {"Dominant 9th", 0.8}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 0.1}, {"Dominant 13th", 0.9}, {"Minor 7th Flat 5", 0.7}, {"Half Diminished 7th", 0.8}, {"6/9", 0.8}, {"Minor 11th Flat 5", 0.6}, {"Diminished 11th", 0.9}, {"Major 13th Flat 9", 1.0}}},
                {"Suspended 2nd", {{"Major", 0.3}, {"Minor", 0.4}, {"Diminished", 0.6}, {"Augmented", 0.5}, {"Suspended 2nd", 0.0}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.5}, {"Dominant 7th", 0.4}, {"Major 7th", 0.3}, {"Diminished 7th", 0.7}, {"Minor Major 7th", 0.4}, {"6th", 0.3}, {"Minor 6th", 0.5}, {"Major Add 9", 0.2}, {"Minor Add 9", 0.4}, {"9th", 0.4}, {"Minor 9th", 0.5}, {"Major 9th", 0.3}, {"11th", 0.4}, {"Minor 11th", 0.5}, {"Major 11th", 0.3}, {"13th", 0.4}, {"Minor 13th", 0.5}, {"Major 13th", 0.3}, {"Dominant 9th", 0.4}, {"Diminished 9th", 0.7}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.4}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.8}}},
                {"Suspended 4th", {{"Major", 0.4}, {"Minor", 0.3}, {"Diminished", 0.7}, {"Augmented", 0.6}, {"Suspended 2nd", 0.2}, {"Suspended 4th", 0.0}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.3}, {"Major 7th", 0.2}, {"Diminished 7th", 0.6}, {"Minor Major 7th", 0.3}, {"6th", 0.4}, {"Minor 6th", 0.3}, {"Major Add 9", 0.2}, {"Minor Add 9", 0.3}, {"9th", 0.3}, {"Minor 9th", 0.4}, {"Major 9th", 0.2}, {"11th", 0.3}, {"Minor 11th", 0.4}, {"Major 11th", 0.2}, {"13th", 0.3}, {"Minor 13th", 0.4}, {"Major 13th", 0.2}, {"Dominant 9th", 0.3}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.7}, {"Dominant 13th", 0.3}, {"Minor 7th Flat 5", 0.4}, {"Half Diminished 7th", 0.3}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.4}, {"Diminished 11th", 0.6}, {"Major 13th Flat 9", 0.7}}},
                {"Minor 7th", {{"Major", 0.6}, {"Minor", 0.1}, {"Diminished", 0.4}, {"Augmented", 0.7}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.0}, {"Dominant 7th", 0.5}, {"Major 7th", 0.6}, {"Diminished 7th", 0.4}, {"Minor Major 7th", 0.2}, {"6th", 0.5}, {"Minor 6th", 0.3}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.2}, {"9th", 0.3}, {"Minor 9th", 0.2}, {"Major 9th", 0.6}, {"11th", 0.4}, {"Minor 11th", 0.2}, {"Major 11th", 0.7}, {"13th", 0.6}, {"Minor 13th", 0.2}, {"Major 13th", 0.8}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"Dominant 7th", {{"Major", 0.4}, {"Minor", 0.5}, {"Diminished", 0.6}, {"Augmented", 0.8}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.5}, {"Dominant 7th", 0.0}, {"Major 7th", 0.3}, {"Diminished 7th", 0.7}, {"Minor Major 7th", 0.4}, {"6th", 0.5}, {"Minor 6th", 0.4}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.5}, {"9th", 0.1}, {"Minor 9th", 0.3}, {"Major 9th", 0.4}, {"11th", 0.2}, {"Minor 11th", 0.4}, {"Major 11th", 0.5}, {"13th", 0.3}, {"Minor 13th", 0.4}, {"Major 13th", 0.5}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.7}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.3}, {"Minor 7th Flat 5", 0.4}, {"Half Diminished 7th", 0.3}, {"6/9", 0.5}, {"Minor 11th Flat 5", 0.4}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.6}}},
                {"Major 7th", {{"Major", 0.1}, {"Minor", 0.4}, {"Diminished", 0.5}, {"Augmented", 0.6}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.6}, {"Dominant 7th", 0.3}, {"Major 7th", 0.0}, {"Diminished 7th", 0.8}, {"Minor Major 7th", 0.4}, {"6th", 0.2}, {"Minor 6th", 0.3}, {"Major Add 9", 0.1}, {"Minor Add 9", 0.4}, {"9th", 0.2}, {"Minor 9th", 0.4}, {"Major 9th", 0.1}, {"11th", 0.3}, {"Minor 11th", 0.5}, {"Major 11th", 0.1}, {"13th", 0.3}, {"Minor 13th", 0.4}, {"Major 13th", 0.1}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.8}, {"Augmented Major 7th", 0.9}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.6}, {"Half Diminished 7th", 0.5}, {"6/9", 0.2}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.5}}},
                {"Diminished 7th", {{"Major", 0.8}, {"Minor", 0.6}, {"Diminished", 0.1}, {"Augmented", 0.9}, {"Suspended 2nd", 0.7}, {"Suspended 4th", 0.6}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.7}, {"Major 7th", 0.8}, {"Diminished 7th", 0.0}, {"Minor Major 7th", 0.5}, {"6th", 0.9}, {"Minor 6th", 0.6}, {"Major Add 9", 0.8}, {"Minor Add 9", 0.6}, {"9th", 0.7}, {"Minor 9th", 0.5}, {"Major 9th", 0.9}, {"11th", 0.7}, {"Minor 11th", 0.4}, {"Major 11th", 1.0}, {"13th", 0.9}, {"Minor 13th", 0.5}, {"Major 13th", 1.0}, {"Dominant 9th", 0.8}, {"Diminished 9th", 0.1}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.9}, {"Minor 7th Flat 5", 0.2}, {"Half Diminished 7th", 0.1}, {"6/9", 0.8}, {"Minor 11th Flat 5", 0.2}, {"Diminished 11th", 0.1}, {"Major 13th Flat 9", 0.9}}},
                {"Minor Major 7th", {{"Major", 0.5}, {"Minor", 0.2}, {"Diminished", 0.3}, {"Augmented", 0.7}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.2}, {"Dominant 7th", 0.4}, {"Major 7th", 0.4}, {"Diminished 7th", 0.5}, {"Minor Major 7th", 0.0}, {"6th", 0.5}, {"Minor 6th", 0.2}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.5}, {"Minor 9th", 0.2}, {"Major 9th", 0.6}, {"11th", 0.4}, {"Minor 11th", 0.1}, {"Major 11th", 0.7}, {"13th", 0.6}, {"Minor 13th", 0.2}, {"Major 13th", 0.8}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"6th", {{"Major", 0.2}, {"Minor", 0.5}, {"Diminished", 0.7}, {"Augmented", 0.8}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.5}, {"Dominant 7th", 0.5}, {"Major 7th", 0.2}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.5}, {"6th", 0.0}, {"Minor 6th", 0.5}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.5}, {"9th", 0.5}, {"Minor 9th", 0.3}, {"Major 9th", 0.2}, {"11th", 0.4}, {"Minor 11th", 0.5}, {"Major 11th", 0.3}, {"13th", 0.4}, {"Minor 13th", 0.5}, {"Major 13th", 0.3}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.6}, {"6/9", 0.2}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.8}}},
                {"Minor 6th", {{"Major", 0.3}, {"Minor", 0.2}, {"Diminished", 0.5}, {"Augmented", 0.6}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.3}, {"Dominant 7th", 0.4}, {"Major 7th", 0.3}, {"Diminished 7th", 0.6}, {"Minor Major 7th", 0.2}, {"6th", 0.5}, {"Minor 6th", 0.0}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.5}, {"Minor 9th", 0.2}, {"Major 9th", 0.6}, {"11th", 0.4}, {"Minor 11th", 0.1}, {"Major 11th", 0.7}, {"13th", 0.6}, {"Minor 13th", 0.2}, {"Major 13th", 0.8}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"Major Add 9", {{"Major", 0.1}, {"Minor", 0.4}, {"Diminished", 0.6}, {"Augmented", 0.7}, {"Suspended 2nd", 0.2}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.3}, {"Major 7th", 0.1}, {"Diminished 7th", 0.8}, {"Minor Major 7th", 0.4}, {"6th", 0.3}, {"Minor 6th", 0.4}, {"Major Add 9", 0.0}, {"Minor Add 9", 0.3}, {"9th", 0.2}, {"Minor 9th", 0.3}, {"Major 9th", 0.1}, {"11th", 0.3}, {"Minor 11th", 0.4}, {"Major 11th", 0.2}, {"13th", 0.3}, {"Minor 13th", 0.4}, {"Major 13th", 0.2}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.8}, {"Augmented Major 7th", 0.9}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.6}, {"Half Diminished 7th", 0.5}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.4}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.5}}},
                {"Minor Add 9", {{"Major", 0.3}, {"Minor", 0.1}, {"Diminished", 0.3}, {"Augmented", 0.6}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.2}, {"Dominant 7th", 0.5}, {"Major 7th", 0.4}, {"Diminished 7th", 0.5}, {"Minor Major 7th", 0.1}, {"6th", 0.5}, {"Minor 6th", 0.2}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.0}, {"9th", 0.4}, {"Minor 9th", 0.1}, {"Major 9th", 0.5}, {"11th", 0.3}, {"Minor 11th", 0.1}, {"Major 11th", 0.6}, {"13th", 0.5}, {"Minor 13th", 0.1}, {"Major 13th", 0.7}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"9th", {{"Major", 0.4}, {"Minor", 0.5}, {"Diminished", 0.7}, {"Augmented", 0.8}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.3}, {"Dominant 7th", 0.1}, {"Major 7th", 0.2}, {"Diminished 7th", 0.7}, {"Minor Major 7th", 0.5}, {"6th", 0.5}, {"Minor 6th", 0.4}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.4}, {"9th", 0.0}, {"Minor 9th", 0.3}, {"Major 9th", 0.1}, {"11th", 0.2}, {"Minor 11th", 0.4}, {"Major 11th", 0.3}, {"13th", 0.4}, {"Minor 13th", 0.4}, {"Major 13th", 0.3}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.7}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.6}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.6}}},
                {"Minor 9th", {{"Major", 0.6}, {"Minor", 0.2}, {"Diminished", 0.5}, {"Augmented", 0.7}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.2}, {"Dominant 7th", 0.3}, {"Major 7th", 0.4}, {"Diminished 7th", 0.5}, {"Minor Major 7th", 0.2}, {"6th", 0.5}, {"Minor 6th", 0.2}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.3}, {"Minor 9th", 0.0}, {"Major 9th", 0.5}, {"11th", 0.4}, {"Minor 11th", 0.1}, {"Major 11th", 0.6}, {"13th", 0.5}, {"Minor 13th", 0.1}, {"Major 13th", 0.7}, {"Dominant 9th", 0.3}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"Major 9th", {{"Major", 0.2}, {"Minor", 0.5}, {"Diminished", 0.8}, {"Augmented", 0.9}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.6}, {"Dominant 7th", 0.4}, {"Major 7th", 0.1}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.6}, {"6th", 0.2}, {"Minor 6th", 0.3}, {"Major Add 9", 0.1}, {"Minor Add 9", 0.5}, {"9th", 0.1}, {"Minor 9th", 0.5}, {"Major 9th", 0.0}, {"11th", 0.3}, {"Minor 11th", 0.5}, {"Major 11th", 0.2}, {"13th", 0.3}, {"Minor 13th", 0.4}, {"Major 13th", 0.1}, {"Dominant 9th", 0.1}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.7}, {"Half Diminished 7th", 0.6}, {"6/9", 0.2}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.5}}},
                {"11th", {{"Major", 0.5}, {"Minor", 0.4}, {"Diminished", 0.6}, {"Augmented", 0.7}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.2}, {"Major 7th", 0.3}, {"Diminished 7th", 0.6}, {"Minor Major 7th", 0.4}, {"6th", 0.4}, {"Minor 6th", 0.3}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.4}, {"9th", 0.2}, {"Minor 9th", 0.4}, {"Major 9th", 0.3}, {"11th", 0.0}, {"Minor 11th", 0.3}, {"Major 11th", 0.1}, {"13th", 0.2}, {"Minor 13th", 0.3}, {"Major 13th", 0.1}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.6}, {"Augmented Major 7th", 0.7}, {"Dominant 13th", 0.3}, {"Minor 7th Flat 5", 0.4}, {"Half Diminished 7th", 0.5}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.6}, {"Major 13th Flat 9", 0.7}}},
                {"Minor 11th", {{"Major", 0.7}, {"Minor", 0.2}, {"Diminished", 0.4}, {"Augmented", 0.6}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.2}, {"Dominant 7th", 0.4}, {"Major 7th", 0.5}, {"Diminished 7th", 0.4}, {"Minor Major 7th", 0.1}, {"6th", 0.6}, {"Minor 6th", 0.2}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.4}, {"Minor 9th", 0.1}, {"Major 9th", 0.5}, {"11th", 0.3}, {"Minor 11th", 0.0}, {"Major 11th", 0.6}, {"13th", 0.5}, {"Minor 13th", 0.1}, {"Major 13th", 0.7}, {"Dominant 9th", 0.4}, {"Diminished 9th", 0.5}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.3}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"Major 11th", {{"Major", 0.3}, {"Minor", 0.6}, {"Diminished", 0.9}, {"Augmented", 1.0}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.7}, {"Dominant 7th", 0.5}, {"Major 7th", 0.1}, {"Diminished 7th", 1.0}, {"Minor Major 7th", 0.6}, {"6th", 0.3}, {"Minor 6th", 0.5}, {"Major Add 9", 0.2}, {"Minor Add 9", 0.6}, {"9th", 0.3}, {"Minor 9th", 0.5}, {"Major 9th", 0.2}, {"11th", 0.1}, {"Minor 11th", 0.6}, {"Major 11th", 0.0}, {"13th", 0.1}, {"Minor 13th", 0.5}, {"Major 13th", 0.1}, {"Dominant 9th", 0.3}, {"Diminished 9th", 1.0}, {"Augmented Major 7th", 1.1}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.8}, {"Half Diminished 7th", 0.7}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.6}, {"Diminished 11th", 0.9}, {"Major 13th Flat 9", 0.5}}},
                {"13th", {{"Major", 0.5}, {"Minor", 0.6}, {"Diminished", 0.8}, {"Augmented", 0.9}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.6}, {"Dominant 7th", 0.3}, {"Major 7th", 0.3}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.5}, {"6th", 0.4}, {"Minor 6th", 0.6}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.5}, {"9th", 0.4}, {"Minor 9th", 0.5}, {"Major 9th", 0.3}, {"11th", 0.2}, {"Minor 11th", 0.5}, {"Major 11th", 0.1}, {"13th", 0.0}, {"Minor 13th", 0.3}, {"Major 13th", 0.1}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.2}, {"Minor 7th Flat 5", 0.6}, {"Half Diminished 7th", 0.7}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.6}}},
                {"Minor 13th", {{"Major", 0.7}, {"Minor", 0.2}, {"Diminished", 0.5}, {"Augmented", 0.7}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.2}, {"Dominant 7th", 0.4}, {"Major 7th", 0.5}, {"Diminished 7th", 0.5}, {"Minor Major 7th", 0.1}, {"6th", 0.6}, {"Minor 6th", 0.2}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.1}, {"9th", 0.4}, {"Minor 9th", 0.1}, {"Major 9th", 0.5}, {"11th", 0.3}, {"Minor 11th", 0.1}, {"Major 11th", 0.6}, {"13th", 0.3}, {"Minor 13th", 0.0}, {"Major 13th", 0.7}, {"Dominant 9th", 0.4}, {"Diminished 9th", 0.5}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.3}, {"Minor 7th Flat 5", 0.3}, {"Half Diminished 7th", 0.4}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.3}, {"Diminished 11th", 0.5}, {"Major 13th Flat 9", 0.7}}},
                {"Major 13th", {{"Major", 0.3}, {"Minor", 0.7}, {"Diminished", 1.0}, {"Augmented", 1.1}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.2}, {"Minor 7th", 0.8}, {"Dominant 7th", 0.5}, {"Major 7th", 0.1}, {"Diminished 7th", 1.1}, {"Minor Major 7th", 0.7}, {"6th", 0.3}, {"Minor 6th", 0.5}, {"Major Add 9", 0.2}, {"Minor Add 9", 0.6}, {"9th", 0.3}, {"Minor 9th", 0.5}, {"Major 9th", 0.2}, {"11th", 0.1}, {"Minor 11th", 0.6}, {"Major 11th", 0.1}, {"13th", 0.1}, {"Minor 13th", 0.5}, {"Major 13th", 0.0}, {"Dominant 9th", 0.3}, {"Diminished 9th", 1.0}, {"Augmented Major 7th", 1.1}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.8}, {"Half Diminished 7th", 0.7}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.6}, {"Diminished 11th", 0.9}, {"Major 13th Flat 9", 0.5}}},
                {"Dominant 9th", {{"Major", 0.4}, {"Minor", 0.5}, {"Diminished", 0.7}, {"Augmented", 0.8}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.3}, {"Dominant 7th", 0.1}, {"Major 7th", 0.2}, {"Diminished 7th", 0.7}, {"Minor Major 7th", 0.5}, {"6th", 0.5}, {"Minor 6th", 0.4}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.4}, {"9th", 0.2}, {"Minor 9th", 0.4}, {"Major 9th", 0.1}, {"11th", 0.2}, {"Minor 11th", 0.4}, {"Major 11th", 0.3}, {"13th", 0.2}, {"Minor 13th", 0.4}, {"Major 13th", 0.1}, {"Dominant 9th", 0.0}, {"Diminished 9th", 0.7}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.2}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.6}, {"6/9", 0.3}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.6}}},
                {"Diminished 9th", {{"Major", 0.8}, {"Minor", 0.6}, {"Diminished", 0.1}, {"Augmented", 0.9}, {"Suspended 2nd", 0.7}, {"Suspended 4th", 0.6}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.7}, {"Major 7th", 0.8}, {"Diminished 7th", 0.0}, {"Minor Major 7th", 0.5}, {"6th", 0.9}, {"Minor 6th", 0.6}, {"Major Add 9", 0.8}, {"Minor Add 9", 0.6}, {"9th", 0.7}, {"Minor 9th", 0.5}, {"Major 9th", 0.9}, {"11th", 0.7}, {"Minor 11th", 0.4}, {"Major 11th", 1.0}, {"13th", 0.9}, {"Minor 13th", 0.5}, {"Major 13th", 1.0}, {"Dominant 9th", 0.8}, {"Diminished 9th", 0.1}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.9}, {"Minor 7th Flat 5", 0.2}, {"Half Diminished 7th", 0.1}, {"6/9", 0.8}, {"Minor 11th Flat 5", 0.2}, {"Diminished 11th", 0.1}, {"Major 13th Flat 9", 0.9}}},
                {"Augmented Major 7th", {{"Major", 0.9}, {"Minor", 0.8}, {"Diminished", 1.0}, {"Augmented", 0.1}, {"Suspended 2nd", 0.8}, {"Suspended 4th", 0.7}, {"Minor 7th", 0.7}, {"Dominant 7th", 0.8}, {"Major 7th", 0.9}, {"Diminished 7th", 1.0}, {"Minor Major 7th", 0.8}, {"6th", 1.0}, {"Minor 6th", 0.8}, {"Major Add 9", 0.9}, {"Minor Add 9", 0.8}, {"9th", 0.8}, {"Minor 9th", 0.7}, {"Major 9th", 1.0}, {"11th", 0.9}, {"Minor 11th", 0.8}, {"Major 11th", 1.1}, {"13th", 1.0}, {"Minor 13th", 0.7}, {"Major 13th", 1.1}, {"Dominant 9th", 0.8}, {"Diminished 9th", 1.0}, {"Augmented Major 7th", 0.0}, {"Dominant 13th", 1.0}, {"Minor 7th Flat 5", 0.8}, {"Half Diminished 7th", 0.9}, {"6/9", 1.0}, {"Minor 11th Flat 5", 0.7}, {"Diminished 11th", 1.0}, {"Major 13th Flat 9", 1.1}}},
                {"Dominant 13th", {{"Major", 0.6}, {"Minor", 0.5}, {"Diminished", 0.8}, {"Augmented", 0.9}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.5}, {"Dominant 7th", 0.3}, {"Major 7th", 0.4}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.5}, {"6th", 0.4}, {"Minor 6th", 0.5}, {"Major Add 9", 0.4}, {"Minor Add 9", 0.5}, {"9th", 0.4}, {"Minor 9th", 0.5}, {"Major 9th", 0.3}, {"11th", 0.2}, {"Minor 11th", 0.5}, {"Major 11th", 0.1}, {"13th", 0.2}, {"Minor 13th", 0.3}, {"Major 13th", 0.1}, {"Dominant 9th", 0.2}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.0}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.6}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.5}, {"Diminished 11th", 0.8}, {"Major 13th Flat 9", 0.6}}},
                {"Minor 7th Flat 5", {{"Major", 0.6}, {"Minor", 0.3}, {"Diminished", 0.2}, {"Augmented", 0.7}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.3}, {"Dominant 7th", 0.4}, {"Major 7th", 0.6}, {"Diminished 7th", 0.2}, {"Minor Major 7th", 0.3}, {"6th", 0.5}, {"Minor 6th", 0.3}, {"Major Add 9", 0.6}, {"Minor Add 9", 0.3}, {"9th", 0.5}, {"Minor 9th", 0.3}, {"Major 9th", 0.7}, {"11th", 0.4}, {"Minor 11th", 0.3}, {"Major 11th", 0.8}, {"13th", 0.6}, {"Minor 13th", 0.3}, {"Major 13th", 0.8}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.2}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.0}, {"Half Diminished 7th", 0.1}, {"6/9", 0.5}, {"Minor 11th Flat 5", 0.2}, {"Diminished 11th", 0.2}, {"Major 13th Flat 9", 0.9}}},
                {"Half Diminished 7th", {{"Major", 0.5}, {"Minor", 0.4}, {"Diminished", 0.1}, {"Augmented", 0.8}, {"Suspended 2nd", 0.4}, {"Suspended 4th", 0.3}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.3}, {"Major 7th", 0.5}, {"Diminished 7th", 0.1}, {"Minor Major 7th", 0.4}, {"6th", 0.6}, {"Minor 6th", 0.4}, {"Major Add 9", 0.5}, {"Minor Add 9", 0.4}, {"9th", 0.6}, {"Minor 9th", 0.4}, {"Major 9th", 0.7}, {"11th", 0.5}, {"Minor 11th", 0.4}, {"Major 11th", 0.8}, {"13th", 0.7}, {"Minor 13th", 0.4}, {"Major 13th", 0.9}, {"Dominant 9th", 0.6}, {"Diminished 9th", 0.1}, {"Augmented Major 7th", 0.9}, {"Dominant 13th", 0.6}, {"Minor 7th Flat 5", 0.1}, {"Half Diminished 7th", 0.0}, {"6/9", 0.6}, {"Minor 11th Flat 5", 0.1}, {"Diminished 11th", 0.1}, {"Major 13th Flat 9", 1.0}}},
                {"6/9", {{"Major", 0.3}, {"Minor", 0.4}, {"Diminished", 0.7}, {"Augmented", 0.8}, {"Suspended 2nd", 0.3}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.5}, {"Major 7th", 0.2}, {"Diminished 7th", 0.9}, {"Minor Major 7th", 0.5}, {"6th", 0.2}, {"Minor 6th", 0.4}, {"Major Add 9", 0.3}, {"Minor Add 9", 0.4}, {"9th", 0.3}, {"Minor 9th", 0.4}, {"Major 9th", 0.2}, {"11th", 0.4}, {"Minor 11th", 0.3}, {"Major 11th", 0.3}, {"13th", 0.4}, {"Minor 13th", 0.4}, {"Major 13th", 0.3}, {"Dominant 9th", 0.3}, {"Diminished 9th", 0.9}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.4}, {"Minor 7th Flat 5", 0.5}, {"Half Diminished 7th", 0.6}, {"6/9", 0.0}, {"Minor 11th Flat 5", 0.4}, {"Diminished 11th", 0.7}, {"Major 13th Flat 9", 0.8}}},
                {"Minor 11th Flat 5", {{"Major", 0.7}, {"Minor", 0.3}, {"Diminished", 0.2}, {"Augmented", 0.6}, {"Suspended 2nd", 0.5}, {"Suspended 4th", 0.4}, {"Minor 7th", 0.3}, {"Dominant 7th", 0.4}, {"Major 7th", 0.5}, {"Diminished 7th", 0.2}, {"Minor Major 7th", 0.3}, {"6th", 0.6}, {"Minor 6th", 0.3}, {"Major Add 9", 0.5}, {"Minor Add 9", 0.3}, {"9th", 0.5}, {"Minor 9th", 0.3}, {"Major 9th", 0.7}, {"11th", 0.4}, {"Minor 11th", 0.3}, {"Major 11th", 0.8}, {"13th", 0.6}, {"Minor 13th", 0.3}, {"Major 13th", 0.8}, {"Dominant 9th", 0.5}, {"Diminished 9th", 0.2}, {"Augmented Major 7th", 0.8}, {"Dominant 13th", 0.5}, {"Minor 7th Flat 5", 0.2}, {"Half Diminished 7th", 0.1}, {"6/9", 0.4}, {"Minor 11th Flat 5", 0.0}, {"Diminished 11th", 0.2}, {"Major 13th Flat 9", 0.9}}},
                {"Diminished 11th", {{"Major", 0.8}, {"Minor", 0.6}, {"Diminished", 0.1}, {"Augmented", 0.9}, {"Suspended 2nd", 0.7}, {"Suspended 4th", 0.6}, {"Minor 7th", 0.4}, {"Dominant 7th", 0.7}, {"Major 7th", 0.8}, {"Diminished 7th", 0.0}, {"Minor Major 7th", 0.5}, {"6th", 0.9}, {"Minor 6th", 0.6}, {"Major Add 9", 0.8}, {"Minor Add 9", 0.6}, {"9th", 0.7}, {"Minor 9th", 0.5}, {"Major 9th", 0.9}, {"11th", 0.6}, {"Minor 11th", 0.4}, {"Major 11th", 0.9}, {"13th", 0.8}, {"Minor 13th", 0.5}, {"Major 13th", 1.0}, {"Dominant 9th", 0.7}, {"Diminished 9th", 0.1}, {"Augmented Major 7th", 1.0}, {"Dominant 13th", 0.8}, {"Minor 7th Flat 5", 0.2}, {"Half Diminished 7th", 0.1}, {"6/9", 0.7}, {"Minor 11th Flat 5", 0.2}, {"Diminished 11th", 0.0}, {"Major 13th Flat 9", 0.9}}},
                {"Major 13th Flat 9", {{"Major", 0.6}, {"Minor", 0.8}, {"Diminished", 0.9}, {"Augmented", 1.0}, {"Suspended 2nd", 0.8}, {"Suspended 4th", 0.7}, {"Minor 7th", 0.7}, {"Dominant 7th", 0.6}, {"Major 7th", 0.5}, {"Diminished 7th", 1.0}, {"Minor Major 7th", 0.7}, {"6th", 0.8}, {"Minor 6th", 0.7}, {"Major Add 9", 0.8}, {"Minor Add 9", 0.7}, {"9th", 0.6}, {"Minor 9th", 0.7}, {"Major 9th", 0.5}, {"11th", 0.7}, {"Minor 11th", 0.7}, {"Major 11th", 0.8}, {"13th", 0.6}, {"Minor 13th", 0.7}, {"Major 13th", 0.5}, {"Dominant 9th", 0.6}, {"Diminished 9th", 1.0}, {"Augmented Major 7th", 1.1}, {"Dominant 13th", 0.6}, {"Minor 7th Flat 5", 0.9}, {"Half Diminished 7th", 1.0}, {"6/9", 0.8}, {"Minor 11th Flat 5", 0.9}, {"Diminished 11th", 0.9}, {"Major 13th Flat 9", 0.0}}},
                {"Major", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"Minor", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Diminished", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Augmented", {{"Add9", 0.6}, {"7sus4", 0.7}, {"Maj7#11", 0.8}, {"7b9", 0.9}, {"7#9", 1.0}, {"m7b9", 0.7}, {"7#5", 0.8}, {"mMaj7", 0.6}, {"m7#5", 0.7}, {"Maj9", 0.5}, {"7b13", 0.9}}},
                {"Suspended 2nd", {{"Add9", 0.4}, {"7sus4", 0.5}, {"Maj7#11", 0.6}, {"7b9", 0.7}, {"7#9", 0.8}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.3}, {"7b13", 0.7}}},
                {"Suspended 4th", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Minor 7th", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Dominant 7th", {{"Add9", 0.4}, {"7sus4", 0.5}, {"Maj7#11", 0.6}, {"7b9", 0.7}, {"7#9", 0.8}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.3}, {"7b13", 0.7}}},
                {"Major 7th", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Diminished 7th", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Minor Major 7th", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"6th", {{"Add9", 0.4}, {"7sus4", 0.5}, {"Maj7#11", 0.6}, {"7b9", 0.7}, {"7#9", 0.8}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.3}, {"7b13", 0.7}}},
                {"Minor 6th", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Major Add 9", {{"Add9", 0.1}, {"7sus4", 0.2}, {"Maj7#11", 0.3}, {"7b9", 0.4}, {"7#9", 0.5}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.2}, {"m7#5", 0.3}, {"Maj9", 0.1}, {"7b13", 0.5}}},
                {"Minor Add 9", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"9th", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"Minor 9th", {{"Add9", 0.1}, {"7sus4", 0.2}, {"Maj7#11", 0.3}, {"7b9", 0.4}, {"7#9", 0.5}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.2}, {"m7#5", 0.3}, {"Maj9", 0.1}, {"7b13", 0.5}}},
                {"Major 9th", {{"Add9", 0.1}, {"7sus4", 0.2}, {"Maj7#11", 0.3}, {"7b9", 0.4}, {"7#9", 0.5}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.2}, {"m7#5", 0.3}, {"Maj9", 0.1}, {"7b13", 0.5}}},
                {"11th", {{"Add9", 0.4}, {"7sus4", 0.5}, {"Maj7#11", 0.6}, {"7b9", 0.7}, {"7#9", 0.8}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.3}, {"7b13", 0.7}}},
                {"Minor 11th", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Major 11th", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"13th", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Minor 13th", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Major 13th", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"Dominant 9th", {{"Add9", 0.4}, {"7sus4", 0.5}, {"Maj7#11", 0.6}, {"7b9", 0.7}, {"7#9", 0.8}, {"m7b9", 0.5}, {"7#5", 0.6}, {"mMaj7", 0.4}, {"m7#5", 0.5}, {"Maj9", 0.3}, {"7b13", 0.7}}},
                {"Diminished 9th", {{"Add9", 0.6}, {"7sus4", 0.7}, {"Maj7#11", 0.8}, {"7b9", 0.9}, {"7#9", 1.0}, {"m7b9", 0.7}, {"7#5", 0.8}, {"mMaj7", 0.6}, {"m7#5", 0.7}, {"Maj9", 0.5}, {"7b13", 0.9}}},
                {"Augmented Major 7th", {{"Add9", 0.7}, {"7sus4", 0.8}, {"Maj7#11", 0.9}, {"7b9", 1.0}, {"7#9", 1.1}, {"m7b9", 0.8}, {"7#5", 0.9}, {"mMaj7", 0.7}, {"m7#5", 0.8}, {"Maj9", 0.6}, {"7b13", 1.0}}},
                {"Dominant 13th", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Minor 7th Flat 5", {{"Add9", 0.2}, {"7sus4", 0.3}, {"Maj7#11", 0.4}, {"7b9", 0.5}, {"7#9", 0.6}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.1}, {"7b13", 0.6}}},
                {"Half Diminished 7th", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"6/9", {{"Add9", 0.1}, {"7sus4", 0.2}, {"Maj7#11", 0.3}, {"7b9", 0.4}, {"7#9", 0.5}, {"m7b9", 0.3}, {"7#5", 0.4}, {"mMaj7", 0.2}, {"m7#5", 0.3}, {"Maj9", 0.1}, {"7b13", 0.5}}},
                {"Minor 11th Flat 5", {{"Add9", 0.3}, {"7sus4", 0.4}, {"Maj7#11", 0.5}, {"7b9", 0.6}, {"7#9", 0.7}, {"m7b9", 0.4}, {"7#5", 0.5}, {"mMaj7", 0.3}, {"m7#5", 0.4}, {"Maj9", 0.2}, {"7b13", 0.7}}},
                {"Diminished 11th", {{"Add9", 0.5}, {"7sus4", 0.6}, {"Maj7#11", 0.7}, {"7b9", 0.8}, {"7#9", 0.9}, {"m7b9", 0.6}, {"7#5", 0.7}, {"mMaj7", 0.5}, {"m7#5", 0.6}, {"Maj9", 0.4}, {"7b13", 0.8}}},
                {"Major 13th Flat 9", {{"Add9", 0.6}, {"7sus4", 0.7}, {"Maj7#11", 0.8}, {"7b9", 0.9}, {"7#9", 1.0}, {"m7b9", 0.7}, {"7#5", 0.8}, {"mMaj7", 0.6}, {"m7#5", 0.7}, {"Maj9", 0.5}, {"7b13", 0.9}}}

        };

        auto it1 = tensionValues.find(chord1);
        if (it1 != tensionValues.end()) {
            auto it2 = it1->second.find(chord2);
            if (it2 != it1->second.end()) {
                return it2->second;
            }
        }

        return 1.0;
    }
};

class HighResolutionTimer {
private:
    static constexpr uint64_t FREQUENCY = 1'000'000'000ULL;
    alignas(64) std::atomic<int64_t> offset;
    alignas(64) std::atomic<uint64_t> start_time;
    const double frequency_multiplier;
    alignas(64) std::atomic<int64_t> average_overshoot;
    static constexpr int BUFFER_SIZE = 16;
    alignas(64) std::array<std::atomic<int64_t>, BUFFER_SIZE> overshoot_buffer;
    std::atomic<int> buffer_index;
    static constexpr int CALIBRATION_ROUNDS = 32;

public:
    HighResolutionTimer() noexcept :
        offset(0),
        start_time(0),
        average_overshoot(1000),
        buffer_index(0),
        frequency_multiplier(static_cast<double>(FREQUENCY) / static_cast<double>(performanceFrequency())) {
        for (auto& a : overshoot_buffer) {
            a.store(0, std::memory_order_relaxed);
        }
        calibrate();
    }

    __forceinline void start() noexcept {
        start_time.store(__rdtsc(), std::memory_order_relaxed);
    }

    [[nodiscard]] __forceinline std::chrono::nanoseconds elapsed() const noexcept {
        uint64_t end = __rdtsc();
        return std::chrono::nanoseconds(static_cast<int64_t>((end - start_time.load(std::memory_order_relaxed)) * frequency_multiplier) + offset.load(std::memory_order_relaxed));
    }

    __forceinline void sleep_until(std::chrono::nanoseconds target_time) noexcept {
        constexpr std::chrono::nanoseconds SPIN_THRESHOLD(5000);

        auto current_time = elapsed();
        auto sleep_duration = target_time - current_time - std::chrono::nanoseconds(average_overshoot.load(std::memory_order_relaxed));

        if (sleep_duration > SPIN_THRESHOLD) {
            _mm_pause();
            std::this_thread::sleep_for(sleep_duration - SPIN_THRESHOLD);
        }

        while (elapsed() < target_time) {
            _mm_pause();
            _mm_prefetch((char*)&target_time, _MM_HINT_T0);
        }

        auto actual_time = elapsed();
        record_overshoot(actual_time - target_time);
    }

    __forceinline void adjust(std::chrono::nanoseconds adjustment) noexcept {
        offset.fetch_add(adjustment.count(), std::memory_order_relaxed);
    }

private:
    static uint64_t performanceFrequency() noexcept {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return li.QuadPart;
    }

    void calibrate() noexcept {
        for (int i = 0; i < CALIBRATION_ROUNDS; ++i) {
            auto start = elapsed();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            auto end = elapsed();
            record_overshoot(end - start - std::chrono::microseconds(100));
        }
        update_average_overshoot();
    }

    __forceinline void record_overshoot(std::chrono::nanoseconds overshoot) noexcept {
        int index = buffer_index.fetch_add(1, std::memory_order_relaxed) % BUFFER_SIZE;
        overshoot_buffer[index].store(overshoot.count(), std::memory_order_relaxed);
        update_average_overshoot();
    }

    __forceinline void update_average_overshoot() noexcept {
        int64_t sum = 0;
        for (const auto& a : overshoot_buffer) {
            sum += a.load(std::memory_order_relaxed);
        }
        average_overshoot.store(sum / BUFFER_SIZE, std::memory_order_relaxed);
    }
};
class alignas(64) NoteEventPool {
private:
    static constexpr size_t BLOCK_SIZE = 1024 * 1024;

    struct alignas(CACHE_LINE_SIZE) Block {
        char data[BLOCK_SIZE];
        Block* next;
    };

    Block* head;
    char* current;
    char* end;
    std::atomic<size_t> allocated_count;

public:
    NoteEventPool() : head(nullptr), current(nullptr), end(nullptr), allocated_count(0) {
        allocateNewBlock();
    }

    ~NoteEventPool() {
        while (head) {
            Block* next = head->next;
            _aligned_free(head);
            head = next;
        }
    }

    template<typename... Args>
    NoteEvent* allocate(Args&&... args) {
        if (current + sizeof(NoteEvent) > end) {
            allocateNewBlock();
        }

        NoteEvent* event = new (current) NoteEvent(std::forward<Args>(args)...);
        current += sizeof(NoteEvent);
        allocated_count.fetch_add(1, std::memory_order_relaxed);
        return event;
    }

    void reset() {
        current = head->data;
        allocated_count.store(0, std::memory_order_relaxed);
    }

    size_t getAllocatedCount() const {
        return allocated_count.load(std::memory_order_relaxed);
    }

private:
    void allocateNewBlock() {
        Block* newBlock = static_cast<Block*>(_aligned_malloc(sizeof(Block), CACHE_LINE_SIZE));
        if (!newBlock) throw std::bad_alloc();

        newBlock->next = head;
        head = newBlock;
        current = newBlock->data;
        end = current + BLOCK_SIZE;
    }
};

class VirtualPianoPlayer {

    // Atomic variables
    alignas(64) std::atomic<bool> should_stop{ false }, paused{ true }, playback_started{ false },
        is_adjusting_playback{ false }, midiFileSelected{ false },
        enable_volume_adjustment{ false }, eightyEightKeyModeActive{ true }, enable_velocity_keypress{ false },
        skip_rewind_requested{ false }, skip_rewind_completed{ false };
    alignas(64) std::atomic<size_t> buffer_index{ 0 };
    alignas(64) std::atomic<int> current_volume, max_volume{ 0 };
    std::atomic<std::chrono::nanoseconds> skip_rewind_amount{ Duration::zero() };
    std::atomic<bool> skip_occurred{ false };
    std::atomic<Duration> skip_end_time{ Duration::zero() };
    std::string lastPressedKey = "gene likes men";
    bool ENABLE_OUT_OF_RANGE_TRANSPOSE{ false };

    // Time-related members
    alignas(64) TimePoint playback_start_time, last_resume_time, adjustment_start_time, last_adjustment_time;
    alignas(64) Duration total_adjusted_time { Duration::zero() };
    static constexpr Duration ADJUSTMENT_DURATION{ std::chrono::milliseconds(500) };

    // AVX constants
    const __m256d AVX_ONE = _mm256_set1_pd(1.0);
    const __m256d AVX_HALF = _mm256_set1_pd(0.5);

    // Other members
    double current_speed{ 1.0 };
    bool isSustainPressed{ false };
    WORD sustain_key_code, volume_up_key_code, volume_down_key_code;
    std::array<int, 128> volume_lookup, arrow_key_presses;
    alignas(64) std::vector<NoteEvent*> note_buffer;

    std::vector<std::tuple<double, std::string, std::string, int>> note_events;
    std::vector<std::pair<double, double>> tempo_changes;
    std::vector<TimeSignature> timeSignatures;
    std::unordered_map<std::string, std::atomic<bool>> pressed_keys;
    std::map<std::string, std::string> limited_key_mappings, full_key_mappings;
    std::map<int, std::function<void()>> controls;

    // Synchronization primitives
    std::mutex buffer_mutex, inputMutex, cv_m, skip_rewind_mutex;
    std::condition_variable cv, skip_rewind_cv;

    // Utility objects
    TransposeSuggestion detector;
    HighResolutionTimer timer;
    NoteEventPool event_pool;
    std::unique_ptr<std::jthread> playback_thread;
    std::thread listener_thread;
public:
    bool checkVCRedistVersion() {
        DWORD dwType = 0;
        DWORD dwSize = 0;
        const char* subkey = "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64";
        const char* value = "Version";

        if (RegGetValueA(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ, &dwType, nullptr, &dwSize) == ERROR_SUCCESS) {
            std::string version(dwSize, '\0');
            if (RegGetValueA(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ, &dwType, &version[0], &dwSize) == ERROR_SUCCESS) {
                version.resize(strlen(version.c_str()));
                if (version >= "14.30") {
                    return true;
                }
            }
        }
        return false;
    }

    bool checkAVX2Support() {
        int cpuInfo[4];
        __cpuid(cpuInfo, 0);
        int nIds = cpuInfo[0];

        if (nIds >= 7) {
            __cpuidex(cpuInfo, 7, 0);
            return (cpuInfo[1] & (1 << 5)) != 0;
        }
        return false;
    }

    VirtualPianoPlayer() noexcept
        : last_adjustment_time(Clock::now()) {
        if (!checkVCRedistVersion()) {
            throw std::runtime_error("Required Visual C++ Redistributable (2022 or newer) is not installed. Please download and install the latest version from Microsoft's website.");
        }
        if (!checkAVX2Support()) {
            throw std::runtime_error("This program requires a CPU with AVX2 support. Your CPU does not support AVX2 instructions.");
        }

        try {
            loadConfig();
        }
        catch (const std::exception& e) {
            handleConfigError(e);
        }
        precompute_volume_adjustments();
        auto [limited, full] = define_key_mappings();
        limited_key_mappings = std::move(limited);
        full_key_mappings = std::move(full);
        controls = define_controls();
        note_buffer.reserve(1 << 20);

        try {
            sustain_key_code = stringToVK(config.hotkeys.SUSTAIN_KEY);
            volume_up_key_code = vkToScanCode(stringToVK(config.hotkeys.VOLUME_UP_KEY));
            volume_down_key_code = vkToScanCode(stringToVK(config.hotkeys.VOLUME_DOWN_KEY));
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Error mapping hotkeys: " + std::string(e.what()));
        }

        if (sustain_key_code == 0 || volume_up_key_code == 0 || volume_down_key_code == 0) {
            throw std::runtime_error("Invalid hotkey configuration. Please check your config file.");
        }
    }
    ~VirtualPianoPlayer() {
        should_stop.store(true, std::memory_order_release);

        if (playback_thread && playback_thread->joinable()) {
            playback_thread->join();
        }

        if (listener_thread.joinable()) {
            listener_thread.join();
        }
    }
    void handleConfigError(const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        std::cerr << "Using default settings." << std::endl;
        setDefaultConfig();
        Sleep(2200);
    }
    struct InputEvent {
        std::vector<INPUT> inputs;
    };

    [[nodiscard]] Duration get_adjusted_time() noexcept {
        TimePoint now = std::chrono::steady_clock::now();
        Duration elapsed = now - last_resume_time;

        if (is_adjusting_playback.load(std::memory_order_relaxed)) {
            elapsed = handle_playback_adjustment(now, elapsed);
        }

        //faster multiplication
        __m256d avx_elapsed = _mm256_set1_pd(elapsed.count());
        __m256d avx_speed = _mm256_set1_pd(current_speed);
        __m256d avx_result = _mm256_mul_pd(avx_elapsed, avx_speed);
        long double adjusted_elapsed = (long double)_mm256_cvtsd_f64(avx_result);

        return total_adjusted_time + Duration(static_cast<Duration::rep>(adjusted_elapsed));
    }
    [[nodiscard]] Duration handle_playback_adjustment(TimePoint now, Duration elapsed) noexcept {
        Duration adjustment_elapsed = std::chrono::duration_cast<Duration>(now - adjustment_start_time);
        if (adjustment_elapsed >= ADJUSTMENT_DURATION) {
            is_adjusting_playback.store(false, std::memory_order_release);
        }
        else {
            //faster calculations
            __m256d avx_progress = _mm256_set1_pd(static_cast<double>(adjustment_elapsed.count()) / ADJUSTMENT_DURATION.count());
            __m256d avx_inverse_progress = _mm256_sub_pd(AVX_ONE, avx_progress);
            __m256d avx_speed_factor = _mm256_add_pd(AVX_ONE, _mm256_mul_pd(avx_inverse_progress, AVX_HALF));

            double speed_factor = _mm256_cvtsd_f64(avx_speed_factor);

            elapsed = Duration(static_cast<Duration::rep>(elapsed.count() * speed_factor));
        }
        return elapsed;
    }
    void play_notes() {
        prepare_event_queue();
        timer.start();

        if (!playback_started.load(std::memory_order_acquire)) {
            playback_start_time = std::chrono::steady_clock::now();
            last_resume_time = playback_start_time;
            playback_started.store(true, std::memory_order_release);
        }

        const size_t buffer_size = note_buffer.size();
        size_t current_index = 0;

        __m256d current_speed_vec = _mm256_set1_pd(current_speed);

        while (!should_stop.load(std::memory_order_acquire)) [[likely]] {
            if (should_stop.load(std::memory_order_acquire)) [[unlikely]] {
                break;  // Exit the loop if stop is requested
            }
            if (!paused.load(std::memory_order_acquire)) [[likely]] {
                Duration adjusted_time = get_adjusted_time();

                if (skip_occurred.load(std::memory_order_acquire)) [[unlikely]] {
                    Duration skip_end = skip_end_time.load(std::memory_order_acquire);
                    while (current_index < buffer_size && note_buffer[current_index]->time <= skip_end) {
                        ++current_index;
                    }
                    skip_occurred.store(false, std::memory_order_release);
                    adjusted_time = std::max(adjusted_time, skip_end);
                    }
                    if (current_index < buffer_size) [[likely]] {
                        if (note_buffer[current_index]->time > adjusted_time) [[unlikely]] {
                            current_index = avx_binary_search_index(adjusted_time, current_index);
                            }

                        size_t events_to_process = process_events(adjusted_time, current_index, buffer_size);
                        current_index += events_to_process;

                        if (current_index < buffer_size) [[likely]] {
                            Duration next_event_time = note_buffer[current_index]->time;
                            __m256d time_diff = _mm256_sub_pd(_mm256_set1_pd(next_event_time.count()), _mm256_set1_pd(adjusted_time.count()));
                            __m256d sleep_duration_vec = _mm256_div_pd(time_diff, current_speed_vec);
                            double sleep_duration = _mm256_cvtsd_f64(sleep_duration_vec);

                            if (sleep_duration > 0) [[likely]] {
                                timer.sleep_until(timer.elapsed() + Duration(static_cast<Duration::rep>(sleep_duration)));
                                }
                            }
                        }
                    else {
                        break;
                    }
                }
            else {
                handle_pause();
            }
            }
    }
    void handle_pause() noexcept {
        std::unique_lock<std::mutex> lock(cv_m);
        cv.wait(lock, [this]() {
            return !paused.load(std::memory_order_relaxed) || should_stop.load(std::memory_order_relaxed);
            });
        if (!should_stop.load(std::memory_order_relaxed)) {
            last_resume_time = std::chrono::steady_clock::now();
        }
    }
    size_t process_events(Duration adjusted_time, size_t start_index, size_t buffer_size) {
        size_t events_processed = 0;
        const __m256i adjusted_time_vec = _mm256_set1_epi64x(adjusted_time.count());

        for (size_t i = start_index; i < buffer_size; i += 4) {
            if (i + 4 <= buffer_size) {
                __m256i event_times = _mm256_set_epi64x(
                    note_buffer[i + 3]->time.count(),
                    note_buffer[i + 2]->time.count(),
                    note_buffer[i + 1]->time.count(),
                    note_buffer[i]->time.count()
                );
                __m256i cmp_result = _mm256_cmpgt_epi64(adjusted_time_vec, event_times);
                int mask = _mm256_movemask_epi8(cmp_result);

                if (mask == 0) break;

                for (int j = 0; j < 4; ++j) {
                    if (mask & (0xF << (j * 8))) {
                        execute_note_event(*note_buffer[i + j]);
                        events_processed++;
                    }
                    else {
                        return events_processed;
                    }
                }
            }
            else {
                for (; i < buffer_size; ++i) {
                    if (note_buffer[i] && note_buffer[i]->time <= adjusted_time) {
                        execute_note_event(*note_buffer[i]);
                        events_processed++;
                    }
                    else {
                        break;
                    }
                }
            }
        }

        return events_processed;
    }
    void handle_sleep(Duration adjusted_time, size_t buffer_index) noexcept {
        if (buffer_index < note_buffer.size()) {
            Duration next_event_time = note_buffer[buffer_index]->time;
            auto sleep_duration = std::chrono::duration_cast<Duration>((next_event_time - adjusted_time) / current_speed);
            if (sleep_duration > Duration::zero()) {
                timer.sleep_until(timer.elapsed() + sleep_duration);
            }
        }
    }

    size_t avx_binary_search_index(Duration adjusted_time, size_t start_index) {
        size_t left = 0;
        size_t right = start_index;

        __m256d target = _mm256_set1_pd(adjusted_time.count());

        while (left < right) {
            size_t mid = left + (right - left) / 2;
            __m256d current = _mm256_set1_pd(note_buffer[mid]->time.count());
            __m256d comparison = _mm256_cmp_pd(current, target, _CMP_LE_OQ);

            if (_mm256_movemask_pd(comparison) == 0xF) {
                left = mid + 1;
            }
            else {
                right = mid;
            }
        }

        return left;
    }
    void calibrate_volume() {
        int max_possible_steps = (config.volume.MAX_VOLUME - config.volume.MIN_VOLUME) / config.volume.VOLUME_STEP;
        for (int i = 0; i < max_possible_steps; ++i) {
            arrowsend(volume_down_key_code, true);
        }
        int steps_to_initial = (config.volume.INITIAL_VOLUME - config.volume.MIN_VOLUME) / config.volume.VOLUME_STEP;
        for (int i = 0; i < steps_to_initial; ++i) {
            arrowsend(volume_up_key_code, true);
        }

        current_volume.store(config.volume.INITIAL_VOLUME);
    }

    void precompute_volume_adjustments() {
        for (int velocity = 0; velocity <= 127; ++velocity) {
            double weighted_avg_velocity = velocity;
            int target_volume = static_cast<int>((weighted_avg_velocity / 127) * max_volume.load());
            target_volume = std::clamp(target_volume, config.volume.MIN_VOLUME, max_volume.load());
            target_volume = ((target_volume + 5) / 10) * 10;
            volume_lookup[velocity] = target_volume;
            arrow_key_presses[velocity] = std::abs(config.volume.INITIAL_VOLUME - target_volume) / config.volume.VOLUME_STEP;
        }
    }

    void adjust_playback_speed(double factor) {
        std::unique_lock<std::mutex> lock(cv_m);

        auto now = std::chrono::steady_clock::now();

        if (!playback_started.load(std::memory_order_relaxed)) {
            playback_start_time = now;
            last_resume_time = now;
        }
        else {
            total_adjusted_time += std::chrono::duration_cast<std::chrono::nanoseconds>(
                (now - last_resume_time) * current_speed);
            last_resume_time = now;
        }

        current_speed *= factor;
        current_speed = std::clamp(current_speed, 0.5, 2.0);
        if (std::abs(current_speed - 1.0) < 0.05) {
            current_speed = 1.0;
        }

        std::cout << "[SPEED] Playback speed adjusted. New speed: x" << current_speed << std::endl;
        cv.notify_all();
    }
    void printCentered(const std::string& text, int width) {
        int padding = (width - text.size()) / 2;
        std::string paddingSpaces(padding, ' ');
        std::cout << paddingSpaces << text << std::endl;
    }
    void print_ascii_interface() {
        system("cls");

        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int consoleWidth;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;

        setcolor(ConsoleColor::Blue);
        std::string ascii_art[] = {
            "  _____________________________________________________",
            " |  _________________________________________________  |",
            " | |                                                 | |",
            " | |     __  __ _____ _____ _____                    | |",
            " | |    |  \\/  |_   _|  __ \\_   _|_     _            | |",
            " | |    | \\  / | | | | |  | || |_| |_ _| |_          | |",
            " | |    | |\\/| | | | | |  | || |_   _|_   _|         | |",
            " | |    | |  | |_| |_| |__| || |_|_|   |_|           | |",
            " | |    |_|  |_|_____|_____/_____|                   | |",
            " | |                                                 | |",
            " | |_________________________________________________| |",
            " |    _________________________________________        |",
            " |   |  | | | |  |  | | | | | |  |  | | | |  |  |      |",
            " |   |  | | | |  |  | | | | | |  |  | | | |  |  |      |",
            " |   |  |_| |_|  |  |_| |_| |_|  |  |_| |_|  |  |      |",
            " |   |   |   |   |   |   |   |   |   |   |   |  |      |",
            " |   |   |   |   |   |   |   |   |   |   |   |  |      |",
            " |   |___|___|___|___|___|___|___|___|___|___|__|      |",
            " |_____________________________________________________|"
        };
        for (const auto& line : ascii_art) {
            printCentered(line, consoleWidth);
        }

        setcolor(ConsoleColor::LightBlue);
        std::string credits[] = {
            "======================================================",
            "                      CREDITS                         ",
            "======================================================"
        };
        for (const auto& line : credits) {
            printCentered(line, consoleWidth);
        }

        setcolor(ConsoleColor::Green);
        std::string contributors[] = {
            "        Zeph      : Main software developer, C++ implementation",
            "        Anger     : Provided insights with original Python code from PyMidi",
            "        Gene      : Assisted with extensive testing and validation"
        };
        for (const auto& line : contributors) {
            printCentered(line, consoleWidth);
        }

        setcolor(ConsoleColor::LightBlue);
        std::string thanks[] = {
            "------------------------------------------------------",
            "   Thanks to everyone for their invaluable help and   ",
            "                   contributions!                     ",
            "------------------------------------------------------"
        };
        for (const auto& line : thanks) {
            printCentered(line, consoleWidth);
        }

        setcolor(ConsoleColor::Default);
        std::cout << "\n";
        Sleep(2200);
        system("cls");
    }
    void main() {
        print_ascii_interface();
        setup_listener();
        std::string midi_file_wpath = get_midi_file_choice();
        if (!midi_file_wpath.empty()) {
            load_midi_file(midi_file_wpath);
            midiFileSelected.store(true);
        }

        if (listener_thread.joinable()) {
            listener_thread.join();
        }
    }

    void KeyPress(const std::string& key, bool press) {
        static std::atomic<bool> ctrlPressed(false), shiftPressed(false), altPressed(false);

        std::lock_guard<std::mutex> lock(inputMutex);
        std::vector<INPUT> inputs;
        inputs.reserve(4);  // Increased to accommodate potential alt key

        bool shifted = isupper(key.back()) || std::ispunct(key.back());
        bool ctrl = key.find("ctrl+") != std::string::npos;
        bool alt = key.find("alt+") != std::string::npos;

        auto addInput = [&inputs](WORD scanCode, DWORD flags) {
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = scanCode;
            input.ki.dwFlags = flags;
            inputs.push_back(input);
            };

        // Handle modifier keys
        if (alt && !altPressed.exchange(true)) {
            addInput(MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE);
        }
        if (ctrl && !ctrlPressed.exchange(true)) {
            addInput(MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE);
        }
        if (shifted && !shiftPressed.exchange(true)) {
            addInput(0x2A, KEYEVENTF_SCANCODE);
        }

        // Handle the main key
        WORD vkCode = VkKeyScan(key.back());
        BYTE scanCode = MapVirtualKey(LOBYTE(vkCode), MAPVK_VK_TO_VSC);
        addInput(scanCode, press ? KEYEVENTF_SCANCODE : (KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP));

        // Release modifier keys in reverse order
        if (shifted && shiftPressed.exchange(false)) {
            addInput(0x2A, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
        }
        if (ctrl && ctrlPressed.exchange(false)) {
            addInput(MapVirtualKey(VK_CONTROL, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
        }
        if (alt && altPressed.exchange(false)) {
            addInput(MapVirtualKey(VK_MENU, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP);
        }

        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
private:
    std::pair<std::map<std::string, std::string>, std::map<std::string, std::string>> define_key_mappings() {
        return { config.key_mappings["LIMITED"], config.key_mappings["FULL"] };
    }
    int stringToVK(const std::string& keyName) {
        static const std::unordered_map<std::string, int> keyMap = { //lol
             {"LBUTTON", VK_LBUTTON}, {"RBUTTON", VK_RBUTTON}, {"CANCEL", VK_CANCEL},
             {"MBUTTON", VK_MBUTTON}, {"XBUTTON1", VK_XBUTTON1}, {"XBUTTON2", VK_XBUTTON2},
             {"BACK", VK_BACK}, {"TAB", VK_TAB}, {"CLEAR", VK_CLEAR}, {"RETURN", VK_RETURN},
             {"SHIFT", VK_SHIFT}, {"CONTROL", VK_CONTROL}, {"MENU", VK_MENU}, {"PAUSE", VK_PAUSE},
             {"CAPITAL", VK_CAPITAL}, {"KANA", VK_KANA}, {"HANGEUL", VK_HANGEUL}, {"HANGUL", VK_HANGUL},
             {"JUNJA", VK_JUNJA}, {"FINAL", VK_FINAL}, {"HANJA", VK_HANJA}, {"KANJI", VK_KANJI},
             {"ESCAPE", VK_ESCAPE}, {"CONVERT", VK_CONVERT}, {"NONCONVERT", VK_NONCONVERT},
             {"ACCEPT", VK_ACCEPT}, {"MODECHANGE", VK_MODECHANGE}, {"SPACE", VK_SPACE},
             {"PRIOR", VK_PRIOR}, {"NEXT", VK_NEXT}, {"END", VK_END}, {"HOME", VK_HOME},
             {"LEFT", VK_LEFT}, {"UP", VK_UP}, {"RIGHT", VK_RIGHT}, {"DOWN", VK_DOWN},
             {"SELECT", VK_SELECT}, {"PRINT", VK_PRINT}, {"EXECUTE", VK_EXECUTE},
             {"SNAPSHOT", VK_SNAPSHOT}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
             {"HELP", VK_HELP}, {"LWIN", VK_LWIN}, {"RWIN", VK_RWIN}, {"APPS", VK_APPS},
             {"SLEEP", VK_SLEEP}, {"NUMPAD0", VK_NUMPAD0}, {"NUMPAD1", VK_NUMPAD1},
             {"NUMPAD2", VK_NUMPAD2}, {"NUMPAD3", VK_NUMPAD3}, {"NUMPAD4", VK_NUMPAD4},
             {"NUMPAD5", VK_NUMPAD5}, {"NUMPAD6", VK_NUMPAD6}, {"NUMPAD7", VK_NUMPAD7},
             {"NUMPAD8", VK_NUMPAD8}, {"NUMPAD9", VK_NUMPAD9}, {"MULTIPLY", VK_MULTIPLY},
             {"ADD", VK_ADD}, {"SEPARATOR", VK_SEPARATOR}, {"SUBTRACT", VK_SUBTRACT},
             {"DECIMAL", VK_DECIMAL}, {"DIVIDE", VK_DIVIDE}, {"F1", VK_F1}, {"F2", VK_F2},
             {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7},
             {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
             {"F13", VK_F13}, {"F14", VK_F14}, {"F15", VK_F15}, {"F16", VK_F16}, {"F17", VK_F17},
             {"F18", VK_F18}, {"F19", VK_F19}, {"F20", VK_F20}, {"F21", VK_F21}, {"F22", VK_F22},
             {"F23", VK_F23}, {"F24", VK_F24}, {"NUMLOCK", VK_NUMLOCK}, {"SCROLL", VK_SCROLL},
             {"LSHIFT", VK_LSHIFT}, {"RSHIFT", VK_RSHIFT}, {"LCONTROL", VK_LCONTROL},
             {"RCONTROL", VK_RCONTROL}, {"LMENU", VK_LMENU}, {"RMENU", VK_RMENU},
             {"BROWSER_BACK", VK_BROWSER_BACK}, {"BROWSER_FORWARD", VK_BROWSER_FORWARD},
             {"BROWSER_REFRESH", VK_BROWSER_REFRESH}, {"BROWSER_STOP", VK_BROWSER_STOP},
             {"BROWSER_SEARCH", VK_BROWSER_SEARCH}, {"BROWSER_FAVORITES", VK_BROWSER_FAVORITES},
             {"BROWSER_HOME", VK_BROWSER_HOME}, {"VOLUME_MUTE", VK_VOLUME_MUTE},
             {"VOLUME_DOWN", VK_VOLUME_DOWN}, {"VOLUME_UP", VK_VOLUME_UP},
             {"MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK}, {"MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK},
             {"MEDIA_STOP", VK_MEDIA_STOP}, {"MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE},
             {"LAUNCH_MAIL", VK_LAUNCH_MAIL}, {"LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT},
             {"LAUNCH_APP1", VK_LAUNCH_APP1}, {"LAUNCH_APP2", VK_LAUNCH_APP2},
             {"OEM_1", VK_OEM_1}, {"OEM_PLUS", VK_OEM_PLUS}, {"OEM_COMMA", VK_OEM_COMMA},
             {"OEM_MINUS", VK_OEM_MINUS}, {"OEM_PERIOD", VK_OEM_PERIOD}, {"OEM_2", VK_OEM_2},
             {"OEM_3", VK_OEM_3}, {"OEM_4", VK_OEM_4}, {"OEM_5", VK_OEM_5}, {"OEM_6", VK_OEM_6},
             {"OEM_7", VK_OEM_7}, {"OEM_8", VK_OEM_8}, {"OEM_102", VK_OEM_102},
             {"PROCESSKEY", VK_PROCESSKEY}, {"PACKET", VK_PACKET}, {"ATTN", VK_ATTN},
             {"CRSEL", VK_CRSEL}, {"EXSEL", VK_EXSEL}, {"EREOF", VK_EREOF}, {"PLAY", VK_PLAY},
             {"ZOOM", VK_ZOOM}, {"NONAME", VK_NONAME}, {"PA1", VK_PA1}, {"OEM_CLEAR", VK_OEM_CLEAR},
             {"ENTER", VK_RETURN}, {"ESC", VK_ESCAPE}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
             {"PGUP", VK_PRIOR}, {"PGDN", VK_NEXT}, {"PRTSC", VK_SNAPSHOT}, {"PRTSCR", VK_SNAPSHOT},
             {"PRINTSCRN", VK_SNAPSHOT}, {"APPS", VK_APPS}, {"CTRL", VK_CONTROL}, {"ALT", VK_MENU},
             {"DEL", VK_DELETE}, {"INS", VK_INSERT}, {"NUM", VK_NUMLOCK}, {"SCRLK", VK_SCROLL},
             {"CAPSLOCK", VK_CAPITAL}, {"CAPS", VK_CAPITAL}, {"BREAK", VK_CANCEL},
        };
        std::string upperKey = keyName;
        std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(),
            [](unsigned char c) { return std::toupper(c); });

        if (upperKey.substr(0, 3) == "VK_" || upperKey.substr(0, 2) == "VK") {
            upperKey = upperKey.substr(upperKey[2] == '_' ? 3 : 2);
        }

        auto it = keyMap.find(upperKey);
        if (it != keyMap.end()) {
            return it->second;
        }

        if (upperKey.length() == 1) {
            char ch = upperKey[0];
            if (std::isalnum(static_cast<unsigned char>(ch))) {
                SHORT vk = VkKeyScanA(ch);
                if (vk != -1) {
                    return LOBYTE(vk);
                }
            }
        }

        try {
            int vkCode = std::stoi(upperKey);
            if (vkCode >= 0 && vkCode <= 255) {
                return vkCode;
            }
        }
        catch (const std::exception&) {
            //ignore
        }
        throw std::runtime_error("Unable to map key '" + keyName + "' to a virtual key code.");
    }

    WORD vkToScanCode(int vk) {
        return static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
    }

    std::map<int, std::function<void()>> define_controls() {
        std::map<int, std::function<void()>> controls;

        for (const auto& [action, key] : config.controls) {
            int vk_code = stringToVK(key);
            if (vk_code != 0) {
                controls[vk_code] = [this, action]() {
                    if (action == "PLAY_PAUSE") toggle_play_pause();
                    else if (action == "REWIND") rewind(std::chrono::seconds(10));
                    else if (action == "SKIP") skip(std::chrono::seconds(10));
                    else if (action == "SPEED_UP") speed_up();
                    else if (action == "SLOW_DOWN") slow_down();
                    else if (action == "LOAD_NEW_SONG") load_new_song();
                    else if (action == "TOGGLE_88_KEY_MODE") toggle_88_key_mode();
                    else if (action == "TOGGLE_VOLUME_ADJUSTMENT") toggle_volume_adjustment();
                    else if (action == "TOGGLE_TRANSPOSE_ADJUSTMENT") toggle_transpose_adjustment();
                    else if (action == "STOP_AND_EXIT") stop_and_exit();
                    else if (action == "RESTART_SONG") restart_song();
                    else if (action == "TOGGLE_VELOCITY_KEYPRESS")  toggle_velocity_keypress();
                    else if (action == "TOGGLE_TRANSPOSE_KEY") toggle_out_of_range_transpose();
                    else if (action == "TOGGLE_SUSTAIN_MODE") toggleSustainMode();
                    else std::cerr << "Unknown action: " << action << std::endl;
                    };
            }
            else {
                std::cerr << "Invalid key binding: " << key << " for action: " << action << std::endl;
            }
        }
        return controls;
    }

    void clean_keyboard_state() {
        const int keys_to_reset[] = {
            VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
            VK_SHIFT, VK_LSHIFT, VK_RSHIFT,
            VK_MENU, VK_LMENU, VK_RMENU
        };

        for (int vk = 0; vk < 256; ++vk) {
            if (GetAsyncKeyState(vk) & 0x8000) {
                INPUT input = { 0 };
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                input.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(INPUT));
            }
        }
        for (int vk : keys_to_reset) {
            INPUT input[2] = { 0 };
            input[0].type = INPUT_KEYBOARD;
            input[0].ki.wVk = vk;
            input[1].type = INPUT_KEYBOARD;
            input[1].ki.wVk = vk;
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(2, input, sizeof(INPUT));
        }
        BYTE keyboard_state[256] = { 0 };
        SetKeyboardState(keyboard_state);
        Sleep(50);
        OpenClipboard(NULL);
        EmptyClipboard();
        CloseClipboard();
    }

    void stop_and_exit() {
        std::cout << "\n[EMERGENCY EXIT] Stopping playback and cleaning up...\n";
        should_stop.store(true, std::memory_order_relaxed);
        paused.store(true, std::memory_order_relaxed);
        release_all_keys();
        clean_keyboard_state();
        auto future = std::async(std::launch::async, [this] {
            if (playback_thread->joinable()) {
                playback_thread->join();
            }
            });

        if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
            std::cout << "Playback thread did not stop in time. Forcing termination...\n";
        }

        std::cout << "Cleanup complete. Keyboard state reset. Exiting program.\n";
        std::exit(0);
    }

    void reset_volume() {
        int volume_difference = config.volume.INITIAL_VOLUME - current_volume.load(std::memory_order_relaxed);
        int steps = std::abs(volume_difference) / config.volume.VOLUME_STEP;
        WORD scan_code = (volume_difference > 0) ? 0x4D : 0x4B;

        for (int i = 0; i < steps; ++i) {
            arrowsend(scan_code, true);
        }

        current_volume.store(config.volume.INITIAL_VOLUME, std::memory_order_relaxed);
    }

    void setup_listener() {
        listener_thread = std::thread([this]() {
            std::unordered_map<int, bool> key_states;
            for (const auto& [key, _] : controls) {
                key_states[key] = false;
            }

            while (true) {
                if (midiFileSelected.load()) {
                    for (const auto& [key, action] : controls) {
                        bool current_state = (GetAsyncKeyState(key) & 0x8000) != 0;
                        if (current_state && !key_states[key]) {
                            action();
                            key_states[key] = true;
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        }
                        else if (!current_state && key_states[key]) {
                            key_states[key] = false;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            });
    }

    void toggleSustainMode() {
        switch (currentSustainMode) {
        case SustainMode::IG:
            currentSustainMode = SustainMode::SPACE_DOWN;
            std::cout << "[SUSTAIN] DOWN (Press on sustain events) with cut off velocity set to: " << config.sustain.SUSTAIN_CUTOFF << std::endl;
            break;
        case SustainMode::SPACE_DOWN:
            currentSustainMode = SustainMode::SPACE_UP;
            std::cout << "[SUSTAIN] UP (Release on sustain events) with cut off velocity set to: " << config.sustain.SUSTAIN_CUTOFF << std::endl;
            break;
        case SustainMode::SPACE_UP:
            currentSustainMode = SustainMode::IG;
            std::cout << "[SUSTAIN] IGNORE (Ignores sustain events)" << std::endl;
            break;
        }
    }
    std::string transpose_note(const std::string& note) {
        int midi_note = note_name_to_midi(note);
        int transposed_note = (midi_note < 36) ? 36 + (midi_note % 12) :
            (midi_note > 96) ? 96 - (11 - (midi_note % 12)) :
            midi_note;
        if (transposed_note > midi_note + 24) {
            transposed_note = midi_note + 24;
        }
        else if (transposed_note < midi_note - 24) {
            transposed_note = midi_note - 24;
        }
        return get_note_name(transposed_note);
    }

    void toggle_out_of_range_transpose() {
        ENABLE_OUT_OF_RANGE_TRANSPOSE = !ENABLE_OUT_OF_RANGE_TRANSPOSE;

        setcolor(ConsoleColor::Blue);
        std::cout << "[61-KEY AUTO-TRANSPOSE] ";

        setcolor(ConsoleColor::White);
        std::cout << "Out-of-range transposition ";

        if (ENABLE_OUT_OF_RANGE_TRANSPOSE) {
            setcolor(ConsoleColor::Green);
            std::cout << "Enabled";
        }
        else {
            setcolor(ConsoleColor::Red);
            std::cout << "Disabled";
        }

        setcolor(ConsoleColor::White);
        std::cout << std::endl;
    }
    int note_name_to_midi(const std::string& note_name) {
        std::string pitch_class = note_name.substr(0, note_name.length() - 1);
        int octave = std::stoi(note_name.substr(note_name.length() - 1));
        int pitch_class_value = std::distance(NOTE_NAMES.begin(),
            std::find(NOTE_NAMES.begin(), NOTE_NAMES.end(), pitch_class));
        return (octave + 1) * 12 + pitch_class_value;
    }
    std::string getVelocityCurveName(VelocityCurveType curveType) {
        switch (curveType) {
        case VelocityCurveType::LinearCoarse: return "Linear Coarse";
        case VelocityCurveType::LinearFine: return "Linear Fine";
        case VelocityCurveType::ImprovedLowVolume: return "Improved Low Volume";
        case VelocityCurveType::Logarithmic: return "Logarithmic Curve";
        case VelocityCurveType::Exponential: return "Exponential Curve";
        default: return "Unknown";
        }
    }
    void toggle_velocity_keypress() {
        const int width = 80;
        const std::string separator(width, '=');
        const std::string section_title = "[VELOCITY ADJUSTMENT] ";

        setcolor(ConsoleColor::DarkGray);
        std::cout << separator << "\n";
        setcolor(ConsoleColor::Blue);
        std::cout << section_title;
        setcolor(ConsoleColor::White);

        enable_velocity_keypress.store(!enable_velocity_keypress.load(std::memory_order_relaxed), std::memory_order_relaxed);

        if (enable_velocity_keypress) {
            setcolor(ConsoleColor::Green);
            std::cout << "Enabled" << "\n";

            setcolor(ConsoleColor::DarkGray);
            std::cout << separator << "\n";
            setcolor(ConsoleColor::Red);
            std::cout << "[WARNING] ";
            setcolor(ConsoleColor::Yellow);
            std::cout << "This feature uses 'ALT + key' for velocity adjustments." << "\n";

            setcolor(ConsoleColor::White);
            std::cout << "\nTo avoid interference:\n";

            setcolor(ConsoleColor::Yellow);
            std::cout << "  - Make sure you are in ";
            setcolor(ConsoleColor::Green);
            std::cout << "fullscreen mode";
            setcolor(ConsoleColor::Yellow);
            std::cout << " in Roblox.\n";

            std::cout << "  - Disable or change keybindings for overlays such as:\n";
            setcolor(ConsoleColor::Cyan);
            std::cout << "    NVIDIA GeForce Experience, Intel Arc, AMD Radeon,\n";
            std::cout << "    and any other software that uses ALT+key shortcuts.\n";

            setcolor(ConsoleColor::White);
            std::cout << "\nIf your game does not support this, consider using the ";
            setcolor(ConsoleColor::LightBlue);
            std::cout << "Auto Volume";
            setcolor(ConsoleColor::White);
            std::cout << " feature instead." << "\n";

            // Selected Velocity Curve section
            setcolor(ConsoleColor::DarkGray);
            std::cout << separator << "\n";
            setcolor(ConsoleColor::White);
            std::cout << "\n[Selected Velocity Curve]: ";
            setcolor(ConsoleColor::Green);
            std::cout << getVelocityCurveName(config.playback.velocityCurve) << "\n";
        }

        else {
            setcolor(ConsoleColor::Red);
            std::cout << "Disabled" << std::endl;
        }
        setcolor(ConsoleColor::Default);
    }

    void toggle_play_pause() {
        std::lock_guard<std::mutex> lock(cv_m);
        release_all_keys();
        bool was_paused = paused.exchange(!paused.load(std::memory_order_relaxed));

        auto now = std::chrono::steady_clock::now();

        if (was_paused) {
            if (!playback_started.load(std::memory_order_relaxed)) {
                playback_start_time = now;
                last_resume_time = now;
                playback_started.store(true, std::memory_order_relaxed);
            }
            else {
                last_resume_time = now;
            }

            if (!playback_thread || !playback_thread->joinable()) {
                playback_thread = std::make_unique<std::jthread>(&VirtualPianoPlayer::play_notes, this);
            }
        }
        else {
            total_adjusted_time += std::chrono::duration_cast<std::chrono::nanoseconds>(
                (now - last_resume_time) * current_speed);
        }
        cv.notify_all();
        std::cout << (was_paused ? "Resumed playback" : "Paused playback") << std::endl;
    }
    void sendVirtualKey(WORD vk, bool is_press) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = is_press ? 0 : KEYEVENTF_KEYUP;
        input.ki.wScan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        input.ki.time = 0;
        input.ki.dwExtraInfo = 0;
        SendInput(1, &input, sizeof(INPUT));
    }

    void pressKey(WORD vk) {
        sendVirtualKey(vk, true);
    }

    void releaseKey(WORD vk) {
        sendVirtualKey(vk, false);
    }
    void handle_sustain_event(const NoteEvent& event) {
        bool pedal_on = event.sustainValue >= config.sustain.SUSTAIN_CUTOFF;

        switch (currentSustainMode) {
        case SustainMode::IG:
            break;
        case SustainMode::SPACE_DOWN:
            if (pedal_on && !isSustainPressed) {
                pressKey(sustain_key_code);
                isSustainPressed = true;
            }
            else if (!pedal_on && isSustainPressed) {
                releaseKey(sustain_key_code);
                isSustainPressed = false;
            }
            break;
        case SustainMode::SPACE_UP:
            if (!pedal_on && isSustainPressed) {
                releaseKey(sustain_key_code);
                isSustainPressed = false;
            }
            else if (pedal_on && !isSustainPressed) {
                pressKey(sustain_key_code);
                isSustainPressed = true;
            }
            break;
        }
    }
    void prepare_event_queue() {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        note_buffer.clear();
        event_pool.reset();

        for (const auto& event : note_events) {
            auto time_ns = std::chrono::nanoseconds(static_cast<long long>(std::get<0>(event) * 1e9));
            std::string note_or_control = std::get<1>(event);
            bool isPress = std::get<2>(event) == "press";
            int velocity_or_sustain = std::get<3>(event);
            bool isSustain = (note_or_control == "sustain");
            int sustainValue = isSustain ? (velocity_or_sustain & 0xFF) : 0;
            int velocity = isSustain ? 0 : velocity_or_sustain;

            note_buffer.push_back(event_pool.allocate(time_ns, note_or_control, isPress, velocity, isSustain, sustainValue));
        }

        std::sort(note_buffer.begin(), note_buffer.end(), [](const NoteEvent* a, const NoteEvent* b) {
            return a->time < b->time;
            });
    }
    const std::array<int, 32> linearCoarseVelocityList = {
        4,   8,   12,  16,
        20,  24,  28,  32,
        36,  40,  44,  48,
        52,  56,  60,  64,
        68,  72,  76,  80,
        84,  88,  92,  96,
        100, 104, 108, 112,
        116, 120, 124, 127
    };

    const std::array<int, 32> linearFineVelocityList = {
        2,   6,   10,  14,
        18,  22,  26,  30,
        34,  38,  42,  46,
        50,  54,  58,  62,
        66,  70,  74,  78,
        82,  86,  90,  94,
        98,  102, 106, 110,
        114, 118, 122, 127
    };

    const std::array<int, 32> improvedLowVolumeVelocityList = {
        1,   3,   5,   7,
        10,  13,  16,  20,
        24,  29,  34,  40,
        46,  53,  60,  68,
        76,  85,  94,  104,
        114, 120, 123, 127,
        127, 127, 127, 127,
        127, 127, 127, 127
    };

    const std::array<int, 32> logarithmicVelocityList = {
        1,   2,   3,   5,
        7,   10,  14,  19,
        25,  32,  40,  49,
        60,  72,  85,  99,
        115, 127, 127, 127,
        127, 127, 127, 127,
        127, 127, 127, 127,
        127, 127, 127, 127
    };

    const std::array<int, 32> exponentialVelocityList = {
        1,   2,   4,   8,
        16,  24,  32,  40,
        48,  56,  64,  72,
        80,  88,  96,  104,
        110, 115, 120, 124,
        126, 127, 127, 127,
        127, 127, 127, 127,
        127, 127, 127, 127
    };

    std::string getVelocityKey(int targetVelocity) {
        const std::array<int, 32>* selectedVelocityList;

        // Select the appropriate velocity list based on the configuration
        switch (config.playback.velocityCurve) {
        case VelocityCurveType::LinearCoarse:
            selectedVelocityList = &linearCoarseVelocityList;
            break;
        case VelocityCurveType::LinearFine:
            selectedVelocityList = &linearFineVelocityList;
            break;
        case VelocityCurveType::ImprovedLowVolume:
            selectedVelocityList = &improvedLowVolumeVelocityList;
            break;
        case VelocityCurveType::Logarithmic:
            selectedVelocityList = &logarithmicVelocityList;
            break;
        case VelocityCurveType::Exponential:
            selectedVelocityList = &exponentialVelocityList;
            break;
        default:
            selectedVelocityList = &linearCoarseVelocityList; // Default case
            break;
        }

        if (targetVelocity <= (*selectedVelocityList)[0]) {
            return "1"; // Minimum velocity key
        }

        auto upperBound = std::upper_bound(selectedVelocityList->begin(), selectedVelocityList->end(), targetVelocity);
        int index = upperBound - selectedVelocityList->begin();

        if (index >= selectedVelocityList->size()) {
            index = selectedVelocityList->size() - 1;
        }

        return std::string(1, velocitykeys[index]);
    }


    const std::string velocitykeys = "1234567890qwertyuiopasdfghjklzxc";

    __forceinline void execute_note_event(const NoteEvent& event) noexcept {
        if (!event.isSustain) [[likely]] {
            if (event.isPress) [[likely]] {
                const bool volumeAdjustmentEnabled = enable_volume_adjustment.load(std::memory_order_relaxed);
                const bool velocityKeyPressEnabled = enable_velocity_keypress.load(std::memory_order_relaxed);

                if (volumeAdjustmentEnabled) [[unlikely]] {
                    AdjustVolumeBasedOnVelocity(event.velocity);
                    }

                    if (velocityKeyPressEnabled && event.velocity != 0) [[unlikely]] {
                        std::string velocityKey = "alt+" + getVelocityKey(event.velocity);

                        if (velocityKey != lastPressedKey) {
                            KeyPress(velocityKey, true);
                            KeyPress(velocityKey, false);
                            lastPressedKey = std::move(velocityKey);
                        }
                        }

                press_key(event.note);
                }
            else {
                release_key(event.note);
            }
            }
        else {
            handle_sustain_event(event);
        }
    }


    inline void press_key(const std::string& note) noexcept {
        const std::string& actual_note = ENABLE_OUT_OF_RANGE_TRANSPOSE ? transpose_note(note) : note ;
        const std::string& key = eightyEightKeyModeActive ?
            full_key_mappings[actual_note] : limited_key_mappings[actual_note];
        if (!key.empty()) {
            if (!pressed_keys[actual_note].exchange(true, std::memory_order_relaxed)) {
                KeyPress(key, true);
            }
            else {
                KeyPress(key, false);
                KeyPress(key, true);
            }
        }
    }

    inline void release_key(const std::string& note) noexcept {
        const std::string& key = eightyEightKeyModeActive ?
            full_key_mappings[note] : limited_key_mappings[note];
        if (!key.empty() && pressed_keys[note].exchange(false, std::memory_order_relaxed)) {
            KeyPress(key, false);
        }
    }
    void release_all_keys() { //todo: im fucking lazy but add release code for space, because sustain breaks when pausing somtimes due to the key up event not being sent before a new keydown, fuck roblox x2
        const auto& current_mappings = eightyEightKeyModeActive ? full_key_mappings : limited_key_mappings;
        for (const auto& [note, key] : current_mappings) {
            KeyPress(key, false);
        }
        for (auto& [note, pressed] : pressed_keys) {
            pressed.store(false, std::memory_order_relaxed);
        }
        KeyPress("alt+", false); // fuck roblox
    }

    void skip(std::chrono::seconds duration) {
        std::unique_lock<std::mutex> lock(skip_rewind_mutex);

        if (!playback_started.load(std::memory_order_acquire)) {
            setcolor(ConsoleColor::Blue);
            std::cout << "[SKIP] ";
            setcolor(ConsoleColor::White);
            std::cout << "Playback not started, adjusting start time\n";
            setcolor(ConsoleColor::White);
            playback_start_time += duration;
            last_resume_time = playback_start_time;
            return;
        }

        auto now = std::chrono::steady_clock::now();
        Duration current_position;

        if (!paused.load(std::memory_order_acquire)) {
            auto elapsed = std::chrono::duration_cast<Duration>((now - last_resume_time) * current_speed);
            current_position = total_adjusted_time + elapsed;
        }
        else {
            current_position = total_adjusted_time;
        }

        auto skip_amount = std::chrono::duration_cast<Duration>(duration * current_speed);
        Duration new_position = current_position + skip_amount;

        // bugfix check if we're trying to skip beyond the end of the song
        if (!note_buffer.empty() && new_position > note_buffer.back()->time) {
            setcolor(ConsoleColor::Blue);
            std::cout << "[SKIP] ";
            setcolor(ConsoleColor::White);
            std::cout << "Attempting to skip beyond the end of the song sure...\n";
            setcolor(ConsoleColor::White);
            new_position = note_buffer.back()->time;
        }

        total_adjusted_time = new_position;
        last_resume_time = now;
        is_adjusting_playback.store(true, std::memory_order_release);
        adjustment_start_time = now;

        size_t new_index = find_next_event_index(total_adjusted_time);
        buffer_index.store(new_index, std::memory_order_release);

        skip_occurred.store(true, std::memory_order_release);
        skip_end_time.store(total_adjusted_time, std::memory_order_release);

        release_all_keys();
        setcolor(ConsoleColor::Blue);
        std::cout << "[SKIP] ";
        setcolor(ConsoleColor::White);
        std::cout << "New buffer index after skip: " << std::to_string(new_index)<<std::endl;
        setcolor(ConsoleColor::White);

        if (paused.load(std::memory_order_acquire)) {
            cv.notify_all(); // Wake up the playback thread if it's paused
        }
    }
    void rewind(std::chrono::seconds duration) {
        std::unique_lock<std::mutex> lock(skip_rewind_mutex);

        if (!playback_started.load(std::memory_order_acquire)) {
            setcolor(ConsoleColor::Blue);
            std::cout << "[REWIND] ";
            setcolor(ConsoleColor::White);
            std::cout << "Playback not started, adjusting start time\n";
            setcolor(ConsoleColor::White);
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (!paused.load(std::memory_order_acquire)) {
            auto elapsed = std::chrono::duration_cast<Duration>((now - last_resume_time) * current_speed);
            total_adjusted_time += elapsed;
        }

        auto rewind_amount = std::chrono::duration_cast<Duration>(duration * current_speed);
        if (rewind_amount > total_adjusted_time) {
            rewind_amount = total_adjusted_time;
        }
        total_adjusted_time -= rewind_amount;

        last_resume_time = now;
        is_adjusting_playback.store(true, std::memory_order_release);
        adjustment_start_time = now;

        size_t new_index = find_next_event_index(total_adjusted_time);
        buffer_index.store(new_index, std::memory_order_release);

        release_all_keys();
        setcolor(ConsoleColor::Blue);
        std::cout << "[REWIND] ";
        setcolor(ConsoleColor::White);
        std::cout << "New buffer index after rewind: " << std::to_string(new_index) << std::endl;
        setcolor(ConsoleColor::White);
        if (paused.load(std::memory_order_acquire)) {
            cv.notify_all(); // Wake up the playback thread if it's paused
        }
    }
    size_t find_next_event_index(const Duration& target_time) {
        return std::lower_bound(note_buffer.begin(), note_buffer.end(), target_time,
            [](const auto& event, const Duration& time) {
                return event->time < time;
            }) - note_buffer.begin();
    }

    void speed_up() {
        adjust_playback_speed(1.1);
    }

    void slow_down() {
        adjust_playback_speed(1.0 / 1.1);
    }

    void toggle_88_key_mode() {
        eightyEightKeyModeActive = !eightyEightKeyModeActive;
        setcolor(ConsoleColor::Blue);
        std::cout << "[88-KEY MODE] ";
        setcolor(ConsoleColor::White);
        std::cout << (eightyEightKeyModeActive ? "Enabled" : "Disabled") << std::endl;
    }

    void toggle_volume_adjustment() {
        enable_volume_adjustment = !enable_volume_adjustment;
        if (enable_volume_adjustment) {
            max_volume = config.volume.MAX_VOLUME;
            precompute_volume_adjustments();
            calibrate_volume();
            setcolor(ConsoleColor::Blue);
            std::cout << "[AUTOVOL] ";
            setcolor(ConsoleColor::White);
            std::cout << "INITIAL VOLUME: ";
            std::cout << config.volume.INITIAL_VOLUME << "%" << " | VOLUME STEP: " << config.volume.VOLUME_STEP << "%" << " | MAX VOLUME: " << config.volume.MAX_VOLUME << "%" << std::endl;
            setcolor(ConsoleColor::White);
        }
        else {
            setcolor(ConsoleColor::Blue);
            std::cout << "[AUTOVOL] ";
            setcolor(ConsoleColor::White);
            std::cout << "Volume adjustment disabled." << std::endl;
        }
    }
    void toggle_transpose_adjustment() {
        const int width = 80;
        const std::string separator(width, '=');
        const std::string section_title = "[TRANSPOSE ENGINE]";

        setcolor(ConsoleColor::DarkGray);
        std::cout << separator << "\n";
        setcolor(ConsoleColor::Cyan);
        std::cout << section_title << "\n";
        setcolor(ConsoleColor::White);
        std::cout << "Warning: Only run this before starting playback as it may affect performance due to intensive calculations.\n";
        setcolor(ConsoleColor::DarkGray);
        std::cout << separator << "\n";
        setcolor(ConsoleColor::Default);
        auto [notes, durations] = detector.extractNotesAndDurations(midi_file);
        if (notes.empty()) {
            setcolor(ConsoleColor::Red);
            std::cout << "[ERROR] No notes detected. The MIDI file might be corrupted or empty.\n";
            setcolor(ConsoleColor::DarkGray);
            std::cout << separator << "\n";
            setcolor(ConsoleColor::Default);
            return;
        }

        setcolor(ConsoleColor::Cyan);
        std::cout << "Estimated musical key: ";
        setcolor(ConsoleColor::White);
        std::string key = detector.estimateKey(notes, durations);
        std::cout << key << "\n";

        setcolor(ConsoleColor::Cyan);
        std::cout << "Detected genre: ";
        setcolor(ConsoleColor::White);
        std::string genre = detector.detectGenre(midi_file, notes, durations);
        std::cout << genre << "\n";
        setcolor(ConsoleColor::DarkGray);
        std::cout << separator << "\n";

        int bestTranspose = detector.findBestTranspose(notes, durations, key, genre);
        setcolor(ConsoleColor::Cyan);
        std::cout << "Transpose Suggestion (Beta):\n";
        setcolor(ConsoleColor::White);
        std::cout << "The suggested transpose value is ";
        setcolor(ConsoleColor::Green);
        std::cout << bestTranspose;
        setcolor(ConsoleColor::White);
        std::cout << " semitones.\n";
        setcolor(ConsoleColor::Yellow);
        std::cout << "\n[ ! ] This suggestion is based on advanced music theory concepts.\n";
        std::cout << "Remember, the best transpose ultimately depends on personal preference.\n";
        setcolor(ConsoleColor::DarkGray);
        std::cout << separator << "\n";

    }
    std::string wstring_to_string(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
        return str;
    }

    std::wstring string_to_wstring(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }
    // fucking gene bro
    std::wstring sanitizeFileName(const std::wstring& fileName) {
        const size_t maxLength = 200;
        std::wstring sanitized;

        std::wstring truncatedFileName = fileName.substr(0, maxLength - 1);

        for (wchar_t c : truncatedFileName) {
            if ((c >= L'a' && c <= L'z') ||
                (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') ||
                c == L'_' || c == L'-' ||
                c == L'.' || c == L' ' ||
                c == L'(' || c == L')' ||
                c == L'[' || c == L']' ||
                c == L'{' || c == L'}' ||
                c == L'@' || c == L'!' ||
                c == L'#' || c == L'$' ||
                c == L'%' || c == L'^' ||
                c == L'&' || c == L'+' ||
                c == L'=' || c == L',' ||
                c == L'\'' || c == L'`' ||
                c == L'~' || c == L';' ||
                c == L':' || c == L'<' ||
                c == L'>' || c == L'?' ||
                c == L'/' || c == L'|' ||
                c == L'\\' || c == L'\"' ||
                c == L'*') {
                sanitized += c;
            }
            else {
                sanitized += L'_';
            }

            if (sanitized.length() >= maxLength) {
                sanitized = sanitized.substr(0, maxLength - 1);
                break;
            }
        }

        return sanitized;
    }

    std::vector<std::pair<std::wstring, std::wstring>> list_midi_files(const std::wstring& folder) {
        std::vector<std::pair<std::wstring, std::wstring>> midi_files;
        std::set<std::wstring> processed_files;
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((folder + L"\\*").c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            std::wcerr << L"FindFirstFileW failed with error code: " << GetLastError() << std::endl;
            return midi_files;
        }

        do {
            std::wstring file_name(findFileData.cFileName);
            if (file_name == L"." || file_name == L"..") {
                continue;
            }
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring file_ext = file_name.substr(file_name.find_last_of(L'.') + 1);
                std::transform(file_ext.begin(), file_ext.end(), file_ext.begin(), ::towlower);
                if (file_ext == L"mid" || file_ext == L"midi") {
                    std::wstring sanitizedName = sanitizeFileName(file_name);
                    std::wstring fullPath = folder + L"\\" + file_name;
                    if (processed_files.find(sanitizedName) == processed_files.end()) {
                        processed_files.insert(sanitizedName);

                        if (sanitizedName != file_name) {
                            std::wstring newPath = folder + L"\\" + sanitizedName;
                            if (MoveFileW(fullPath.c_str(), newPath.c_str())) {
                                midi_files.emplace_back(sanitizedName, newPath);
                            }
                            else {
                                std::wcerr << L"Failed to rename file: " << file_name << std::endl;
                                midi_files.emplace_back(file_name, fullPath);
                            }
                        }
                        else {
                            midi_files.emplace_back(file_name, fullPath);
                        }
                    }
                }
            }
        } while (FindNextFileW(hFind, &findFileData) != 0);

        FindClose(hFind);
        return midi_files;
    }

    std::string get_midi_file_choice() {
        const std::wstring midi_folder = L"midi"; 
        auto midi_files = list_midi_files(midi_folder);

        if (midi_files.empty()) {
            std::wcout << L"No MIDI files found in the 'midi' folder.\n";
            return "";
        }

        std::sort(midi_files.begin(), midi_files.end());

        const int width = 80;
        const std::wstring title = L"MIDI File Selection";
        const std::wstring separator(width, L'=');

        std::wcout << L"\n";
        setcolor(ConsoleColor::Cyan);
        std::wcout << separator << L"\n";
        setcolor(ConsoleColor::Yellow);
        std::wcout << std::setw((width + title.length()) / 2) << title << L"\n";
        setcolor(ConsoleColor::Cyan);
        std::wcout << separator << L"\n\n";

        setcolor(ConsoleColor::White);
        for (size_t i = 0; i < midi_files.size(); ++i) {
            setcolor(ConsoleColor::LightBlue);
            std::wcout << L"[" << std::setw(2) << i + 1 << L"] ";
            setcolor(ConsoleColor::White);
            std::wstring displayName = midi_files[i].first;
            if (displayName.length() > 77) {
                displayName = displayName.substr(0, 74) + L"...";
            }
            std::wcout << displayName << L"\n";
        }
        std::wcout << L"\n";
        setcolor(ConsoleColor::Cyan);
        std::wcout << separator << L"\n";
        setcolor(ConsoleColor::Yellow);
        std::wcout << L"Enter the number of the MIDI file you want to play (1-"
            << midi_files.size() << L"): ";

        size_t choice;
        while (true) {
            setcolor(ConsoleColor::White);
            if (std::wcin >> choice && choice >= 1 && choice <= midi_files.size()) {
                break;
            }
            setcolor(ConsoleColor::LightRed);
            std::wcout << L"Invalid input. Please enter a number between 1 and "
                << midi_files.size() << L": ";
            std::wcin.clear();
            std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n');
        }

        setcolor(ConsoleColor::LightCyan);
        std::wcout << L"\nYou selected: " << midi_files[choice - 1].first << L"\n";
        setcolor(ConsoleColor::Cyan);
        std::wcout << separator << L"\n";

        setcolor(ConsoleColor::Default);

        return wstring_to_string(midi_files[choice - 1].second);
    }
    void reset() {
            std::lock_guard<std::mutex> lock(cv_m);
            should_stop.store(true, std::memory_order_release);
            paused.store(true, std::memory_order_release);
            cv.notify_all();

            if (playback_thread && playback_thread->joinable()) {
                try {
                    playback_thread->join();
                }
                catch (const std::exception& e) {
                    std::cerr << "Error joining playback thread: " << e.what() << std::endl;
                }
            }
            playback_started.store(false, std::memory_order_release);
            is_adjusting_playback.store(false, std::memory_order_release);
            midiFileSelected.store(false, std::memory_order_release);
            enable_volume_adjustment.store(false, std::memory_order_release);
            eightyEightKeyModeActive.store(false, std::memory_order_release);
            skip_rewind_requested.store(false, std::memory_order_release);
            skip_rewind_completed.store(false, std::memory_order_release);
            skip_occurred.store(false, std::memory_order_release);

            total_adjusted_time = Duration::zero();
            playback_start_time = TimePoint();
            last_resume_time = TimePoint();
            adjustment_start_time = TimePoint();
            last_adjustment_time = Clock::now();
            skip_end_time.store(Duration::zero(), std::memory_order_release);

            current_speed = 1.0;
            buffer_index.store(0, std::memory_order_release);
            current_volume.store(config.volume.INITIAL_VOLUME, std::memory_order_release);
            max_volume.store(config.volume.MAX_VOLUME, std::memory_order_release);
            note_events.clear();
            tempo_changes.clear();
            timeSignatures.clear();
            note_buffer.clear();
            for (auto& [note, pressed] : pressed_keys) {
                pressed.store(false, std::memory_order_relaxed);
            }
            release_all_keys();
            reset_volume();
            skip_rewind_amount.store(Duration::zero(), std::memory_order_release);
            isSustainPressed = false;
            currentSustainMode = SustainMode::IG;

            event_pool.reset();
            timer.start();
            midi_file = MidiFile();
    }
  void restart_song() {
        try {
            std::cout << "[RESTART] Restarting the current song...\n";
            should_stop.store(true, std::memory_order_release);
            paused.store(true, std::memory_order_release);
            cv.notify_all();

            // Safely join the playback thread
            if (playback_thread && playback_thread->joinable()) {
                auto future = std::async(std::launch::async, [this] {
                    playback_thread->join();
                    });

                if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                    std::cerr << "Warning: Playback thread did not join in time. Detaching.\n";
                    playback_thread->detach();
                }
            }
            playback_started.store(false, std::memory_order_release);
            total_adjusted_time = Duration::zero();
            current_speed = 1.0;
            release_all_keys();
            timer.start();
            playback_start_time = std::chrono::steady_clock::now();
            last_resume_time = playback_start_time;
            should_stop.store(false, std::memory_order_release);
            paused.store(false, std::memory_order_release);

            playback_thread = std::make_unique<std::jthread>(&VirtualPianoPlayer::play_notes, this);
        }
        catch (const std::exception& e) {
            std::cerr << "Error during song restart: " << e.what() << "\n";
            reset();
        }
    }

  void load_new_song() {
      while (true) {
          try {
              system("cls");
              cv.notify_all();

              should_stop.store(true, std::memory_order_release);
              paused.store(true, std::memory_order_release);
              if (playback_thread && playback_thread->joinable()) {
                  auto future = std::async(std::launch::async, [this] {
                      playback_thread->join();
                      });

                  if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                      std::cerr << "Warning: Playback thread did not join in time. Detaching.\n";
                      playback_thread->detach();
                  }
              }
              note_events.clear();
              tempo_changes.clear();
              timeSignatures.clear();
              std::string new_midi_file_path = get_midi_file_choice();
              if (new_midi_file_path.empty()) {
                  std::cout << "No new MIDI file selected. Returning to previous state.\n";
                  return;
              }
              MidiParser parser;
              midi_file = parser.parse(new_midi_file_path);
              process_tracks(midi_file);

              should_stop.store(false, std::memory_order_release);
              paused.store(true, std::memory_order_release);
              playback_started.store(false, std::memory_order_release);
              total_adjusted_time = Duration::zero();
              current_speed = 1.0;
              is_adjusting_playback.store(false, std::memory_order_release);
              skip_occurred.store(false, std::memory_order_release);
              skip_end_time.store(Duration::zero(), std::memory_order_release);
              buffer_index.store(0, std::memory_order_release);

              release_all_keys();
              timer.start();
              playback_start_time = std::chrono::steady_clock::now();
              last_resume_time = playback_start_time;

              display_midi_details(midi_file, new_midi_file_path);

              break;

          }
          catch (const std::exception& e) {
              std::cerr << "Error loading new song: " << e.what() << "\n";
              std::cerr << "Please select a valid MIDI file.\n";
              Sleep(2500);  
          }
      }
  }
  void load_midi_file(const std::string& initial_midi_file_path) {
      MidiParser parser;
      std::string midi_file_path = initial_midi_file_path;
      bool is_load_successful = false;

      midi_file = MidiFile();  
      note_events.clear();
      tempo_changes.clear();
      timeSignatures.clear();
      event_pool.reset();
      pressed_keys.clear();
      playback_started.store(false, std::memory_order_release);
      midiFileSelected.store(false, std::memory_order_release);

      while (!is_load_successful) {
          try {
              if (!std::filesystem::exists(midi_file_path)) {
                  throw std::runtime_error("File does not exist: " + midi_file_path);
              }
              midi_file = parser.parse(midi_file_path);
              if (midi_file.tracks.empty()) {
                  throw std::runtime_error("Parsed MIDI file contains no tracks.");
              }
              process_tracks(midi_file);
              if (note_events.empty()) {
                  throw std::runtime_error("No notes detected after processing MIDI file.");
              }
              is_load_successful = true;  
              display_midi_details(midi_file, midi_file_path);

          }
          catch (const std::exception& e) {
              std::cerr << "Error parsing MIDI file: " << midi_file_path << std::endl;
              std::cerr << "Error details: " << e.what() << std::endl;
              std::cout << "Would you like to select another song? (y/n): ";
              char response;
              std::cin >> response;
              if (response != 'y' && response != 'Y') {
                  std::cout << "Exiting MIDI file loading." << std::endl;
                  throw std::runtime_error("Failed to load a valid MIDI file.");
              }

              midi_file_path = get_midi_file_choice();
              if (midi_file_path.empty()) {
                  std::cout << "No MIDI file selected. Exiting MIDI file loading." << std::endl;
                  throw std::runtime_error("No MIDI file selected.");
              }
          }
      }
      midiFileSelected.store(true, std::memory_order_release);
  }


    template <typename T>
    std::string toString(const T& value) const {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
    void display_midi_details(const MidiFile& midi_file, const std::string& midi_file_path) {
        const int width = 80;
        const std::string separator(width, '=');
        const std::string half_separator(width / 2, '-');

        system("cls");

        auto center = [width](const std::string& text) {
            return std::string((width - text.length()) / 2, ' ') + text;
            };

        auto print_colored_line = [](const std::string& text, ConsoleColor color) {
            setcolor(color);
            std::cout << text << "\n";
            setcolor(ConsoleColor::Default);
            };

        std::cout << "\n";
        print_colored_line(separator, ConsoleColor::Cyan);
        print_colored_line(center("MIDI++ Interface"), ConsoleColor::Cyan);
        print_colored_line(separator, ConsoleColor::Cyan);
        std::cout << "\n";

        print_colored_line(center("Controls"), ConsoleColor::LightBlue);
        print_colored_line(separator, ConsoleColor::Cyan);

        std::unordered_map<std::string, std::string> hardcoded_descriptions = {
            {"PLAY_PAUSE", "Play/Pause"},
            {"REWIND", "Rewind"},
            {"SKIP", "Skip ahead"},
            {"SPEED_UP", "Speed up"},
            {"SLOW_DOWN", "Slow down"},
            {"LOAD_NEW_SONG", "Load new song"},
            {"TOGGLE_88_KEY_MODE", "Toggle 88 Key mode"},
            {"TOGGLE_VOLUME_ADJUSTMENT", "Toggle Automatic Volume"},
            {"TOGGLE_TRANSPOSE_ADJUSTMENT", "Toggle Transpose Engine"},
            {"RESTART_SONG", "Restart current song"},
            {"STOP_AND_EXIT", "Stop and exit"},
            {"TOGGLE_SUSTAIN_MODE", "Toggle Sustain Mode"},
            {"TOGGLE_VELOCITY_KEYPRESS", "Toggle Velocity Adjustment"},
           { "TOGGLE_TRANSPOSE_KEY", "Toggle 61-Key Transpose" }

        };
        std::vector<std::pair<std::string, std::string>> f_key_controls;
        std::vector<std::pair<std::string, std::string>> other_controls;

        for (const auto& [action, key] : config.controls) {
            std::string display_key = key;
            if (key.substr(0, 3) == "VK_") {
                display_key = key.substr(3);
            }
            if (display_key[0] == 'F' && isdigit(display_key[1])) {
                f_key_controls.emplace_back(display_key, action);
            }
            else {
                other_controls.emplace_back(display_key, action);
            }
        }
        std::sort(f_key_controls.begin(), f_key_controls.end(), [](const auto& a, const auto& b) {
            int a_num = std::stoi(a.first.substr(1)); // "F1" -> 1, "F10" -> 10
            int b_num = std::stoi(b.first.substr(1));
            return a_num < b_num;
            });

        std::sort(other_controls.begin(), other_controls.end());

        int max_key_length = 0;
        for (const auto& [key, _] : f_key_controls) {
            if (key.length() > max_key_length) {
                max_key_length = key.length();
            }
        }
        for (const auto& [key, _] : other_controls) {
            if (key.length() > max_key_length) {
                max_key_length = key.length();
            }
        }

        int col_width = width / 2 - 2;
        int key_padding = max_key_length + 4; 
        size_t max_lines = std::max(f_key_controls.size(), other_controls.size());
        for (size_t i = 0; i < max_lines; ++i) {
            setcolor(ConsoleColor::White);
            if (i < f_key_controls.size()) {
                std::string desc1 = hardcoded_descriptions[f_key_controls[i].second];
                std::cout << "  " << std::setw(key_padding) << std::left << f_key_controls[i].first << ": " << std::setw(col_width - key_padding) << std::left << desc1;
            }
            else {
                std::cout << "  " << std::setw(col_width) << " ";
            }
            if (i < other_controls.size()) {
                std::string desc2 = hardcoded_descriptions[other_controls[i].second];
                std::cout << " | " << std::setw(key_padding) << std::left << other_controls[i].first << ": " << std::setw(col_width - key_padding) << std::left << desc2;
            }

            std::cout << "\n";
        }

        std::cout << "\n";
        print_colored_line(separator, ConsoleColor::Cyan);
        print_colored_line(center("MIDI File Details"), ConsoleColor::Cyan);
        print_colored_line(separator, ConsoleColor::Cyan);

        auto print_detail = [this](const std::string& label, const auto& value) {
            setcolor(ConsoleColor::LightBlue);
            std::cout << std::setw(25) << std::left << label;
            setcolor(ConsoleColor::White);

            std::string valueStr = toString(value);
            if (valueStr.length() > 77) {
                valueStr = valueStr.substr(0, 77) + "...";
            }
            std::cout << ": " << valueStr << "\n";
            };
        std::string note_handling_mode_str;
        switch (config.playback.noteHandlingMode) {
        case NoteHandlingMode::FIFO:
            note_handling_mode_str = "FIFO (First In, First Out)";
            break;
        case NoteHandlingMode::LIFO:
            note_handling_mode_str = "LIFO (Last In, First Out)";
            break;
        case NoteHandlingMode::NoHandling:
            note_handling_mode_str = "No Handling (Direct Note On/Off)";
            break;
        }
        print_detail("File", midi_file_path);
        print_detail("Format", midi_file.format);
        print_detail("Number of Tracks", midi_file.numTracks);
        print_detail("Division", midi_file.division);
        print_detail("Tempo Changes", midi_file.tempoChanges.size());
        print_detail("Time Signatures", midi_file.timeSignatures.size());
        print_detail("Key Signatures", midi_file.keySignatures.size());
        print_detail("Legit Mode", config.legit_mode.ENABLED ? "Enabled" : "Disabled");
        if (config.legit_mode.ENABLED) {
            print_detail("Timing Variation", std::to_string(config.legit_mode.TIMING_VARIATION * 100) + "%");
            print_detail("Note Skip Chance", std::to_string(config.legit_mode.NOTE_SKIP_CHANCE * 100) + "%");
            print_detail("Extra Delay Chance", std::to_string(config.legit_mode.EXTRA_DELAY_CHANCE * 100) + "%");
            print_detail("Extra Delay Range",
                std::to_string(config.legit_mode.EXTRA_DELAY_MIN) + "s - " +
                std::to_string(config.legit_mode.EXTRA_DELAY_MAX) + "s");
        }
        print_detail("Filter Drums", config.midi.FILTER_DRUMS ? "Enabled" : "Disabled");
        print_detail("Note Handling Mode", note_handling_mode_str);

        std::cout << "\n";
        print_colored_line(separator, ConsoleColor::Cyan);
        print_colored_line(center("Press [DELETE] to start playback"), ConsoleColor::White);
        print_colored_line(center("Ensure your mouse is focused on Roblox."), ConsoleColor::White);
        print_colored_line(separator, ConsoleColor::Cyan);
        setcolor(ConsoleColor::Default);
    }
    void process_tracks(const MidiFile& midi_file) {
        note_events.clear();
        tempo_changes.clear();
        timeSignatures.clear();
        int drums = 0;
        long double current_time = 0.0L;
        int ticks_per_beat = midi_file.division;
        long double tempo = 500000.0L;
        int last_tick = 0;
        uint8_t current_numerator = 4;
        uint8_t current_denominator = 4;
        std::vector<int> all_notes;
        std::vector<long double> all_durations;
        std::vector<std::pair<int, MidiEvent>> all_events;
        std::unordered_map<int, bool> sustain_pedal_state;
        long double first_musical_event_time = -1.0L;
        long double last_musical_event_time = 0.0L;

        for (const auto& track : midi_file.tracks) {
            for (const auto& event : track.events) {
                all_events.emplace_back(event.absoluteTick, event);
            }
        }

        std::sort(all_events.begin(), all_events.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::unordered_map<int, std::unordered_map<int, std::vector<long double>>> active_notes;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> timing_variation(1.0 - config.legit_mode.TIMING_VARIATION, 1.0 + config.legit_mode.TIMING_VARIATION);
        std::uniform_real_distribution<> note_skip(0.0, 1.0);
        std::uniform_real_distribution<> extra_delay(0.0, 1.0);
        std::uniform_real_distribution<> extra_delay_amount(config.legit_mode.EXTRA_DELAY_MIN, config.legit_mode.EXTRA_DELAY_MAX);

        for (const auto& [tick, event] : all_events) {
            long double elapsed_time = tick2second(tick - last_tick, ticks_per_beat, tempo);

            if (config.legit_mode.ENABLED) {
                elapsed_time *= static_cast<long double>(timing_variation(gen));
            }

            current_time += elapsed_time;
            last_tick = event.absoluteTick;

            if (event.status == 0xFF && event.data1 == 0x51 && event.metaData.size() == 3) {
                tempo = static_cast<long double>((event.metaData[0] << 16) | (event.metaData[1] << 8) | event.metaData[2]);
                tempo_changes.push_back({ tick, static_cast<double>(tempo) });
            }
            else if (event.status == 0xFF && event.data1 == 0x58 && event.metaData.size() == 4) {
                current_numerator = event.metaData[0];
                current_denominator = static_cast<uint8_t>(1 << event.metaData[1]);
                timeSignatures.push_back(TimeSignature{
                    static_cast<uint32_t>(tick),
                    current_numerator,
                    current_denominator,
                    event.metaData[2],
                    event.metaData[3]
                    });
            }
            else if ((event.status & 0xF0) == 0xB0 && event.data1 == 64) {
                int channel = event.status & 0x0F;
                int sustainValue = event.data2;
                add_sustain_event(current_time, channel, sustainValue);
            }
            else if ((event.status & 0xF0) == 0x90 || (event.status & 0xF0) == 0x80) {
                int note = event.data1;
                int channel = event.status & 0x0F;
                int velocity = event.data2;

                std::string note_name = get_note_name(note);
                if (config.midi.FILTER_DRUMS && channel == 9) { //Channel 10 is USUALLY reserved for drums, this has been the case since the 90's according to this (https://www.soundonsound.com/forum/viewtopic.php?t=42965&start=12) this is to preserve GM compatibility so this will be the easier solution that would work with MOST normal midis.
                    continue;
                }

                if (config.legit_mode.ENABLED) {
                    if (note_skip(gen) < config.legit_mode.NOTE_SKIP_CHANCE) {
                        continue;
                    }

                    if (extra_delay(gen) < config.legit_mode.EXTRA_DELAY_CHANCE) {
                        long double delay = static_cast<long double>(extra_delay_amount(gen));
                        current_time += delay;
                    }
                }

                if (first_musical_event_time < 0.0L) {
                    first_musical_event_time = current_time;
                }
                last_musical_event_time = current_time;

                if ((event.status & 0xF0) == 0x90 && velocity > 0) {
                    handle_note_on(current_time, channel, note, velocity, active_notes);
                }
                else {
                    handle_note_off(current_time, channel, note, velocity, active_notes);
                }
            }
        }

        release_all_active_notes(current_time, active_notes);

        std::stable_sort(note_events.begin(), note_events.end());
    }

    void handle_note_on(long double current_time, int channel, int note, int velocity,
        std::unordered_map<int, std::unordered_map<int, std::vector<long double>>>& active_notes) {
        std::string note_name = get_note_name(note);
        active_notes[channel][note].push_back(current_time);
        add_note_event(current_time, note_name, "press", velocity);
    }
    void handle_note_off(long double current_time, int channel, int note, int velocity,
        std::unordered_map<int, std::unordered_map<int, std::vector<long double>>>& active_notes) {
        std::string note_name = get_note_name(note);

        if (config.playback.noteHandlingMode == NoteHandlingMode::NoHandling) {
            // No handling of stacked notes, just add the note off event directly
            add_note_event(current_time, note_name, "release", velocity);
            return;
        }

        if (active_notes[channel].find(note) != active_notes[channel].end() && !active_notes[channel][note].empty()) {
            add_note_event(current_time, note_name, "release", velocity);

            if (config.playback.noteHandlingMode == NoteHandlingMode::LIFO) {
                active_notes[channel][note].pop_back();  // LIFO: Remove the most recent start time
            }
            else if (config.playback.noteHandlingMode == NoteHandlingMode::FIFO) {
                active_notes[channel][note].erase(active_notes[channel][note].begin());  // FIFO: Remove the oldest start time
            }
        }
    }

    void release_all_active_notes(long double current_time,
        std::unordered_map<int, std::unordered_map<int, std::vector<long double>>>& active_notes) {

        if (config.playback.noteHandlingMode == NoteHandlingMode::NoHandling) {
            // No special handling needed; clear all active notes directly
            active_notes.clear();
            return;
        }

        for (auto& [channel, notes] : active_notes) {
            for (auto& [note, start_times] : notes) {
                std::string note_name = get_note_name(note);
                while (!start_times.empty()) {
                    add_note_event(current_time, note_name, "release", 0);

                    if (config.playback.noteHandlingMode == NoteHandlingMode::LIFO) {
                        start_times.pop_back();  // LIFO
                    }
                    else {
                        start_times.erase(start_times.begin());  // FIFO
                    }
                }
            }
        }
        active_notes.clear();
    }


#pragma float_control(precise, on)  
    __forceinline long double tick2second(uint64_t ticks, uint16_t ticks_per_beat, long double tempo) noexcept {
        constexpr long double MICROS_PER_SECOND = 1000000.0L;
        const long double inv_ticks_per_beat = 1.0L / static_cast<long double>(ticks_per_beat);
        const long double seconds_per_tick = (tempo * inv_ticks_per_beat) / MICROS_PER_SECOND;
        return std::fmal(static_cast<long double>(ticks), seconds_per_tick, 0.0L);
    }
#pragma float_control(precise, off)

    inline std::string get_note_name(int midi_note) {
        int octave = (midi_note / 12) - 1;
        int note = midi_note % 12;
        if (note >= 0 && note < NOTE_NAMES.size()) {
            return std::string(NOTE_NAMES[note]) + std::to_string(octave);
        }
        else {
            return "Unknown";
        }
    }

    void add_sustain_event(double time, int channel, int sustainValue) {
        note_events.push_back(std::make_tuple(time, "sustain",
            sustainValue >= config.sustain.SUSTAIN_CUTOFF ? "press" : "release",
            sustainValue | (channel << 8)));
    }

    void add_note_event(double time, const std::string& note, const std::string& action, int velocity) {
        note_events.push_back({ time, note, action, velocity });
    }

    void AdjustVolumeBasedOnVelocity(int velocity) noexcept {
        if (velocity < 0 || velocity >= static_cast<int>(volume_lookup.size())) {
            std::cerr << "error WTF32 report to dev pls thanks" << std::endl;
            return;
        }
        int target_volume = volume_lookup[velocity];
        int volume_change = target_volume - current_volume.load(std::memory_order_relaxed);

        if (std::abs(volume_change) >= config.volume.VOLUME_STEP) {
            WORD scan_code = volume_change > 0 ? volume_up_key_code : volume_down_key_code;
            int steps = std::abs(volume_change) / config.volume.VOLUME_STEP;

            for (int i = 0; i < steps; ++i) {
                arrowsend(scan_code, true);
            }

            current_volume.store(target_volume, std::memory_order_relaxed);
            last_adjustment_time = std::chrono::steady_clock::now();
        }
    }
};

int main() {
    SetConsoleTitle(L"MIDI++ v1.0.2");

    try {
        VirtualPianoPlayer player;
        player.main();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        std::cerr << "The program will now exit." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
