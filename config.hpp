#pragma once

#include <string>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <optional>
#include "json.hpp"

namespace midi {

    // Forward declarations
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

    enum class NoteHandlingMode {
        FIFO,
        LIFO,
        NoHandling
    };

    // Configuration structures
    struct VolumeSettings {
        int MIN_VOLUME = 10;
        int MAX_VOLUME = 200;
        int INITIAL_VOLUME = 100;
        int VOLUME_STEP = 10;
        int ADJUSTMENT_INTERVAL_MS = 50;

        void validate() const;
    };

    struct LegitModeSettings {
        bool ENABLED = false;
        double TIMING_VARIATION = 0.1;
        double NOTE_SKIP_CHANCE = 0.02;
        double EXTRA_DELAY_CHANCE = 0.05;
        double EXTRA_DELAY_MIN = 0.05;
        double EXTRA_DELAY_MAX = 0.2;

        void validate() const;
    };

    struct UISettings {
        bool alwaysOnTop = false;
    };
    struct MIDISettings {
        bool FILTER_DRUMS = true;

        void validate() const;
    };

    struct HotkeySettings {
        std::string SUSTAIN_KEY = "VK_SPACE";
        std::string VOLUME_UP_KEY = "VK_RIGHT";
        std::string VOLUME_DOWN_KEY = "VK_LEFT";

        void validate() const;
    };

    struct CustomVelocityCurve {
        std::string name;
        std::array<int, 32> velocityValues;
    };

    // Modify PlaybackSettings
    struct PlaybackSettings {
        VelocityCurveType velocityCurve = VelocityCurveType::LinearCoarse;
        NoteHandlingMode noteHandlingMode = NoteHandlingMode::LIFO;
        std::vector<CustomVelocityCurve> customVelocityCurves; 
        void validate() const;
    };

    class Config {
    public:
        MIDISettings midi;
        PlaybackSettings playback;
        VolumeSettings volume;
        LegitModeSettings legit_mode;
        HotkeySettings hotkeys;
        UISettings ui;
        std::map<std::string, std::map<std::string, std::string>> key_mappings;
        std::map<std::string, std::string> controls;
        std::vector<std::string> playlistFiles;

        static Config& getInstance();

        void loadFromFile(const std::filesystem::path& path);
        void saveToFile(const std::filesystem::path& path) const;
        void validate() const;
        void setDefaults();

        // Conversion methods made public and static
        static NoteHandlingMode stringToNoteHandlingMode(const std::string& mode);
        static std::string noteHandlingModeToString(NoteHandlingMode mode);

        // Delete copy constructor and assignment operator
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

    private:
        Config() = default;

        void validateKeyMappings() const;
    };

    // JSON conversion functions declarations
    void to_json(nlohmann::json& j, const VolumeSettings& v);
    void from_json(const nlohmann::json& j, VolumeSettings& v);
    void to_json(nlohmann::json& j, const LegitModeSettings& l);
    void from_json(const nlohmann::json& j, LegitModeSettings& l);
    void to_json(nlohmann::json& j, const MIDISettings& m);
    void from_json(const nlohmann::json& j, MIDISettings& m);
    void to_json(nlohmann::json& j, const HotkeySettings& h);
    void from_json(const nlohmann::json& j, HotkeySettings& h);
    void to_json(nlohmann::json& j, const PlaybackSettings& p);
    void from_json(const nlohmann::json& j, PlaybackSettings& p);
    void to_json(nlohmann::json& j, const Config& c);
    void from_json(const nlohmann::json& j, Config& c);
    void to_json(nlohmann::json& j, const UISettings& ui);
    void from_json(const nlohmann::json& j, UISettings& ui);

} // namespace midi