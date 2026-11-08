#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Song;

struct ShiftScore {
    int noteShift = 0;
    double cost = 0.0;
    int outOfRange = 0;
    int needsModifier = 0;
    int sameKeyCollisions = 0;
    int mixedChords = 0;
};

struct KeySpan {
    int64_t start = 0;
    int64_t end = 0;
    int key = -1;
    std::string name;
};

struct TransposePlan {
    int semitones = 0;
    int noteShift = 0;
    std::string key = "Unknown";
    std::string playedKey = "Unknown";
    int keyIndex = -1;
    double keyConfidence = 0.0;
    int outOfRange = 0;
    int needsModifier = 0;
    int sameKeyCollisions = 0;
    int total = 0;
    ShiftScore unshifted;
    std::vector<ShiftScore> shifts;
    std::vector<KeySpan> keys;
};

struct TransposeOptions {
    std::string layout = "LIMITED";
    std::vector<bool> tracks;
    bool fold = false;
    bool keepPitch = true;
    int key = 0;
    int range = 24;
    int batchMs = -1;
};

class TransposeEngine {
public:
    // Core functionality
    [[nodiscard]] std::string estimateKey(const std::vector<int>& notes,
        const std::vector<double>& durations) const;
    [[nodiscard]] std::string detectGenre(const std::vector<int>& notes,
        const std::vector<double>& durations, double tempo, int timeSignatureNum) const;
    [[nodiscard]] int findBestTranspose(const std::vector<int>& notes,
        const std::vector<double>& durations,
        const std::string& detectedKey,
        const std::string& genre) const;
    [[nodiscard]] TransposePlan plan(const Song& song, const TransposeOptions& options = {}) const;
    [[nodiscard]] TransposePlan plan(const Song& song, const std::map<std::string, std::string>& keys,
        const TransposeOptions& options = {}) const;

private:
    [[nodiscard]] double calculateRhythmComplexity(const std::vector<double>& durations) const;
};
