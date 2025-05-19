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
    struct AutoTranspose {
        bool ENABLED = false;
        std::string TRANSPOSE_UP_KEY = "VK_UP";   // Default to Up Arrow
        std::string TRANSPOSE_DOWN_KEY = "VK_DOWN"; // Default to Down Arrow

        void validate() const;
    };

    struct AutoplayerTimingAccuracy {
        int MAX_PASSES = 20;
        double MEASURE_SEC = 1.0;

        void validate() const;
    };

    struct UISettings {
        bool alwaysOnTop = false;
    };

    struct MIDISettings {
        bool DETECT_DRUMS = true;

        void validate() const;
    };

    struct HotkeySettings {
        std::string SUSTAIN_KEY = "VK_SPACE";
        std::string VOLUME_UP_KEY = "VK_RIGHT";
        std::string VOLUME_DOWN_KEY = "VK_LEFT";
        std::string PLAY_PAUSE_KEY = "VK_F1";      // Added default for play/pause
        std::string REWIND_KEY = "VK_F2";          // Added default for rewind
        std::string SKIP_KEY = "VK_F3";            // Added default for skip
        std::string EMERGENCY_EXIT_KEY = "VK_F4"; // Added default for emergency exit
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
        AutoTranspose auto_transpose;
        HotkeySettings hotkeys;
        UISettings ui;
        AutoplayerTimingAccuracy autoplayer_timing;
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
    void to_json(nlohmann::json& j, const AutoTranspose& l);
    void from_json(const nlohmann::json& j, AutoTranspose& l);
    void to_json(nlohmann::json& j, const AutoplayerTimingAccuracy& a);
    void from_json(const nlohmann::json& j, AutoplayerTimingAccuracy& a);
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