#include "Transpose.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>
#include <iostream>
#include <iomanip>

// Key detection profiles based on Krumhansl-Schmuckler research
const std::array<double, 12> MAJOR_PROFILE = {
    0.748, 0.060, 0.488, 0.082, 0.670, 0.460,
    0.096, 0.715, 0.104, 0.366, 0.057, 0.400
};

const std::array<double, 12> MINOR_PROFILE = {
    0.712, 0.084, 0.474, 0.618, 0.049, 0.460,
    0.105, 0.747, 0.404, 0.067, 0.133, 0.330
};

// Genre-specific weights for transpose analysis
const std::unordered_map<std::string, double> GENRE_WEIGHTS = {
    {"Classical Piano", 1.2},
    {"Jazz Piano", 1.1},
    {"Pop Piano", 0.9},
    {"Rock Piano", 0.8},
    {"Blues Piano", 1.0},
    {"Romantic Piano", 1.3},
    {"Contemporary Piano", 0.7}
};

std::string TransposeEngine::estimateKey(const std::vector<int>& notes,
    const std::vector<double>& durations) const {
    if (notes.empty() || durations.empty()) {
        return "Unknown";
    }

    auto pitchDistribution = computePitchClassDistribution(notes, durations);
    int bestTonic = 0;
    std::string bestMode = "Major";
    double maxCorrelation = -1.0;

    // Apply weightings for important scale degrees
    const double thirdWeight = 1.4;
    const double fifthWeight = 1.2;
    const double seventhWeight = 1.1;

    // Create weighted profiles
    auto weightedMajor = MAJOR_PROFILE;
    auto weightedMinor = MINOR_PROFILE;
    for (int i = 0; i < 12; ++i) {
        // Weight the thirds, fifths, and sevenths
        weightedMajor[(i + 4) % 12] *= thirdWeight;  // Major third
        weightedMajor[(i + 7) % 12] *= fifthWeight;  // Perfect fifth
        weightedMajor[(i + 11) % 12] *= seventhWeight; // Major seventh

        weightedMinor[(i + 3) % 12] *= thirdWeight;  // Minor third
        weightedMinor[(i + 7) % 12] *= fifthWeight;  // Perfect fifth
        weightedMinor[(i + 10) % 12] *= seventhWeight; // Minor seventh
    }

    // Calculate correlations for each possible key
    for (int i = 0; i < 12; ++i) {
        double majorCorr = 0.0;
        double minorCorr = 0.0;

        for (int j = 0; j < 12; ++j) {
            int idx = (j + i) % 12;
            majorCorr += pitchDistribution[j] * weightedMajor[idx];
            minorCorr += pitchDistribution[j] * weightedMinor[idx];
        }

        // Apply slight bias towards major keys (1.02x)
        majorCorr *= 1.02;

        if (majorCorr > maxCorrelation) {
            maxCorrelation = majorCorr;
            bestTonic = i;
            bestMode = "Major";
        }
        if (minorCorr > maxCorrelation) {
            maxCorrelation = minorCorr;
            bestTonic = i;
            bestMode = "Minor";
        }
    }

    adjustKeyEstimate(pitchDistribution, bestTonic, bestMode);
    return std::string(NOTE_NAMES[bestTonic]) + " " + bestMode;
}

std::string TransposeEngine::detectGenre(const MidiFile& midiFile) const {
    auto [notes, durations] = extractNotesAndDurations(midiFile);
    if (notes.empty()) return "Unknown";

    // Extract musical features
    double tempo = midiFile.tempoChanges.empty() ? 120.0 :
        60000000.0 / midiFile.tempoChanges[0].microsecondsPerQuarter;
    int timeSignatureNum = midiFile.timeSignatures.empty() ? 4 :
        midiFile.timeSignatures[0].numerator;
    double totalDuration = std::accumulate(durations.begin(), durations.end(), 0.0);

    // Calculate musical metrics
    double noteDensity = static_cast<double>(notes.size()) / totalDuration;
    double rhythmComplexity = calculateRhythmComplexity(durations);
    int pitchRange = notes.empty() ? 0 :
        *std::max_element(notes.begin(), notes.end()) -
        *std::min_element(notes.begin(), notes.end());

    // Calculate syncopation
    double syncopation = 0.0;
    for (const auto& duration : durations) {
        double beatPosition = std::fmod(duration, 1.0);
        if (beatPosition > 0.25 && beatPosition < 0.75) {
            syncopation += 1.0;
        }
    }
    syncopation /= notes.size();

    // Genre classification based on musical features
    if (tempo >= 60 && tempo <= 80 && timeSignatureNum == 4 && pitchRange >= 48) {
        double complexity = calculateIntervalComplexity(notes, 0);
        if (complexity > 0.8 && rhythmComplexity > 1.3) return "Romantic Piano";
        if (complexity > 0.7) return "Classical Piano";
        return "Baroque Piano";
    }

    if (tempo >= 100 && tempo <= 160 && noteDensity > 4 && rhythmComplexity > 1.2) {
        if (tempo >= 140 && syncopation > 0.5) return "Bebop Piano";
        if (rhythmComplexity > 1.4) return "Jazz Piano";
        return "Cool Jazz Piano";
    }

    if (tempo >= 120 && tempo <= 140 && timeSignatureNum == 4 && noteDensity <= 3) {
        return "Pop Piano";
    }

    if (tempo >= 140 && noteDensity > 5 && rhythmComplexity > 1.5 && pitchRange >= 36) {
        return "Rock Piano";
    }

    if (tempo >= 70 && tempo <= 130 && syncopation > 0.4) {
        if (tempo < 100) return "Blues Piano";
        return "Boogie-Woogie Piano";
    }

    return "Contemporary Piano";
}

int TransposeEngine::findBestTranspose(const std::vector<int>& notes,
    const std::vector<double>& durations,
    const std::string& detectedKey,
    const std::string& genre) const {
    if (notes.empty()) return 0;

    const std::vector<int> transposeOptions = {
        -12, -11, -9, -7, -5, -4, -2, 0, 2, 4, 5, 7, 9, 11, 12
    };

    int bestTranspose = 0;
    double bestScore = -std::numeric_limits<double>::infinity();

    for (int transpose : transposeOptions) {
        auto score = evaluateTranspose(notes, transpose, genre);
        if (score.total > bestScore) {
            bestScore = score.total;
            bestTranspose = transpose;
        }
    }

    return bestTranspose;
}

std::pair<std::vector<int>, std::vector<double>> TransposeEngine::extractNotesAndDurations(
    const MidiFile& midiFile) const {
    std::vector<int> notes;
    std::vector<double> durations;

    for (const auto& track : midiFile.tracks) {
        std::map<int, double> activeNotes;
        double currentTime = 0.0;
        double tempo = 500000.0;  // Default tempo (120 BPM)
        double ticksPerQuarterNote = midiFile.division;
        double lastTick = 0.0;

        for (const auto& event : track.events) {
            // Handle tempo changes
            if (event.status == 0xFF && event.data1 == 0x51 && event.metaData.size() == 3) {
                tempo = static_cast<double>((event.metaData[0] << 16) |
                    (event.metaData[1] << 8) |
                    event.metaData[2]);
            }

            // Calculate time
            double deltaTime = static_cast<double>(event.absoluteTick - lastTick);
            currentTime += deltaTime * (tempo / 1000000.0) / ticksPerQuarterNote;
            lastTick = event.absoluteTick;

            // Handle note events
            if ((event.status & 0xF0) == 0x90 && event.data2 > 0) {
                activeNotes[event.data1] = currentTime;
            }
            else if (((event.status & 0xF0) == 0x80) ||
                ((event.status & 0xF0) == 0x90 && event.data2 == 0)) {
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

std::vector<double> TransposeEngine::computePitchClassDistribution(
    const std::vector<int>& notes,
    const std::vector<double>& durations) const {

    std::vector<double> distribution(12, 0.0);
    double totalDuration = std::accumulate(durations.begin(), durations.end(), 0.0);

    for (size_t i = 0; i < notes.size(); ++i) {
        int pitchClass = getPitchClass(notes[i]);
        distribution[pitchClass] += durations[i];
    }

    // Normalize
    for (double& value : distribution) {
        value /= totalDuration;
    }

    return distribution;
}

void TransposeEngine::adjustKeyEstimate(
    const std::vector<double>& pitchDistribution,
    int& bestTonic,
    std::string& bestMode) const {

    // Handle special cases for ambiguous keys
    if (bestTonic == 7) {  // G
        double fNatural = pitchDistribution[5];
        double fSharp = pitchDistribution[6];
        if (fSharp > fNatural * 1.2) {
            bestMode = "Major";
        }
        else if (fNatural > fSharp * 1.2) {
            bestMode = "Minor";
        }
    }
    else if (bestTonic == 9) {  // A
        double cNatural = pitchDistribution[0];
        double cSharp = pitchDistribution[1];
        double gNatural = pitchDistribution[7];
        double gSharp = pitchDistribution[8];

        if (cNatural > cSharp * 1.1 && gNatural > gSharp * 1.1) {
            bestMode = "Minor";
        }
        else if (cSharp > cNatural * 1.1 && gSharp > gNatural * 1.1) {
            bestMode = "Major";
        }
    }
    else if (bestTonic == 11) {  // B
        // Check for dominant function in E
        double ePresence = pitchDistribution[4];
        double bPresence = pitchDistribution[11];
        if (ePresence > bPresence * 1.3) {
            bestTonic = 4;  // Change to E
        }
    }
}

TransposeEngine::TransposeScore TransposeEngine::evaluateTranspose(
    const std::vector<int>& notes,
    int transpose,
    const std::string& genre) const {

    TransposeScore score;

    // Basic range check
    int minNote = *std::min_element(notes.begin(), notes.end()) + transpose;
    int maxNote = *std::max_element(notes.begin(), notes.end()) + transpose;
    if (minNote < 21 || maxNote > 108) {
        score.total = -2000.0;  // Invalid range
        return score;
    }

    // Calculate ideal center for piano range
    double idealCenter = 60.0;
    double actualCenter = (minNote + maxNote) / 2.0;
    double centerScore = -std::abs(idealCenter - actualCenter) * 2.0;

    // Calculate component scores
    score.playability = calculatePlayabilityScore(getPitchClass(minNote), transpose) + centerScore;
    score.entropy = calculateNoteDistributionEntropy(notes, transpose) * 6.0;
    score.intervals = calculateIntervalComplexity(notes, transpose) * 8.0;
    score.genre = calculateGenreSpecificTransposeScore(
        getPitchClass(minNote),
        KEY_COMPLEXITY[getPitchClass(minNote)],
        genre,
        transpose);

    // Combine scores with weights
    score.total = score.playability + score.entropy + score.intervals + score.genre;

    // Bonus for positive transpose (generally better for piano playing)
    if (transpose > 0) {
        score.total += 5.0;
    }

    return score;
}
double TransposeEngine::calculateRhythmComplexity(
    const std::vector<double>& durations) const {

    if (durations.size() <= 1) return 1.0;

    std::vector<double> intervalRatios;
    intervalRatios.reserve(durations.size() - 1);

    // Calculate ratios between consecutive durations
    for (size_t i = 1; i < durations.size(); ++i) {
        if (durations[i - 1] > 0) {
            intervalRatios.push_back(durations[i] / durations[i - 1]);
        }
    }

    if (intervalRatios.empty()) return 1.0;
    // Calculate mean and standard deviation
    double sum = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0);
    double mean = sum / intervalRatios.size();

    double variance = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0,
        [mean](double acc, double ratio) {
            double diff = ratio - mean;
            return acc + diff * diff;
        }) / intervalRatios.size();

    // Weight shorter durations more heavily as they tend to be more important rhythmically
    double complexityMultiplier = 1.0;
    for (const auto& ratio : intervalRatios) {
        if (ratio < 0.5) {  // For shorter notes
            complexityMultiplier *= 1.2;
        }
    }

    // Calculate syncopation factor
    double syncopation = 0.0;
    for (const auto& ratio : intervalRatios) {
        if (std::fmod(ratio, 1.0) > 0.25 && std::fmod(ratio, 1.0) < 0.75) {
            syncopation += 0.2;  // Add complexity for off-beat rhythms
        }
    }

    // Combine standard deviation with syncopation and apply complexity multiplier
    double complexity = (std::sqrt(variance) + syncopation) * complexityMultiplier;

    // Cap the maximum complexity to avoid extreme values
    return std::min(complexity, 10.0);
}

double TransposeEngine::calculateNoteDistributionEntropy(
    const std::vector<int>& notes,
    int transpose) const {

    std::vector<int> distribution(12, 0);
    int totalNotes = static_cast<int>(notes.size());

    // Build pitch class distribution
    for (int note : notes) {
        int pitchClass = (note + transpose) % 12;
        if (pitchClass < 0) pitchClass += 12;  // Handle negative pitch classes
        distribution[pitchClass]++;
    }

    double entropy = 0.0;
    double localEntropy = 0.0;
    int windowSize = 4;  // For local entropy calculation

    // Calculate Shannon entropy
    for (int count : distribution) {
        if (count > 0) {
            double probability = static_cast<double>(count) / totalNotes;
            entropy -= probability * std::log2(probability);
        }
    }

    // Calculate local entropy (for neighboring pitch classes)
    for (int i = 0; i < 12; i++) {
        double localSum = 0;
        for (int j = 0; j < windowSize; j++) {
            int idx = (i + j) % 12;
            if (distribution[idx] > 0) {
                double localProb = static_cast<double>(distribution[idx]) / totalNotes;
                localEntropy -= localProb * std::log2(localProb);
            }
        }
    }
    localEntropy /= (12 - windowSize + 1);  // Normalize local entropy

    // Combine global and local entropy with weighting
    double combinedEntropy = (entropy * 0.7 + localEntropy * 0.3);

    // Normalize to 0-1 range and scale
    return (combinedEntropy / std::log2(12)) * 5.0;
}

double TransposeEngine::calculateIntervalComplexity(
    const std::vector<int>& notes,
    int transpose) const {

    if (notes.size() < 2) return 0.0;

    std::vector<int> intervals;
    intervals.reserve(notes.size() - 1);
    std::map<int, int> intervalHistogram;

    // Calculate intervals and build histogram
    for (size_t i = 1; i < notes.size(); ++i) {
        int interval = std::abs((notes[i] + transpose) - (notes[i - 1] + transpose));
        intervals.push_back(interval);
        intervalHistogram[interval % 12]++;
    }

    double complexity = 0.0;
    std::set<int> uniqueIntervals;

    // Analyze melodic contour
    int directionChanges = 0;
    for (size_t i = 2; i < notes.size(); ++i) {
        int prev_direction = notes[i - 1] - notes[i - 2];
        int curr_direction = notes[i] - notes[i - 1];
        if ((prev_direction > 0 && curr_direction < 0) ||
            (prev_direction < 0 && curr_direction > 0)) {
            directionChanges++;
        }
    }
    double contourComplexity = static_cast<double>(directionChanges) / (notes.size() - 2);

    // Score each interval based on musical complexity and frequency
    for (int interval : intervals) {
        interval %= 12;  // Normalize to octave
        uniqueIntervals.insert(interval);

        // Base interval complexity
        double intervalScore = 0.0;
        switch (interval) {
        case 0:  // Unison
            intervalScore = 0.1;
            break;
        case 1: case 2:  // Minor/Major 2nd
            intervalScore = 0.5;
            break;
        case 3: case 4:  // Minor/Major 3rd
            intervalScore = 0.7;
            break;
        case 5:  // Perfect 4th
            intervalScore = 0.8;
            break;
        case 7:  // Perfect 5th
            intervalScore = 0.6;
            break;
        case 6:  // Tritone
            intervalScore = 1.0;
            break;
        case 8: case 9:  // Minor/Major 6th
            intervalScore = 0.9;
            break;
        case 10: case 11:  // Minor/Major 7th
            intervalScore = 1.1;
            break;
        default:  // Larger intervals
            intervalScore = std::min(1.5, interval * 0.1);
        }

        // Adjust score based on interval frequency
        int frequency = intervalHistogram[interval];
        double frequencyWeight = 1.0 + (1.0 / frequency);  // Rarer intervals contribute more to complexity
        complexity += intervalScore * frequencyWeight;
    }

    // Factor in interval variety and melodic contour
    double varietyFactor = static_cast<double>(uniqueIntervals.size()) / 12.0;
    complexity *= (1.0 + varietyFactor);
    complexity *= (1.0 + contourComplexity);

    // Calculate chromatic density
    int chromaticCount = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
        if (std::abs(notes[i] - notes[i - 1]) == 1) {
            chromaticCount++;
        }
    }
    double chromaticDensity = static_cast<double>(chromaticCount) / (notes.size() - 1);
    complexity *= (1.0 + chromaticDensity);

    // Normalize and return
    return complexity / (notes.size() * 2.0);  // Divide by 2.0 to keep the final value in a reasonable range
}
double TransposeEngine::calculatePlayabilityScore(
    int newKeyIndex,
    int transpose) const {

    double score = 0.0;

    // Base key preferences
    const std::vector<int> easyKeys = { 0, 7, 5, 2, 9, 4 };  // C, G, F, D, A, E
    const std::vector<int> mediumKeys = { 11, 1, 3, 8, 10 }; // B, C#, D#, G#, A#
    const std::vector<int> hardKeys = { 6 };                 // F#

    // Score based on key difficulty
    if (std::find(easyKeys.begin(), easyKeys.end(), newKeyIndex) != easyKeys.end()) {
        score += 15.0;
    }
    else if (std::find(mediumKeys.begin(), mediumKeys.end(), newKeyIndex) != mediumKeys.end()) {
        score += 7.5;
    }
    else if (std::find(hardKeys.begin(), hardKeys.end(), newKeyIndex) != hardKeys.end()) {
        score += 3.0;
    }

    // Hand position comfort scoring
    const std::vector<int> whiteKeys = { 0, 2, 4, 5, 7, 9, 11 }; // C, D, E, F, G, A, B
    if (std::find(whiteKeys.begin(), whiteKeys.end(), newKeyIndex) != whiteKeys.end()) {
        score += 5.0; // Prefer white keys for hand position comfort
    }

    // Transposition preferences
    const std::vector<int> commonTranspositions = { -5, -4, -2, 0, 2, 4, 5 };
    if (std::find(commonTranspositions.begin(), commonTranspositions.end(), transpose)
        != commonTranspositions.end()) {
        score += 10.0;
    }

    // Penalize extreme transpositions with progressive penalty
    if (std::abs(transpose) > 7) {
        double penalty = std::pow(1.5, std::abs(transpose) - 7);
        score -= penalty;
    }

    // Consider hand stretch requirements
    int accidentals = KEY_COMPLEXITY[newKeyIndex];
    double stretchPenalty = accidentals * 1.5;
    score -= stretchPenalty;

    // Bonus for keeping within comfortable hand position range
    if (transpose >= -5 && transpose <= 7) {
        score += 8.0;
    }

    // Penalty for awkward black key combinations
    const std::vector<int> awkwardCombos = { 1, 6, 8 }; // C#, F#, G#
    if (std::find(awkwardCombos.begin(), awkwardCombos.end(), newKeyIndex) != awkwardCombos.end()) {
        score -= 5.0;
    }

    return score;
}

double TransposeEngine::calculateGenreSpecificTransposeScore(
    int newKeyIndex,
    int keySignatureComplexity,
    const std::string& genre,
    int transpose) const {

    double score = 0.0;
    double genreWeight = GENRE_WEIGHTS.count(genre) ? GENRE_WEIGHTS.at(genre) : 1.0;

    // Base key signature complexity scoring
    double complexityScore = (7.0 - keySignatureComplexity) * 1.5 * genreWeight;
    score += complexityScore;

    // Genre-specific scoring logic
    if (genre.find("Classical") != std::string::npos ||
        genre.find("Baroque") != std::string::npos) {
        // Classical/Baroque preferences
        if (std::find(commonClassicalKeys.begin(), commonClassicalKeys.end(),
            newKeyIndex) != commonClassicalKeys.end()) {
            score += 10.0;
        }
        // Prefer traditional key relationships
        score -= std::abs(transpose) * 1.2;

    }
    else if (genre.find("Jazz") != std::string::npos) {
        // Jazz preferences
        if (std::find(commonJazzKeys.begin(), commonJazzKeys.end(),
            newKeyIndex) != commonJazzKeys.end()) {
            score += 8.0;
        }
        // Jazz often uses complex keys
        score += keySignatureComplexity * 0.5;
        // Jazz is more tolerant of transposition
        score -= std::abs(transpose) * 0.8;

    }
    else if (genre.find("Pop") != std::string::npos ||
        genre.find("Rock") != std::string::npos) {
        // Pop/Rock preferences
        if (std::find(commonPopKeys.begin(), commonPopKeys.end(),
            newKeyIndex) != commonPopKeys.end()) {
            score += 6.0;
        }
        // Pop/Rock prefer simpler keys
        score -= keySignatureComplexity * 1.2;
        // Moderate tolerance for transposition
        score -= std::abs(transpose) * 1.0;

    }
    else if (genre.find("Blues") != std::string::npos) {
        // Blues preferences
        const std::vector<int> bluesKeys = { 0, 5, 7, 10 }; // C, F, G, Bb
        if (std::find(bluesKeys.begin(), bluesKeys.end(), newKeyIndex) != bluesKeys.end()) {
            score += 12.0;
        }
        // Blues often works well in specific keys
        score -= std::abs(transpose) * 1.5;

    }
    else if (genre.find("Contemporary") != std::string::npos) {
        // Contemporary preferences - more flexible
        score += 5.0; // Base score
        // More tolerant of unusual keys and transpositions
        score -= std::abs(transpose) * 0.5;
    }

    // Universal adjustments
    if (transpose > 0) {
        score += 3.0;  // Slight preference for upward transposition
    }

    if (std::abs(transpose) <= 5) {
        score += 5.0;  // Preference for smaller transpositions
    }

    // Consider relationship to original key
    int distanceFromC = std::min((newKeyIndex - 0 + 12) % 12,
        (0 - newKeyIndex + 12) % 12);
    score -= distanceFromC * 0.5;

    // Harmonic considerations
    const std::vector<int> dominantKeys = { 7, 2, 9 }; // Perfect 5th relationships
    if (std::find(dominantKeys.begin(), dominantKeys.end(), newKeyIndex) != dominantKeys.end()) {
        score += 4.0;
    }

    // Register-specific adjustments
    if (transpose > 12) {
        score -= (transpose - 12) * 2.0; // Heavy penalty for very high transpositions
    }
    else if (transpose < -12) {
        score -= (std::abs(transpose) - 12) * 2.0; // Heavy penalty for very low transpositions
    }

    return score;
}
int TransposeEngine::calculateInstrumentDiversity(const MidiFile& midiFile) const {
    std::set<uint8_t> uniqueInstruments;
    std::map<uint8_t, int> instrumentCounts;

    for (const auto& track : midiFile.tracks) {
        std::set<uint8_t> trackInstruments;

        for (const auto& event : track.events) {
            if ((event.status & 0xF0) == 0xC0) {  // Program Change events
                uint8_t instrument = event.data1;
                uniqueInstruments.insert(instrument);
                trackInstruments.insert(instrument);
                instrumentCounts[instrument]++;
            }
        }

        // Weight instruments based on their role in each track
        for (uint8_t instrument : trackInstruments) {
            if (instrument >= 0 && instrument <= 7) {  // Piano family
                instrumentCounts[instrument] *= 2;
            }
            else if (instrument >= 24 && instrument <= 31) {  // Guitar family
                instrumentCounts[instrument] *= 1.5;
            }
        }
    }

    // Calculate weighted diversity score
    double diversityScore = 0.0;
    int totalInstruments = 0;

    for (const auto& [instrument, count] : instrumentCounts) {
        totalInstruments += count;
        diversityScore += count * std::log2(count + 1);
    }

    return static_cast<int>(diversityScore / std::log2(totalInstruments + 1));
}

