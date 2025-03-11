#include "config.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace midi {

    using json = nlohmann::json;

    void VolumeSettings::validate() const {
        if (MIN_VOLUME < 0) throw ConfigException("MIN_VOLUME cannot be negative");
        if (MAX_VOLUME > 200) throw ConfigException("MAX_VOLUME cannot exceed 200");
        if (MIN_VOLUME > MAX_VOLUME) throw ConfigException("MIN_VOLUME cannot be greater than MAX_VOLUME");
        if (INITIAL_VOLUME < MIN_VOLUME || INITIAL_VOLUME > MAX_VOLUME)
            throw ConfigException("INITIAL_VOLUME must be between MIN_VOLUME and MAX_VOLUME");
        if (VOLUME_STEP <= 0) throw ConfigException("VOLUME_STEP must be positive");
        if (ADJUSTMENT_INTERVAL_MS < 0) throw ConfigException("ADJUSTMENT_INTERVAL_MS cannot be negative");
    }

    void LegitModeSettings::validate() const {
        if (TIMING_VARIATION < 0.0 || TIMING_VARIATION > 1.0)
            throw ConfigException("TIMING_VARIATION must be between 0.0 and 1.0");
        if (NOTE_SKIP_CHANCE < 0.0 || NOTE_SKIP_CHANCE > 1.0)
            throw ConfigException("NOTE_SKIP_CHANCE must be between 0.0 and 1.0");
        if (EXTRA_DELAY_CHANCE < 0.0 || EXTRA_DELAY_CHANCE > 1.0)
            throw ConfigException("EXTRA_DELAY_CHANCE must be between 0.0 and 1.0");
        if (EXTRA_DELAY_MIN < 0.0) throw ConfigException("EXTRA_DELAY_MIN cannot be negative");
        if (EXTRA_DELAY_MAX < EXTRA_DELAY_MIN)
            throw ConfigException("EXTRA_DELAY_MAX cannot be less than EXTRA_DELAY_MIN");
    }

    void AutoTranspose::validate() const {
        if (TRANSPOSE_UP_KEY.empty() || TRANSPOSE_DOWN_KEY.empty()) {
            throw ConfigException("Transpose hotkeys cannot be empty");
        }
    }

    void MIDISettings::validate() const {
        //abcdefg
    }

    void HotkeySettings::validate() const {
        auto validateKey = [](const std::string& key) {
            if (key.empty()) throw ConfigException("Hotkey cannot be empty");
            if (key.find("VK_") != 0) throw ConfigException("Hotkey must start with 'VK_'");
            };

        validateKey(SUSTAIN_KEY);
        validateKey(VOLUME_UP_KEY);
        validateKey(VOLUME_DOWN_KEY);
    }

    void PlaybackSettings::validate() const {
        for (const auto& curve : customVelocityCurves) {
            if (curve.name.empty()) {
                throw ConfigException("Custom velocity curve name cannot be empty");
            }

            for (size_t i = 0; i < curve.velocityValues.size(); i++) {
                if (curve.velocityValues[i] < 0 || curve.velocityValues[i] > 127) {
                    throw ConfigException("Velocity value in curve '" + curve.name +
                        "' must be between 0 and 127");
                }
            }
        }
    }
    Config& Config::getInstance() {
        static Config instance;
        return instance;
    }

    void Config::loadFromFile(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            throw ConfigException("Config file not found: " + path.string());
        }

        try {
            std::ifstream file(path);
            json j;
            file >> j;
            from_json(j, *this);
            validate();
        }
        catch (const json::exception& e) {
            throw ConfigException("JSON parsing error: " + std::string(e.what()));
        }
    }

    void Config::saveToFile(const std::filesystem::path& path) const {
        try {
            json j;
            to_json(j, *this);
            std::ofstream file(path);
            file << j.dump(4);
        }
        catch (const std::exception& e) {
            throw ConfigException("Failed to save config: " + std::string(e.what()));
        }
    }

    void Config::validate() const {
        try {
            midi.validate();
            playback.validate();
            volume.validate();
            legit_mode.validate();
            auto_transpose.validate();
            hotkeys.validate();
            validateKeyMappings();
        }
        catch (const ConfigException& e) {
            throw ConfigException("Configuration validation failed: " + std::string(e.what()));
        }
    }

    void Config::validateKeyMappings() const {
        if (key_mappings.find("LIMITED") == key_mappings.end())
            throw ConfigException("Missing LIMITED key mappings");
        if (key_mappings.find("FULL") == key_mappings.end())
            throw ConfigException("Missing FULL key mappings");

        for (const auto& [mode, mappings] : key_mappings) {
            if (mappings.empty())
                throw ConfigException("Empty key mappings for mode: " + mode);

            for (const auto& [note, key] : mappings) {
                if (note.empty() || key.empty())
                    throw ConfigException("Invalid key mapping in mode " + mode);
            }
        }
    }

    NoteHandlingMode Config::stringToNoteHandlingMode(const std::string& mode) {
        static const std::map<std::string, NoteHandlingMode> mapping = {
            {"FIFO", NoteHandlingMode::FIFO},
            {"LIFO", NoteHandlingMode::LIFO},
            {"NoHandling", NoteHandlingMode::NoHandling}
        };

        auto it = mapping.find(mode);
        if (it == mapping.end())
            throw ConfigException("Invalid note handling mode: " + mode);
        return it->second;
    }
    std::string Config::noteHandlingModeToString(NoteHandlingMode mode) {
        switch (mode) {
        case NoteHandlingMode::FIFO: return "FIFO";
        case NoteHandlingMode::LIFO: return "LIFO";
        case NoteHandlingMode::NoHandling: return "NoHandling";
        default: throw ConfigException("Unknown note handling mode");
        }
    }

    void to_json(json& j, const VolumeSettings& v) {
        j = json{
            {"MIN_VOLUME", v.MIN_VOLUME},
            {"MAX_VOLUME", v.MAX_VOLUME},
            {"INITIAL_VOLUME", v.INITIAL_VOLUME},
            {"VOLUME_STEP", v.VOLUME_STEP},
            {"ADJUSTMENT_INTERVAL_MS", v.ADJUSTMENT_INTERVAL_MS}
        };
    }

    void from_json(const json& j, VolumeSettings& v) {
        j.at("MIN_VOLUME").get_to(v.MIN_VOLUME);
        j.at("MAX_VOLUME").get_to(v.MAX_VOLUME);
        j.at("INITIAL_VOLUME").get_to(v.INITIAL_VOLUME);
        j.at("VOLUME_STEP").get_to(v.VOLUME_STEP);
        j.at("ADJUSTMENT_INTERVAL_MS").get_to(v.ADJUSTMENT_INTERVAL_MS);
        v.validate();
    }

    void to_json(json& j, const LegitModeSettings& l) {
        j = json{
            {"ENABLED", l.ENABLED},
            {"TIMING_VARIATION", l.TIMING_VARIATION},
            {"NOTE_SKIP_CHANCE", l.NOTE_SKIP_CHANCE},
            {"EXTRA_DELAY_CHANCE", l.EXTRA_DELAY_CHANCE},
            {"EXTRA_DELAY_MIN", l.EXTRA_DELAY_MIN},
            {"EXTRA_DELAY_MAX", l.EXTRA_DELAY_MAX}
        };
    }

    void from_json(const json& j, LegitModeSettings& l) {
        j.at("ENABLED").get_to(l.ENABLED);
        j.at("TIMING_VARIATION").get_to(l.TIMING_VARIATION);
        j.at("NOTE_SKIP_CHANCE").get_to(l.NOTE_SKIP_CHANCE);
        j.at("EXTRA_DELAY_CHANCE").get_to(l.EXTRA_DELAY_CHANCE);
        j.at("EXTRA_DELAY_MIN").get_to(l.EXTRA_DELAY_MIN);
        j.at("EXTRA_DELAY_MAX").get_to(l.EXTRA_DELAY_MAX);
        l.validate();
    }

    void to_json(json& j, const AutoTranspose& at) {
        j = json{
            {"ENABLED", at.ENABLED},
            {"TRANSPOSE_UP_KEY", at.TRANSPOSE_UP_KEY},
            {"TRANSPOSE_DOWN_KEY", at.TRANSPOSE_DOWN_KEY}
        };
    }

    void from_json(const json& j, AutoTranspose& at) {
        j.at("ENABLED").get_to(at.ENABLED);
        j.at("TRANSPOSE_UP_KEY").get_to(at.TRANSPOSE_UP_KEY);
        j.at("TRANSPOSE_DOWN_KEY").get_to(at.TRANSPOSE_DOWN_KEY);
    }

    void to_json(json& j, const MIDISettings& m) {
        j = json{ {"FILTER_DRUMS", m.FILTER_DRUMS} };
    }

    void from_json(const json& j, MIDISettings& m) {
        j.at("FILTER_DRUMS").get_to(m.FILTER_DRUMS);
        m.validate();
    }
    void to_json(nlohmann::json& j, const UISettings& ui) {
        j = nlohmann::json{ {"alwaysOnTop", ui.alwaysOnTop} };
    }

    void from_json(const nlohmann::json& j, UISettings& ui) {
        j.at("alwaysOnTop").get_to(ui.alwaysOnTop);
    }

    void to_json(json& j, const HotkeySettings& h) {
        j = json{
            {"SUSTAIN_KEY", h.SUSTAIN_KEY},
            {"VOLUME_UP_KEY", h.VOLUME_UP_KEY},
            {"VOLUME_DOWN_KEY", h.VOLUME_DOWN_KEY}
        };
    }

    void from_json(const json& j, HotkeySettings& h) {
        j.at("SUSTAIN_KEY").get_to(h.SUSTAIN_KEY);
        j.at("VOLUME_UP_KEY").get_to(h.VOLUME_UP_KEY);
        j.at("VOLUME_DOWN_KEY").get_to(h.VOLUME_DOWN_KEY);
        h.validate();
    }
    void to_json(json& j, const PlaybackSettings& p) {
        j = json{
            {"STACKED_NOTE_HANDLING_MODE", Config::noteHandlingModeToString(p.noteHandlingMode)},
            {"CUSTOM_VELOCITY_CURVES", json::array()}
        };

        for (const auto& curve : p.customVelocityCurves) {
            j["CUSTOM_VELOCITY_CURVES"].push_back({
                {"name", curve.name},
                {"values", curve.velocityValues}
                });
        }
    }


    void from_json(const json& j, PlaybackSettings& p) {
        std::string mode = j.at("STACKED_NOTE_HANDLING_MODE").get<std::string>();
        p.noteHandlingMode = Config::stringToNoteHandlingMode(mode);
        //TODO: validate here probably too
        if (j.contains("CUSTOM_VELOCITY_CURVES")) {
            for (const auto& curveJson : j["CUSTOM_VELOCITY_CURVES"]) {
                CustomVelocityCurve customCurve;
                customCurve.name = curveJson["name"].get<std::string>();
                customCurve.velocityValues = curveJson["values"].get<std::array<int, 32>>();
                p.customVelocityCurves.push_back(customCurve);
            }
        }

        p.validate();
    }
    void to_json(json& j, const Config& c) {
        j = json{
            {"VOLUME_SETTINGS", c.volume},
            {"KEY_MAPPINGS", c.key_mappings},
            {"LEGIT_MODE_SETTINGS", c.legit_mode},
            {"AUTO_TRANSPOSE", c.auto_transpose},
            {"HOTKEY_SETTINGS", c.hotkeys},
            {"MIDI_SETTINGS", json{{"FILTER_DRUMS", c.midi.FILTER_DRUMS}}},
            {"STACKED_NOTE_HANDLING_MODE", Config::noteHandlingModeToString(c.playback.noteHandlingMode)},
            {"CUSTOM_VELOCITY_CURVES", json::array()},
            {"PLAYLIST_FILES", c.playlistFiles},
            {"UI_SETTINGS", c.ui}
        };

        for (const auto& curve : c.playback.customVelocityCurves) {
            j["CUSTOM_VELOCITY_CURVES"].push_back({
                {"name", curve.name},
                {"values", curve.velocityValues}
                });
        }
    }

    void from_json(const json& j, Config& c) {
        j.at("VOLUME_SETTINGS").get_to(c.volume);
        j.at("KEY_MAPPINGS").get_to(c.key_mappings);
        j.at("LEGIT_MODE_SETTINGS").get_to(c.legit_mode);
        j.at("AUTO_TRANSPOSE").get_to(c.auto_transpose);
        j.at("HOTKEY_SETTINGS").get_to(c.hotkeys);
        j.at("MIDI_SETTINGS").at("FILTER_DRUMS").get_to(c.midi.FILTER_DRUMS);

        if (j.contains("STACKED_NOTE_HANDLING_MODE")) {
            std::string mode = j.at("STACKED_NOTE_HANDLING_MODE").get<std::string>();
            c.playback.noteHandlingMode = Config::stringToNoteHandlingMode(mode);
        }

        if (j.contains("CUSTOM_VELOCITY_CURVES")) {
            const auto& curves = j.at("CUSTOM_VELOCITY_CURVES");
            c.playback.customVelocityCurves.clear();
            for (const auto& curveJson : curves) {
                CustomVelocityCurve customCurve;
                customCurve.name = curveJson["name"].get<std::string>();
                customCurve.velocityValues = curveJson["values"].get<std::array<int, 32>>();
                c.playback.customVelocityCurves.push_back(customCurve);
            }
        }

        if (j.contains("PLAYLIST_FILES") && j["PLAYLIST_FILES"].is_array()) {
            c.playlistFiles.clear();
            for (auto& item : j["PLAYLIST_FILES"]) {
                c.playlistFiles.push_back(item.get<std::string>());
            }
        }

        // New: read UI settings
        if (j.contains("UI_SETTINGS")) {
            j.at("UI_SETTINGS").get_to(c.ui);
        }

        // Validate at the end
        c.validate();
    }

    void Config::setDefaults() {
        // Volume settings
        volume = {
            10,     // MIN_VOLUME
            200,    // MAX_VOLUME
            100,    // INITIAL_VOLUME
            10,     // VOLUME_STEP
            50      // ADJUSTMENT_INTERVAL_MS
        };

        // Legit mode settings
        legit_mode = {
            false,  // ENABLED
            0.1,    // TIMING_VARIATION
            0.02,   // NOTE_SKIP_CHANCE
            0.05,   // EXTRA_DELAY_CHANCE
            0.05,   // EXTRA_DELAY_MIN
            0.2     // EXTRA_DELAY_MAX
        };

        // AutoTranspose settings
        auto_transpose = {
            false,      // ENABLED
            "VK_UP",    // TRANSPOSE_UP_KEY
            "VK_DOWN"   // TRANSPOSE_DOWN_KEY
        };

        // MIDI settings
        midi = { true }; // FILTER_DRUMS

        // Hotkey settings
        hotkeys = {
            "VK_SPACE",   // SUSTAIN_KEY
            "VK_RIGHT",   // VOLUME_UP_KEY
            "VK_LEFT"     // VOLUME_DOWN_KEY
        };



        // Setup default LIMITED key mappings
        key_mappings["LIMITED"] = {
            {"C2", "1"}, {"C#2", "!"}, {"D2", "2"}, {"D#2", "@"}, {"E2", "3"},
            {"F2", "4"}, {"F#2", "$"}, {"G2", "5"}, {"G#2", "%"}, {"A2", "6"},
            {"A#2", "^"}, {"B2", "7"}, {"C3", "8"}, {"C#3", "*"}, {"D3", "9"},
            {"D#3", "("}, {"E3", "0"}, {"F3", "q"}, {"F#3", "Q"}, {"G3", "w"},
            {"G#3", "W"}, {"A3", "e"}, {"A#3", "E"}, {"B3", "r"}, {"C4", "t"},
            {"C#4", "T"}, {"D4", "y"}, {"D#4", "Y"}, {"E4", "u"}, {"F4", "i"},
            {"F#4", "I"}, {"G4", "o"}, {"G#4", "O"}, {"A4", "p"}, {"A#4", "P"},
            {"B4", "a"}, {"C5", "s"}, {"C#5", "S"}, {"D5", "d"}, {"D#5", "D"},
            {"E5", "f"}, {"F5", "g"}, {"F#5", "G"}, {"G5", "h"}, {"G#5", "H"},
            {"A5", "j"}, {"A#5", "J"}, {"B5", "k"}, {"C6", "l"}, {"C#6", "L"},
            {"D6", "z"}, {"D#6", "Z"}, {"E6", "x"}, {"F6", "c"}, {"F#6", "C"},
            {"G6", "v"}, {"G#6", "V"}, {"A6", "b"}, {"A#6", "B"}, {"B6", "n"},
            {"C7", "m"}
        };

        // Setup default FULL key mappings with lower octaves
        key_mappings["FULL"] = {
            {"A0", "ctrl+1"}, {"A#0", "ctrl+2"}, {"B0", "ctrl+3"},
            {"C1", "ctrl+4"}, {"C#1", "ctrl+5"}, {"D1", "ctrl+6"},
            {"D#1", "ctrl+7"}, {"E1", "ctrl+8"}, {"F1", "ctrl+9"},
            {"F#1", "ctrl+0"}, {"G1", "ctrl+q"}, {"G#1", "ctrl+w"},
            {"A1", "ctrl+e"}, {"A#1", "ctrl+r"}, {"B1", "ctrl+t"}
        };

        // Copy all LIMITED mappings to FULL
        for (const auto& [note, key] : key_mappings["LIMITED"]) {
            key_mappings["FULL"][note] = key;
        }

        // Add higher octaves to FULL mapping
        std::map<std::string, std::string> high_notes = {
            {"C#7", "ctrl+y"}, {"D7", "ctrl+u"}, {"D#7", "ctrl+i"}, {"E7", "ctrl+o"},
            {"F7", "ctrl+p"}, {"F#7", "ctrl+a"}, {"G7", "ctrl+s"}, {"G#7", "ctrl+d"},
            {"A7", "ctrl+f"}, {"A#7", "ctrl+g"}, {"B7", "ctrl+h"}, {"C8", "ctrl+j"}
        };

        for (const auto& [note, key] : high_notes) {
            key_mappings["FULL"][note] = key;
        }
        validate();
    }

} // namespace midi