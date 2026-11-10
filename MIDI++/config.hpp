#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

namespace midi {

    class ConfigException : public std::runtime_error {
    public:
        explicit ConfigException(const std::string& message) : std::runtime_error(message) {}
    };

    enum class VelocityCurveType {
        LinearCoarse = 0,
        LinearFine = 1,
        ImprovedLowVolume = 2,
        Logarithmic = 3,
        Exponential = 4,
        Custom = 5
    };

    enum class NoteHandlingMode { FIFO, LIFO, NoHandling };
    enum class SustainMode { Ignore, Down, Up, Hold, Always };
    enum class OutputMode { Keys, MidiConnect };

    struct VolumeSettings {
        int MIN_VOLUME = 10;
        int MAX_VOLUME = 200;
        int INITIAL_VOLUME = 100;
        int VOLUME_STEP = 10;
        int ADJUSTMENT_INTERVAL_MS = 50;
    };

    struct AutoTranspose {
        bool ENABLED = false;
        std::string TRANSPOSE_UP_KEY = "VK_UP";
        std::string TRANSPOSE_DOWN_KEY = "VK_DOWN";
    };

    struct LegitModeSettings {
        bool ENABLED = false;
        double TIMING_VARIATION = 0.1;
        double NOTE_SKIP_CHANCE = 0.02;
        double EXTRA_DELAY_CHANCE = 0.05;
        double EXTRA_DELAY_MIN = 0.05;
        double EXTRA_DELAY_MAX = 0.2;
    };

    enum class LiveWait { Block, SpinThenBlock, Spin };

    struct MIDISettings {
        bool DETECT_DRUMS = true;
        bool kernelStreaming = true;
        LiveWait liveWait = LiveWait::Block;
    };

    struct HotkeySettings {
        std::string SUSTAIN_KEY = "VK_SPACE";
        std::string VOLUME_UP_KEY = "VK_RIGHT";
        std::string VOLUME_DOWN_KEY = "VK_LEFT";
        std::string PLAY_PAUSE_KEY = "VK_F1";
        std::string REWIND_KEY = "VK_F2";
        std::string SKIP_KEY = "VK_F3";
        std::string EMERGENCY_EXIT_KEY = "VK_F4";
    };

    struct CustomVelocityCurve {
        std::string name;
        std::array<int, 32> velocityValues{};
    };

    struct PlaybackSettings {
        VelocityCurveType velocityCurve = VelocityCurveType::LinearCoarse;
        NoteHandlingMode noteHandlingMode = NoteHandlingMode::LIFO;
        std::vector<CustomVelocityCurve> customVelocityCurves;
        int velocityCurveIndex = 0;
        OutputMode output = OutputMode::Keys;
        bool keys88 = false;
        bool velocityKeys = false;
        bool outOfRange = false;
        SustainMode sustain = SustainMode::Ignore;
        int sustainCutoff = 64;
        bool sostenuto = true;
        bool gameWindowOnly = true;
        int batchMs = -1;
        bool typedKeys = false;
        bool autoVolume = false;
    };

    struct UISettings {
        bool alwaysOnTop = false;
        int opacity = 255;
        int sortMode = 0;
        std::string midiDevice;
        int midiChannel = -1;
        int darkMode = -1;
        bool compact = false;
    };

    class Config {
    public:
        MIDISettings midi;
        PlaybackSettings playback;
        VolumeSettings volume;
        AutoTranspose auto_transpose;
        LegitModeSettings legit;
        HotkeySettings hotkeys;
        UISettings ui;
        std::map<std::string, std::map<std::string, std::string>> key_mappings;
        std::vector<std::string> playlistFiles;

        static Config& getInstance();

        std::vector<std::string> loadFromFile(const std::filesystem::path& path);
        void saveToFile(const std::filesystem::path& path) const;
        void setDefaults();

        static NoteHandlingMode stringToNoteHandlingMode(const std::string& mode);
        static std::string noteHandlingModeToString(NoteHandlingMode mode);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

    private:
        Config() { setDefaults(); }
        nlohmann::json raw_;
    };

    int noteNameToMidi(std::string_view name);
    std::string midiToNoteName(int note);

}
