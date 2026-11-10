#include "config.hpp"
#include "Input.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace midi {

    using json = nlohmann::json;

    namespace {

        using Warnings = std::vector<std::string>;

        template <class T>
        void pick(const json& obj, const char* key, T& out, Warnings& warn, const std::string& where) {
            if (!obj.is_object())
                return;
            auto it = obj.find(key);
            if (it == obj.end())
                return;
            try {
                out = it->get<T>();
            }
            catch (const json::exception&) {
                warn.push_back(where + key + " has the wrong type, using the default");
            }
        }

        template <class T>
        std::string text(T v) {
            std::ostringstream s;
            s << v;
            return s.str();
        }

        template <class T>
        void clamp_to(T& v, T lo, T hi, const char* key, Warnings& warn, const std::string& where) {
            if (v < lo || v > hi) {
                T c = std::clamp(v, lo, hi);
                warn.push_back(where + key + " = " + text(v) + " is out of range, using " + text(c));
                v = c;
            }
        }

        std::string upper(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }

        const char* waitName(LiveWait m) {
            switch (m) {
            case LiveWait::Spin: return "SPIN";
            case LiveWait::SpinThenBlock: return "SPIN_THEN_BLOCK";
            default: return "BLOCK";
            }
        }

        const char* sustainName(SustainMode m) {
            switch (m) {
            case SustainMode::Down: return "DOWN";
            case SustainMode::Up: return "UP";
            case SustainMode::Hold: return "HOLD";
            case SustainMode::Always: return "ALWAYS";
            default: return "IGNORE";
            }
        }

        void hotkey(const json& obj, const char* key, std::string& out, Warnings& warn) {
            std::string v = out;
            pick(obj, key, v, warn, "HOTKEY_SETTINGS.");
            if (input::parse_vk(v))
                out = v;
            else
                warn.push_back(std::string("HOTKEY_SETTINGS.") + key + " '" + v + "' is not a key, using " + out);
        }

    }

    Config& Config::getInstance() {
        static Config instance;
        return instance;
    }

    int noteNameToMidi(std::string_view name) {
        static constexpr int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };
        if (name.size() < 2)
            return -1;
        char l = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (l < 'A' || l > 'G')
            return -1;
        int pc = pcs[l - 'A'];
        size_t i = 1;
        if (name[i] == '#') { ++pc; ++i; }
        else if (name[i] == 'b') { --pc; ++i; }
        if (i >= name.size())
            return -1;
        bool neg = name[i] == '-';
        if (neg && ++i >= name.size())
            return -1;
        int oct = 0;
        for (; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9')
                return -1;
            oct = oct * 10 + (name[i] - '0');
        }
        int n = ((neg ? -oct : oct) + 1) * 12 + pc;
        return n >= 0 && n < 128 ? n : -1;
    }

    std::string midiToNoteName(int note) {
        static constexpr const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (note < 0 || note > 127)
            return "?";
        return std::string(names[note % 12]) + std::to_string(note / 12 - 1);
    }

    NoteHandlingMode Config::stringToNoteHandlingMode(const std::string& mode) {
        std::string m = upper(mode);
        if (m == "FIFO")
            return NoteHandlingMode::FIFO;
        if (m == "NOHANDLING")
            return NoteHandlingMode::NoHandling;
        if (m == "LIFO")
            return NoteHandlingMode::LIFO;
        throw ConfigException("Invalid note handling mode: " + mode);
    }

    std::string Config::noteHandlingModeToString(NoteHandlingMode mode) {
        switch (mode) {
        case NoteHandlingMode::FIFO: return "FIFO";
        case NoteHandlingMode::NoHandling: return "NoHandling";
        default: return "LIFO";
        }
    }

    void Config::setDefaults() {
        midi = {};
        playback = {};
        volume = {};
        auto_transpose = {};
        legit = {};
        hotkeys = {};
        ui = {};
        playlistFiles.clear();
        key_mappings.clear();

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

        key_mappings["FULL"] = {
            {"A0", "ctrl+1"}, {"A#0", "ctrl+2"}, {"B0", "ctrl+3"},
            {"C1", "ctrl+4"}, {"C#1", "ctrl+5"}, {"D1", "ctrl+6"},
            {"D#1", "ctrl+7"}, {"E1", "ctrl+8"}, {"F1", "ctrl+9"},
            {"F#1", "ctrl+0"}, {"G1", "ctrl+q"}, {"G#1", "ctrl+w"},
            {"A1", "ctrl+e"}, {"A#1", "ctrl+r"}, {"B1", "ctrl+t"},
            {"C#7", "ctrl+y"}, {"D7", "ctrl+u"}, {"D#7", "ctrl+i"}, {"E7", "ctrl+o"},
            {"F7", "ctrl+p"}, {"F#7", "ctrl+a"}, {"G7", "ctrl+s"}, {"G#7", "ctrl+d"},
            {"A7", "ctrl+f"}, {"A#7", "ctrl+g"}, {"B7", "ctrl+h"}, {"C8", "ctrl+j"}
        };
        for (const auto& [note, key] : key_mappings["LIMITED"])
            key_mappings["FULL"][note] = key;
    }

    std::vector<std::string> Config::loadFromFile(const std::filesystem::path& path) {
        Warnings warn;
        setDefaults();
        raw_ = json::object();

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            warn.push_back("config.json not found, wrote a fresh one");
            return warn;
        }
        json j = json::parse(file, nullptr, false, true);
        file.close();
        if (j.is_discarded() || !j.is_object()) {
            std::error_code ec;
            std::filesystem::path bad = path;
            bad.replace_filename("config.broken.json");
            std::filesystem::rename(path, bad, ec);
            warn.push_back("config.json could not be read, moved it to config.broken.json and used the defaults");
            return warn;
        }
        raw_ = j;

        auto section = [&](const char* name) -> const json& {
            static const json empty = json::object();
            auto it = j.find(name);
            if (it == j.end())
                return empty;
            if (!it->is_object()) {
                warn.push_back(std::string(name) + " is not an object, using the defaults");
                return empty;
            }
            return *it;
        };

        {
            const json& s = section("VOLUME_SETTINGS");
            const std::string w = "VOLUME_SETTINGS.";
            pick(s, "MIN_VOLUME", volume.MIN_VOLUME, warn, w);
            pick(s, "MAX_VOLUME", volume.MAX_VOLUME, warn, w);
            pick(s, "INITIAL_VOLUME", volume.INITIAL_VOLUME, warn, w);
            pick(s, "VOLUME_STEP", volume.VOLUME_STEP, warn, w);
            pick(s, "ADJUSTMENT_INTERVAL_MS", volume.ADJUSTMENT_INTERVAL_MS, warn, w);
            clamp_to(volume.MAX_VOLUME, 0, 200, "MAX_VOLUME", warn, w);
            clamp_to(volume.MIN_VOLUME, 0, volume.MAX_VOLUME, "MIN_VOLUME", warn, w);
            clamp_to(volume.INITIAL_VOLUME, volume.MIN_VOLUME, volume.MAX_VOLUME, "INITIAL_VOLUME", warn, w);
            clamp_to(volume.VOLUME_STEP, 1, 200, "VOLUME_STEP", warn, w);
            clamp_to(volume.ADJUSTMENT_INTERVAL_MS, 0, 10000, "ADJUSTMENT_INTERVAL_MS", warn, w);
        }

        {
            const json& s = section("AUTO_TRANSPOSE");
            pick(s, "ENABLED", auto_transpose.ENABLED, warn, "AUTO_TRANSPOSE.");
            std::string up = auto_transpose.TRANSPOSE_UP_KEY, down = auto_transpose.TRANSPOSE_DOWN_KEY;
            pick(s, "TRANSPOSE_UP_KEY", up, warn, "AUTO_TRANSPOSE.");
            pick(s, "TRANSPOSE_DOWN_KEY", down, warn, "AUTO_TRANSPOSE.");
            if (input::parse_vk(up)) auto_transpose.TRANSPOSE_UP_KEY = up;
            else warn.push_back("AUTO_TRANSPOSE.TRANSPOSE_UP_KEY '" + up + "' is not a key");
            if (input::parse_vk(down)) auto_transpose.TRANSPOSE_DOWN_KEY = down;
            else warn.push_back("AUTO_TRANSPOSE.TRANSPOSE_DOWN_KEY '" + down + "' is not a key");
        }

        {
            const json& s = section("LEGIT_MODE_SETTINGS");
            const std::string w = "LEGIT_MODE_SETTINGS.";
            pick(s, "ENABLED", legit.ENABLED, warn, w);
            pick(s, "TIMING_VARIATION", legit.TIMING_VARIATION, warn, w);
            pick(s, "NOTE_SKIP_CHANCE", legit.NOTE_SKIP_CHANCE, warn, w);
            pick(s, "EXTRA_DELAY_CHANCE", legit.EXTRA_DELAY_CHANCE, warn, w);
            pick(s, "EXTRA_DELAY_MIN", legit.EXTRA_DELAY_MIN, warn, w);
            pick(s, "EXTRA_DELAY_MAX", legit.EXTRA_DELAY_MAX, warn, w);
            clamp_to(legit.TIMING_VARIATION, 0.0, 1.0, "TIMING_VARIATION", warn, w);
            clamp_to(legit.NOTE_SKIP_CHANCE, 0.0, 1.0, "NOTE_SKIP_CHANCE", warn, w);
            clamp_to(legit.EXTRA_DELAY_CHANCE, 0.0, 1.0, "EXTRA_DELAY_CHANCE", warn, w);
            clamp_to(legit.EXTRA_DELAY_MIN, 0.0, 10.0, "EXTRA_DELAY_MIN", warn, w);
            clamp_to(legit.EXTRA_DELAY_MAX, 0.0, 10.0, "EXTRA_DELAY_MAX", warn, w);
            if (legit.EXTRA_DELAY_MAX < legit.EXTRA_DELAY_MIN)
                std::swap(legit.EXTRA_DELAY_MIN, legit.EXTRA_DELAY_MAX);
        }

        {
            const json& s = section("MIDI_SETTINGS");
            const std::string w = "MIDI_SETTINGS.";
            std::string wait = waitName(midi.liveWait);
            pick(s, "DETECT_DRUMS", midi.DETECT_DRUMS, warn, w);
            pick(s, "KERNEL_STREAMING", midi.kernelStreaming, warn, w);
            pick(s, "LIVE_WAIT_MODE", wait, warn, w);
            wait = upper(wait);
            midi.liveWait = wait == "SPIN" ? LiveWait::Spin : wait == "SPIN_THEN_BLOCK" ? LiveWait::SpinThenBlock : LiveWait::Block;
        }

        {
            const json& s = section("HOTKEY_SETTINGS");
            hotkey(s, "SUSTAIN_KEY", hotkeys.SUSTAIN_KEY, warn);
            hotkey(s, "VOLUME_UP_KEY", hotkeys.VOLUME_UP_KEY, warn);
            hotkey(s, "VOLUME_DOWN_KEY", hotkeys.VOLUME_DOWN_KEY, warn);
            hotkey(s, "PLAY_PAUSE_KEY", hotkeys.PLAY_PAUSE_KEY, warn);
            hotkey(s, "REWIND_KEY", hotkeys.REWIND_KEY, warn);
            hotkey(s, "SKIP_KEY", hotkeys.SKIP_KEY, warn);
            hotkey(s, "EMERGENCY_EXIT_KEY", hotkeys.EMERGENCY_EXIT_KEY, warn);
        }

        if (auto it = j.find("STACKED_NOTE_HANDLING_MODE"); it != j.end()) {
            try {
                playback.noteHandlingMode = stringToNoteHandlingMode(it->get<std::string>());
            }
            catch (const std::exception&) {
                warn.push_back("STACKED_NOTE_HANDLING_MODE must be FIFO, LIFO or NoHandling, using LIFO");
            }
        }

        if (auto it = j.find("CUSTOM_VELOCITY_CURVES"); it != j.end() && it->is_array()) {
            for (const json& c : *it) {
                CustomVelocityCurve curve;
                if (!c.is_object() || !c.contains("name") || !c["name"].is_string() || !c.contains("values")) {
                    warn.push_back("CUSTOM_VELOCITY_CURVES has a curve without a name or values, skipped it");
                    continue;
                }
                curve.name = c["name"].get<std::string>();
                const json& v = c["values"];
                if (curve.name.empty() || !v.is_array() || v.size() != 32) {
                    warn.push_back("velocity curve '" + curve.name + "' needs a name and 32 values, skipped it");
                    continue;
                }
                bool ok = true;
                for (size_t i = 0; i < 32 && ok; ++i) {
                    if (!v[i].is_number()) {
                        ok = false;
                        break;
                    }
                    curve.velocityValues[i] = std::clamp(v[i].get<int>(), 0, 127);
                }
                if (ok)
                    playback.customVelocityCurves.push_back(std::move(curve));
                else
                    warn.push_back("velocity curve '" + curve.name + "' has a value that is not a number, skipped it");
            }
        }

        {
            const json& s = section("PLAYBACK_SETTINGS");
            const std::string w = "PLAYBACK_SETTINGS.";
            std::string output = playback.output == OutputMode::MidiConnect ? "MIDICONNECT" : "KEYS";
            std::string sustain = sustainName(playback.sustain);
            pick(s, "OUTPUT", output, warn, w);
            pick(s, "88_KEYS", playback.keys88, warn, w);
            pick(s, "VELOCITY_KEYS", playback.velocityKeys, warn, w);
            pick(s, "VELOCITY_CURVE", playback.velocityCurveIndex, warn, w);
            pick(s, "OUT_OF_RANGE_TRANSPOSE", playback.outOfRange, warn, w);
            pick(s, "SUSTAIN_MODE", sustain, warn, w);
            pick(s, "SUSTAIN_CUTOFF", playback.sustainCutoff, warn, w);
            pick(s, "SOSTENUTO", playback.sostenuto, warn, w);
            pick(s, "GAME_WINDOW_ONLY", playback.gameWindowOnly, warn, w);
            pick(s, "KEY_BATCHING_MS", playback.batchMs, warn, w);
            pick(s, "TYPED_KEYS", playback.typedKeys, warn, w);
            output = upper(output);
            sustain = upper(sustain);
            playback.output = output == "MIDICONNECT" ? OutputMode::MidiConnect : OutputMode::Keys;
            playback.sustain = sustain == "DOWN" ? SustainMode::Down : sustain == "UP" ? SustainMode::Up
                : sustain == "HOLD" ? SustainMode::Hold : sustain == "ALWAYS" ? SustainMode::Always : SustainMode::Ignore;
            clamp_to(playback.sustainCutoff, 0, 127, "SUSTAIN_CUTOFF", warn, w);
            clamp_to(playback.batchMs, -1, 50, "KEY_BATCHING_MS", warn, w);
            int curves = 5 + static_cast<int>(playback.customVelocityCurves.size());
            clamp_to(playback.velocityCurveIndex, 0, curves - 1, "VELOCITY_CURVE", warn, w);
            playback.velocityCurve = playback.velocityCurveIndex < 5
                ? static_cast<VelocityCurveType>(playback.velocityCurveIndex) : VelocityCurveType::Custom;
        }

        {
            const json& s = section("UI_SETTINGS");
            const std::string w = "UI_SETTINGS.";
            pick(s, "alwaysOnTop", ui.alwaysOnTop, warn, w);
            pick(s, "opacity", ui.opacity, warn, w);
            pick(s, "sortMode", ui.sortMode, warn, w);
            pick(s, "midiDevice", ui.midiDevice, warn, w);
            pick(s, "midiChannel", ui.midiChannel, warn, w);
            pick(s, "darkMode", ui.darkMode, warn, w);
            pick(s, "compact", ui.compact, warn, w);
            clamp_to(ui.opacity, 100, 255, "opacity", warn, w);
            clamp_to(ui.sortMode, 0, 3, "sortMode", warn, w);
            clamp_to(ui.midiChannel, -1, 15, "midiChannel", warn, w);
            clamp_to(ui.darkMode, -1, 1, "darkMode", warn, w);
        }

        if (auto it = j.find("PLAYLIST_FILES"); it != j.end() && it->is_array())
            for (const json& f : *it)
                if (f.is_string())
                    playlistFiles.push_back(f.get<std::string>());

        if (auto it = j.find("KEY_MAPPINGS"); it != j.end()) {
            if (!it->is_object()) {
                warn.push_back("KEY_MAPPINGS is not an object, using the default keys");
            }
            else {
                for (const auto& [mode, m] : it->items()) {
                    if (!m.is_object()) {
                        warn.push_back("KEY_MAPPINGS." + mode + " is not an object, skipped it");
                        continue;
                    }
                    std::map<std::string, std::string> keys;
                    for (const auto& [note, key] : m.items()) {
                        if (!key.is_string() || noteNameToMidi(note) < 0) {
                            warn.push_back("KEY_MAPPINGS." + mode + "." + note + " is not a note to key mapping, skipped it");
                            continue;
                        }
                        std::string k = key.get<std::string>();
                        if (!input::parse_key(k))
                            warn.push_back("KEY_MAPPINGS." + mode + "." + note + " uses '" + k + "', which is not a key");
                        keys[note] = std::move(k);
                    }
                    if (keys.empty())
                        warn.push_back("KEY_MAPPINGS." + mode + " is empty, using the default keys");
                    else
                        key_mappings[mode] = std::move(keys);
                }
            }
        }

        return warn;
    }

    void Config::saveToFile(const std::filesystem::path& path) const {
        json j = raw_.is_object() ? raw_ : json::object();
        j.erase("AUTOPLAYER_TIMING_ACCURACY");

        j["VOLUME_SETTINGS"] = {
            {"MIN_VOLUME", volume.MIN_VOLUME},
            {"MAX_VOLUME", volume.MAX_VOLUME},
            {"INITIAL_VOLUME", volume.INITIAL_VOLUME},
            {"VOLUME_STEP", volume.VOLUME_STEP},
            {"ADJUSTMENT_INTERVAL_MS", volume.ADJUSTMENT_INTERVAL_MS}
        };
        j["AUTO_TRANSPOSE"] = {
            {"ENABLED", auto_transpose.ENABLED},
            {"TRANSPOSE_UP_KEY", auto_transpose.TRANSPOSE_UP_KEY},
            {"TRANSPOSE_DOWN_KEY", auto_transpose.TRANSPOSE_DOWN_KEY}
        };
        j["LEGIT_MODE_SETTINGS"] = {
            {"ENABLED", legit.ENABLED},
            {"TIMING_VARIATION", legit.TIMING_VARIATION},
            {"NOTE_SKIP_CHANCE", legit.NOTE_SKIP_CHANCE},
            {"EXTRA_DELAY_CHANCE", legit.EXTRA_DELAY_CHANCE},
            {"EXTRA_DELAY_MIN", legit.EXTRA_DELAY_MIN},
            {"EXTRA_DELAY_MAX", legit.EXTRA_DELAY_MAX}
        };
        j["MIDI_SETTINGS"] = {
            {"DETECT_DRUMS", midi.DETECT_DRUMS},
            {"KERNEL_STREAMING", midi.kernelStreaming},
            {"LIVE_WAIT_MODE", waitName(midi.liveWait)}
        };
        j["HOTKEY_SETTINGS"] = {
            {"SUSTAIN_KEY", hotkeys.SUSTAIN_KEY},
            {"VOLUME_UP_KEY", hotkeys.VOLUME_UP_KEY},
            {"VOLUME_DOWN_KEY", hotkeys.VOLUME_DOWN_KEY},
            {"PLAY_PAUSE_KEY", hotkeys.PLAY_PAUSE_KEY},
            {"REWIND_KEY", hotkeys.REWIND_KEY},
            {"SKIP_KEY", hotkeys.SKIP_KEY},
            {"EMERGENCY_EXIT_KEY", hotkeys.EMERGENCY_EXIT_KEY}
        };
        j["STACKED_NOTE_HANDLING_MODE"] = noteHandlingModeToString(playback.noteHandlingMode);
        j["CUSTOM_VELOCITY_CURVES"] = json::array();
        for (const auto& c : playback.customVelocityCurves)
            j["CUSTOM_VELOCITY_CURVES"].push_back({ {"name", c.name}, {"values", c.velocityValues} });
        j["PLAYBACK_SETTINGS"] = {
            {"OUTPUT", playback.output == OutputMode::MidiConnect ? "MIDICONNECT" : "KEYS"},
            {"88_KEYS", playback.keys88},
            {"VELOCITY_KEYS", playback.velocityKeys},
            {"VELOCITY_CURVE", playback.velocityCurveIndex},
            {"OUT_OF_RANGE_TRANSPOSE", playback.outOfRange},
            {"SUSTAIN_MODE", sustainName(playback.sustain)},
            {"SUSTAIN_CUTOFF", playback.sustainCutoff},
            {"SOSTENUTO", playback.sostenuto},
            {"GAME_WINDOW_ONLY", playback.gameWindowOnly},
            {"KEY_BATCHING_MS", playback.batchMs},
            {"TYPED_KEYS", playback.typedKeys}
        };
        j["UI_SETTINGS"] = {
            {"alwaysOnTop", ui.alwaysOnTop},
            {"opacity", ui.opacity},
            {"sortMode", ui.sortMode},
            {"midiDevice", ui.midiDevice},
            {"midiChannel", ui.midiChannel},
            {"darkMode", ui.darkMode},
            {"compact", ui.compact}
        };
        j["PLAYLIST_FILES"] = playlistFiles;
        j["KEY_MAPPINGS"] = key_mappings;

        std::string data = j.dump(4);
        std::filesystem::path tmp = path;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f || !f.write(data.data(), static_cast<std::streamsize>(data.size())))
                throw ConfigException("could not write config.json");
        }
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            throw ConfigException("could not replace config.json");
        }
    }

}
