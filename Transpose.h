#pragma once
#include <string>
#include <vector>
#include <array>
#include <set>
#include <map>
#include <memory>
#include "midi_parser.h"

class TransposeEngine {
public:
    // Core functionality
    [[nodiscard]] std::string estimateKey(const std::vector<int>& notes,
        const std::vector<double>& durations) const;
    [[nodiscard]] std::string detectGenre(const MidiFile& midiFile) const;
    [[nodiscard]] int findBestTranspose(const std::vector<int>& notes,
        const std::vector<double>& durations,
        const std::string& detectedKey,
        const std::string& genre) const;

    // MIDI data extraction
    [[nodiscard]] std::pair<std::vector<int>, std::vector<double>>
        extractNotesAndDurations(const MidiFile& midiFile) const;

private:
    // Constants
    static constexpr std::array<const char*, 12> NOTE_NAMES = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    // Key complexity lookup (number of accidentals)
    static constexpr std::array<int, 12> KEY_COMPLEXITY = {
        0, 5, 2, 7, 4, 1, 6, 3, 8, 5, 2, 7  // C, C#, D, etc.
    };

    // Common key sets for different genres
    const std::vector<int> commonClassicalKeys = { 0, 2, 4, 5, 7, 9, 11 };  // C, D, E, F, G, A, B
    const std::vector<int> commonJazzKeys = { 0, 5, 7, 10, 3 };            // C, F, G, Bb, Eb
    const std::vector<int> commonPopKeys = { 0, 5, 7, 2, 9 };              // C, F, G, D, A

    // Utility functions
    [[nodiscard]] static constexpr int getPitchClass(int midiNote) noexcept {
        return midiNote % 12;
    }

    // Analysis functions
    [[nodiscard]] double calculateRhythmComplexity(const std::vector<double>& durations) const;
    [[nodiscard]] int calculateInstrumentDiversity(const MidiFile& midiFile) const;
    [[nodiscard]] double calculateNoteDistributionEntropy(const std::vector<int>& notes,
        int transpose) const;
    [[nodiscard]] double calculateIntervalComplexity(const std::vector<int>& notes,
        int transpose) const;
    [[nodiscard]] double calculatePlayabilityScore(int newKeyIndex,
        int transpose) const;
    [[nodiscard]] double calculateGenreSpecificTransposeScore(int newKeyIndex,
        int keySignatureComplexity,
        const std::string& genre,
        int transpose) const;

    // Key estimation helpers
    void adjustKeyEstimate(const std::vector<double>& pitchDistribution,
        int& bestTonic,
        std::string& bestMode) const;
    [[nodiscard]] std::vector<double> computePitchClassDistribution(
        const std::vector<int>& notes,
        const std::vector<double>& durations) const;

    // Scoring struct for transpose evaluation
    struct TransposeScore {
        double playability = 0.0;
        double entropy = 0.0;
        double intervals = 0.0;
        double genre = 0.0;
        double total = 0.0;
    };

    [[nodiscard]] TransposeScore evaluateTranspose(const std::vector<int>& notes,
        int transpose,
        const std::string& genre) const;
};