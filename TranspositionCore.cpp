#include "Transpose.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <vector>

// =============================================================================
// Music theory sucks
// =============================================================================

// Compute Pearson correlation coefficient between two vectors.
static double computePearsonCorrelation(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.empty())
        return 0.0;
    double meanX = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
    double meanY = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
    double numerator = 0.0, denomX = 0.0, denomY = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double diffX = x[i] - meanX;
        double diffY = y[i] - meanY;
        numerator += diffX * diffY;
        denomX += diffX * diffX;
        denomY += diffY * diffY;
    }
    if (denomX == 0 || denomY == 0)
        return 0.0;
    return numerator / std::sqrt(denomX * denomY);
}

// Gaussian smoothing on a 12-element vector.
static std::vector<double> gaussianSmooth(const std::vector<double>& data, double sigma) {
    std::vector<double> smoothed(data.size(), 0.0);
    int radius = static_cast<int>(std::ceil(3 * sigma));
    double sumWeights = 0.0;
    std::vector<double> kernel(2 * radius + 1, 0.0);
    for (int i = -radius; i <= radius; ++i) {
        kernel[i + radius] = std::exp(-(i * i) / (2 * sigma * sigma));
        sumWeights += kernel[i + radius];
    }
    for (double& w : kernel)
        w /= sumWeights;
    for (size_t i = 0; i < data.size(); ++i) {
        double accum = 0.0;
        for (int j = -radius; j <= radius; ++j) {
            int idx = (i + j + data.size()) % data.size();
            accum += data[idx] * kernel[j + radius];
        }
        smoothed[i] = accum;
    }
    return smoothed;
}

// Enhanced spectral analysis: Compute pitch-class distribution with harmonic partials.
static std::vector<double> computeEnhancedPitchClassDistribution(const std::vector<int>& notes,
    const std::vector<double>& durations,
    int numHarmonics = 5) {
    std::vector<double> distribution(12, 0.0);
    double totalWeight = 0.0;
    for (size_t i = 0; i < notes.size(); ++i) {
        int note = notes[i];
        double duration = durations[i];
        int pc = note % 12;
        distribution[pc] += duration;
        totalWeight += duration;
        for (int n = 2; n <= numHarmonics; ++n) {
            double harmonicPitch = note + 12 * std::log2(n);
            int harmonicPC = static_cast<int>(std::round(harmonicPitch)) % 12;
            double weight = duration / n;
            distribution[harmonicPC] += weight;
            totalWeight += weight;
        }
    }
    if (totalWeight > 0) {
        for (double& val : distribution)
            val /= totalWeight;
    }
    return distribution;
}

// Compute a spectral centroid from a 12-element distribution using reference frequencies.
static double computeSpectralCentroid(const std::vector<double>& distribution) {
    const std::array<double, 12> refFreq = { 261.63, 277.18, 293.66, 311.13, 329.63, 349.23,
                                            369.99, 392.00, 415.30, 440.00, 466.16, 493.88 };
    double centroid = 0.0;
    for (int i = 0; i < 12; ++i) {
        centroid += distribution[i] * refFreq[i];
    }
    return centroid;
}

// =============================================================================
// Dynamic Segmentation for Key Detection
// =============================================================================
static std::pair<std::vector<int>, std::vector<double>> extractSegment(
    const std::vector<int>& notes, const std::vector<double>& durations,
    size_t start, size_t end) {
    std::vector<int> segNotes(notes.begin() + start, notes.begin() + end);
    std::vector<double> segDurations(durations.begin() + start, durations.begin() + end);
    return { segNotes, segDurations };
}

// =============================================================================
// Interval Profile Computation (with file equalization)
// =============================================================================
static std::vector<std::vector<double>> computeIntervalProfile(const std::vector<int>& notes,
    int transpose) {
    std::vector<std::vector<double>> profile(12, std::vector<double>(12, 0.0));
    for (size_t i = 1; i < notes.size(); ++i) {
        int n1 = (notes[i - 1] + transpose) % 12;
        int n2 = (notes[i] + transpose) % 12;
        if (n1 < 0) n1 += 12;
        if (n2 < 0) n2 += 12;
        profile[n1][n2] += 1.0;
    }
    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            profile[i][j] = std::sqrt(profile[i][j]);
        }
    }
    double sum = 0.0;
    for (const auto& row : profile)
        for (double val : row)
            sum += val;
    if (sum > 0) {
        for (auto& row : profile)
            for (double& val : row)
                val /= sum;
    }
    return profile;
}

static std::vector<std::vector<double>> idealIntervalProfile(int keyCandidate, const std::string& mode) {
    std::set<int> diatonic;
    if (mode == "Major") {
        diatonic = { 0, 2, 4, 5, 7, 9, 11 };
    }
    else { 
        diatonic = { 0, 2, 3, 5, 7, 8, 10 };
    }
    std::vector<std::vector<double>> templateMatrix(12, std::vector<double>(12, 0.0));
    for (int i = 0; i < 12; ++i) {
        int note1 = (i - keyCandidate + 12) % 12;
        for (int j = 0; j < 12; ++j) {
            int note2 = (j - keyCandidate + 12) % 12;
            if (diatonic.count(note1) && diatonic.count(note2))
                templateMatrix[i][j] = 1.0;
        }
    }
    double total = 0.0;
    for (const auto& row : templateMatrix)
        for (double v : row)
            total += v;
    if (total > 0) {
        for (auto& row : templateMatrix)
            for (double& v : row)
                v /= total;
    }
    return templateMatrix;
}

static double matrixInnerProduct(const std::vector<std::vector<double>>& A,
    const std::vector<std::vector<double>>& B) {
    double sum = 0.0;
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j)
            sum += A[i][j] * B[i][j];
    return sum;
}

// =============================================================================
// Key Profiles (from Krumhansl–Kessler, verified)
// =============================================================================
const std::array<double, 12> MAJOR_PROFILE = {
    0.748, 0.060, 0.488, 0.082, 0.670, 0.460,
    0.096, 0.715, 0.104, 0.366, 0.057, 0.400
};
const std::array<double, 12> MINOR_PROFILE = {
    0.712, 0.084, 0.474, 0.618, 0.049, 0.460,
    0.105, 0.747, 0.404, 0.067, 0.133, 0.330
};

// =============================================================================
// Advanced Key Estimation Combining Pitch-Class and Interval Profiles
// =============================================================================
std::string TransposeEngine::estimateKey(const std::vector<int>& notes,
    const std::vector<double>& durations) const {
    if (notes.empty() || durations.empty())
        return "Unknown";

    // Use dynamic segmentation for long pieces.
    const size_t segmentSize = 100;
    size_t numSegments = (notes.size() + segmentSize - 1) / segmentSize;
    std::unordered_map<std::string, double> voteMap;

    for (size_t seg = 0; seg < numSegments; ++seg) {
        size_t start = seg * segmentSize;
        size_t end = std::min(start + segmentSize, notes.size());
        auto [segNotes, segDurations] = extractSegment(notes, durations, start, end);
        // Compute enhanced pitch-class distribution and smooth it.
        auto enhancedDistribution = computeEnhancedPitchClassDistribution(segNotes, segDurations);
        auto smoothedDistribution = gaussianSmooth(enhancedDistribution, 1.0);

        double bestPCScore = -2.0;
        int bestTonicPC = 0;
        std::string bestModePC = "Major";

        // Prepare weighted key profiles.
        std::array<double, 12> majorWeighted, minorWeighted;
        for (int i = 0; i < 12; ++i) {
            majorWeighted[i] = MAJOR_PROFILE[i];
            minorWeighted[i] = MINOR_PROFILE[i];
        }
        majorWeighted[4] *= 1.4;  // major third
        majorWeighted[7] *= 1.2;  // perfect fifth
        majorWeighted[11] *= 1.1; // major seventh
        minorWeighted[3] *= 1.4;  // minor third
        minorWeighted[7] *= 1.2;  // perfect fifth
        minorWeighted[10] *= 1.1; // minor seventh

        for (int keyCandidate = 0; keyCandidate < 12; ++keyCandidate) {
            std::vector<double> shiftedMajor(12), shiftedMinor(12);
            for (int i = 0; i < 12; ++i) {
                shiftedMajor[i] = majorWeighted[(i + keyCandidate) % 12];
                shiftedMinor[i] = minorWeighted[(i + keyCandidate) % 12];
            }
            double corrMajor = computePearsonCorrelation(smoothedDistribution, shiftedMajor);
            double corrMinor = computePearsonCorrelation(smoothedDistribution, shiftedMinor);
            if (corrMajor + 0.01 > bestPCScore) {
                bestPCScore = corrMajor;
                bestTonicPC = keyCandidate;
                bestModePC = "Major";
            }
            if (corrMinor > bestPCScore) {
                bestPCScore = corrMinor;
                bestTonicPC = keyCandidate;
                bestModePC = "Minor";
            }
        }
        adjustKeyEstimate(smoothedDistribution, bestTonicPC, bestModePC);

        auto intervalProfile = computeIntervalProfile(segNotes, 0); 
        double bestIntScore = -1e9;
        int bestTonicInt = 0;
        std::string bestModeInt = "Major";
        for (int keyCandidate = 0; keyCandidate < 12; ++keyCandidate) {
            auto idealMajor = idealIntervalProfile(keyCandidate, "Major");
            auto idealMinor = idealIntervalProfile(keyCandidate, "Minor");
            double prodMajor = matrixInnerProduct(intervalProfile, idealMajor);
            double prodMinor = matrixInnerProduct(intervalProfile, idealMinor);
            if (prodMajor > bestIntScore) {
                bestIntScore = prodMajor;
                bestTonicInt = keyCandidate;
                bestModeInt = "Major";
            }
            if (prodMinor > bestIntScore) {
                bestIntScore = prodMinor;
                bestTonicInt = keyCandidate;
                bestModeInt = "Minor";
            }
        }
        double combinedScore = 0.6 * bestPCScore + 0.4 * bestIntScore;
        std::string segmentKey = std::string(NOTE_NAMES[bestPCScore >= bestIntScore ? bestTonicPC : bestTonicInt])
            + " " + (bestPCScore >= bestIntScore ? bestModePC : bestModeInt);
        voteMap[segmentKey] += combinedScore;
    }
    std::string finalKey = "Unknown";
    double maxVote = -1e9;
    for (const auto& [key, vote] : voteMap) {
        if (vote > maxVote) {
            maxVote = vote;
            finalKey = key;
        }
    }
    return finalKey;
}

// =============================================================================
// Genre Detection with Enhanced Features (including spectral centroid)
// =============================================================================
std::string TransposeEngine::detectGenre(const MidiFile& midiFile) const {
    auto [notes, durations] = extractNotesAndDurations(midiFile);
    if (notes.empty())
        return "Unknown";
    double tempo = midiFile.tempoChanges.empty() ? 120.0 :
        60000000.0 / midiFile.tempoChanges[0].microsecondsPerQuarter;
    int timeSignatureNum = midiFile.timeSignatures.empty() ? 4 :
        midiFile.timeSignatures[0].numerator;
    double totalDuration = std::accumulate(durations.begin(), durations.end(), 0.0);
    double noteDensity = static_cast<double>(notes.size()) / totalDuration;
    double rhythmComplexity = calculateRhythmComplexity(durations);
    int pitchRange = notes.empty() ? 0 :
        *std::max_element(notes.begin(), notes.end()) - *std::min_element(notes.begin(), notes.end());
    double syncopation = 0.0;
    for (const auto& duration : durations) {
        double beatPosition = std::fmod(duration, 1.0);
        if (beatPosition > 0.25 && beatPosition < 0.75)
            syncopation += 1.0;
    }
    syncopation /= notes.size();
    auto enhancedDistribution = computeEnhancedPitchClassDistribution(notes, durations);
    double centroid = computeSpectralCentroid(enhancedDistribution);
    std::string spectralGenreHint;
    if (centroid < 300)
        spectralGenreHint = "Classical";
    else if (centroid > 400)
        spectralGenreHint = "Rock";
    else
        spectralGenreHint = "Contemporary";
    if (tempo >= 60 && tempo <= 80 && timeSignatureNum == 4 && pitchRange >= 48) {
        if (rhythmComplexity > 1.3)
            return "Romantic Piano";
        else
            return "Classical Piano";
    }
    if (tempo >= 100 && tempo <= 160 && noteDensity > 4 && rhythmComplexity > 1.2) {
        if (tempo >= 140 && syncopation > 0.5)
            return "Bebop Piano";
        if (rhythmComplexity > 1.4)
            return "Jazz Piano";
        return "Cool Jazz Piano";
    }
    if (tempo >= 120 && tempo <= 140 && timeSignatureNum == 4 && noteDensity <= 3) {
        return "Pop Piano";
    }
    if (tempo >= 140 && noteDensity > 5 && rhythmComplexity > 1.5 && pitchRange >= 36) {
        return "Rock Piano";
    }
    if (tempo >= 70 && tempo <= 130 && syncopation > 0.4) {
        if (tempo < 100)
            return "Blues Piano";
        return "Boogie-Woogie Piano";
    }
    if (spectralGenreHint == "Rock")
        return "Rock Piano";
    else if (spectralGenreHint == "Classical")
        return "Classical Piano";
    return "Contemporary Piano";
}

// =============================================================================
// Best Transposition Decision (Composite Evaluation)
// =============================================================================
int TransposeEngine::findBestTranspose(const std::vector<int>& notes,
    const std::vector<double>& durations,
    const std::string& detectedKey,
    const std::string& genre) const {
    if (notes.empty())
        return 0;
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

// =============================================================================
// MIDI Data Extraction
// =============================================================================
std::pair<std::vector<int>, std::vector<double>> TransposeEngine::extractNotesAndDurations(
    const MidiFile& midiFile) const {
    std::vector<int> notes;
    std::vector<double> durations;
    for (const auto& track : midiFile.tracks) {
        std::map<int, double> activeNotes;
        double currentTime = 0.0;
        double tempo = 500000.0;  // default tempo (120 BPM)
        double ticksPerQuarterNote = midiFile.division;
        double lastTick = 0.0;
        for (const auto& event : track.events) {
            if (event.status == 0xFF && event.data1 == 0x51 && event.metaData.size() == 3) {
                tempo = static_cast<double>((event.metaData[0] << 16) |
                    (event.metaData[1] << 8) |
                    event.metaData[2]);
            }
            double deltaTime = static_cast<double>(event.absoluteTick - lastTick);
            currentTime += deltaTime * (tempo / 1000000.0) / ticksPerQuarterNote;
            lastTick = event.absoluteTick;
            if ((event.status & 0xF0) == 0x90 && event.data2 > 0)
                activeNotes[event.data1] = currentTime;
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

// =============================================================================
// Fallback: Basic Pitch-Class Distribution
// =============================================================================
std::vector<double> TransposeEngine::computePitchClassDistribution(
    const std::vector<int>& notes,
    const std::vector<double>& durations) const {
    std::vector<double> distribution(12, 0.0);
    double totalDuration = std::accumulate(durations.begin(), durations.end(), 0.0);
    for (size_t i = 0; i < notes.size(); ++i) {
        int pitchClass = getPitchClass(notes[i]);
        distribution[pitchClass] += durations[i];
    }
    for (double& val : distribution)
        val /= totalDuration;
    return distribution;
}

void TransposeEngine::adjustKeyEstimate(const std::vector<double>& pitchDistribution,
    int& bestTonic,
    std::string& bestMode) const {
    if (bestTonic == 7) {  // G
        double fNatural = pitchDistribution[5];
        double fSharp = pitchDistribution[6];
        if (fSharp > fNatural * 1.2)
            bestMode = "Major";
        else if (fNatural > fSharp * 1.2)
            bestMode = "Minor";
    }
    else if (bestTonic == 9) {  // A
        double cNatural = pitchDistribution[0];
        double cSharp = pitchDistribution[1];
        double gNatural = pitchDistribution[7];
        double gSharp = pitchDistribution[8];
        if (cNatural > cSharp * 1.1 && gNatural > gSharp * 1.1)
            bestMode = "Minor";
        else if (cSharp > cNatural * 1.1 && gSharp > gNatural * 1.1)
            bestMode = "Major";
    }
    else if (bestTonic == 11) {  // B
        double ePresence = pitchDistribution[4];
        double bPresence = pitchDistribution[11];
        if (ePresence > bPresence * 1.3)
            bestTonic = 4;  // Shift to E
    }
}

// =============================================================================
// Advanced Analysis Functions
// =============================================================================
double TransposeEngine::calculateRhythmComplexity(const std::vector<double>& durations) const {
    if (durations.size() <= 1)
        return 1.0;
    std::vector<double> intervalRatios;
    intervalRatios.reserve(durations.size() - 1);
    for (size_t i = 1; i < durations.size(); ++i) {
        if (durations[i - 1] > 0)
            intervalRatios.push_back(durations[i] / durations[i - 1]);
    }
    if (intervalRatios.empty())
        return 1.0;
    double sum = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0);
    double mean = sum / intervalRatios.size();
    double variance = std::accumulate(intervalRatios.begin(), intervalRatios.end(), 0.0,
        [mean](double acc, double ratio) {
            double diff = ratio - mean;
            return acc + diff * diff;
        }) / intervalRatios.size();
    double syncopation = 0.0;
    for (const auto& ratio : intervalRatios) {
        if (std::fmod(ratio, 1.0) > 0.25 && std::fmod(ratio, 1.0) < 0.75)
            syncopation += 0.2;
    }
    double complexityMultiplier = 1.0;
    for (const auto& ratio : intervalRatios) {
        if (ratio < 0.5)
            complexityMultiplier *= 1.2;
    }
    double complexity = (std::sqrt(variance) + syncopation) * complexityMultiplier;
    return std::min(complexity, 10.0);
}

double TransposeEngine::calculateNoteDistributionEntropy(const std::vector<int>& notes,
    int transpose) const {
    std::vector<int> distribution(12, 0);
    int totalNotes = static_cast<int>(notes.size());
    for (int note : notes) {
        int pitchClass = (note + transpose) % 12;
        if (pitchClass < 0)
            pitchClass += 12;
        distribution[pitchClass]++;
    }
    double entropy = 0.0;
    for (int count : distribution) {
        if (count > 0) {
            double prob = static_cast<double>(count) / totalNotes;
            entropy -= prob * std::log2(prob);
        }
    }
    return (entropy / std::log2(12)) * 5.0;
}

double TransposeEngine::calculateIntervalComplexity(const std::vector<int>& notes,
    int transpose) const {
    if (notes.size() < 2)
        return 0.0;
    std::vector<int> intervals;
    std::map<int, int> intervalHistogram;
    for (size_t i = 1; i < notes.size(); ++i) {
        int interval = std::abs((notes[i] + transpose) - (notes[i - 1] + transpose));
        intervals.push_back(interval);
        intervalHistogram[interval % 12]++;
    }
    double complexity = 0.0;
    std::set<int> uniqueIntervals;
    int directionChanges = 0;
    for (size_t i = 2; i < notes.size(); ++i) {
        int prevDir = notes[i - 1] - notes[i - 2];
        int currDir = notes[i] - notes[i - 1];
        if ((prevDir > 0 && currDir < 0) || (prevDir < 0 && currDir > 0))
            directionChanges++;
    }
    double contourComplexity = (notes.size() > 2) ? static_cast<double>(directionChanges) / (notes.size() - 2) : 0.0;
    for (int interval : intervals) {
        interval %= 12;
        uniqueIntervals.insert(interval);
        double intervalScore = 0.0;
        switch (interval) {
        case 0: intervalScore = 0.1; break;
        case 1:
        case 2: intervalScore = 0.5; break;
        case 3:
        case 4: intervalScore = 0.7; break;
        case 5: intervalScore = 0.8; break;
        case 7: intervalScore = 0.6; break;
        case 6: intervalScore = 1.0; break;
        case 8:
        case 9: intervalScore = 0.9; break;
        case 10:
        case 11: intervalScore = 1.1; break;
        default: intervalScore = std::min(1.5, interval * 0.1);
        }
        int frequency = intervalHistogram[interval];
        double frequencyWeight = 1.0 + (1.0 / frequency);
        complexity += intervalScore * frequencyWeight;
    }
    double varietyFactor = static_cast<double>(uniqueIntervals.size()) / 12.0;
    complexity *= (1.0 + varietyFactor) * (1.0 + contourComplexity);
    int chromaticCount = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
        if (std::abs(notes[i] - notes[i - 1]) == 1)
            chromaticCount++;
    }
    double chromaticDensity = static_cast<double>(chromaticCount) / (notes.size() - 1);
    complexity *= (1.0 + chromaticDensity);
    return complexity / (notes.size() * 2.0);
}

double TransposeEngine::calculateVoiceLeadingSmoothness(const std::vector<int>& notes,
    int transpose) const {
    if (notes.size() < 3)
        return 0.0;
    double smoothness = 0.0;
    int count = 0;
    for (size_t i = 1; i < notes.size() - 1; ++i) {
        int prevInterval = std::abs((notes[i] + transpose) - (notes[i - 1] + transpose));
        int nextInterval = std::abs((notes[i + 1] + transpose) - (notes[i] + transpose));
        if (prevInterval <= 2 && nextInterval <= 2)
            smoothness += 2.0;
        else if (prevInterval <= 2 || nextInterval <= 2)
            smoothness += 1.0;
        else
            smoothness -= 1.0;
        count++;
    }
    return (count > 0) ? (smoothness / count) : 0.0;
}

double TransposeEngine::calculateHarmonicSmoothness(const std::vector<int>& notes,
    int transpose) const {
    std::vector<int> transposedNotes;
    transposedNotes.reserve(notes.size());
    for (int note : notes)
        transposedNotes.push_back(note + transpose);
    std::vector<double> counts(12, 0.0);
    for (int n : transposedNotes) {
        counts[n % 12] += 1.0;
    }
    double total = static_cast<double>(transposedNotes.size());
    for (double& c : counts)
        c /= total;
    int newKey = std::distance(counts.begin(), std::max_element(counts.begin(), counts.end()));
    std::set<int> chordTones = { newKey, (newKey + 4) % 12, (newKey + 7) % 12 };
    double chordSum = 0.0;
    for (int tone : chordTones)
        chordSum += counts[tone];
    return chordSum * 20.0;
}

double TransposeEngine::calculateMicrotonalPenalty(const std::vector<int>& notes,
    int transpose) const {
    if (notes.size() < 2)
        return 0.0;
    static const std::array<double, 12> microtonalPenaltyTable = {
        0.0, 0.0, 0.0, 16.0, 14.0, 2.0, 11.0, 2.0, 12.0, 10.0, 12.0, 10.0
    };
    double totalPenalty = 0.0;
    int count = 0;
    for (size_t i = 0; i < notes.size() - 1; ++i) {
        int interval = std::abs((notes[i + 1] + transpose) - (notes[i] + transpose)) % 12;
        totalPenalty += microtonalPenaltyTable[interval];
        count++;
    }
    double avgPenalty = (count > 0) ? totalPenalty / count : 0.0;
    return -(avgPenalty / 10.0);
}

double TransposeEngine::calculateGenreSpecificTransposeScore(int newKeyIndex,
    int keySignatureComplexity,
    const std::string& genre,
    int transpose) const {
    double score = 0.0;
    double genreWeight = 1.0;
    if (genre.find("Classical") != std::string::npos ||
        genre.find("Baroque") != std::string::npos) {
        genreWeight = 1.2;
        if (std::find(commonClassicalKeys.begin(), commonClassicalKeys.end(), newKeyIndex) != commonClassicalKeys.end())
            score += 10.0;
        score -= std::abs(transpose) * 1.2;
    }
    else if (genre.find("Jazz") != std::string::npos) {
        genreWeight = 1.1;
        if (std::find(commonJazzKeys.begin(), commonJazzKeys.end(), newKeyIndex) != commonJazzKeys.end())
            score += 8.0;
        score += KEY_COMPLEXITY[newKeyIndex] * 0.5;
        score -= std::abs(transpose) * 0.8;
    }
    else if (genre.find("Pop") != std::string::npos ||
        genre.find("Rock") != std::string::npos) {
        genreWeight = 0.9;
        if (std::find(commonPopKeys.begin(), commonPopKeys.end(), newKeyIndex) != commonPopKeys.end())
            score += 6.0;
        score -= KEY_COMPLEXITY[newKeyIndex] * 1.2;
        score -= std::abs(transpose) * 1.0;
    }
    else if (genre.find("Contemporary") != std::string::npos) {
        genreWeight = 0.7;
        score += 5.0;
        score -= std::abs(transpose) * 0.5;
    }
    return score * genreWeight;
}

double TransposeEngine::calculateCenterAlignmentScore(int minNote,
    int maxNote) const {
    double idealCenter = 60.0;
    double actualCenter = (minNote + maxNote) / 2.0;
    return -std::abs(idealCenter - actualCenter) * 2.0;
}

double TransposeEngine::calculateAdaptiveTuningAdjustment(const std::vector<int>& notes,
    int transpose) const {
    double chordConsonance = calculateHarmonicSmoothness(notes, transpose);
    if (chordConsonance > 0.7)
        return 5.0;
    else
        return -5.0;
}

double TransposeEngine::calculatePsychoacousticBalance(const std::vector<int>& notes,
    int transpose) const {
    int minNote = *std::min_element(notes.begin(), notes.end()) + transpose;
    int maxNote = *std::max_element(notes.begin(), notes.end()) + transpose;
    double avg = (minNote + maxNote) / 2.0;
    double penalty = 0.0;
    if (avg < 50)
        penalty = (50 - avg) * 0.5;
    else if (avg > 70)
        penalty = (avg - 70) * 0.5;
    return -penalty;
}

TransposeEngine::TransposeScore TransposeEngine::evaluateTranspose(const std::vector<int>& notes,
    int transpose,
    const std::string& genre) const {
    TransposeScore score;
    int minNote = *std::min_element(notes.begin(), notes.end()) + transpose;
    int maxNote = *std::max_element(notes.begin(), notes.end()) + transpose;
    if (minNote < 21 || maxNote > 108) {
        score.total = -2000.0;
        return score;
    }
    score.center = calculateCenterAlignmentScore(minNote, maxNote);
    score.entropy = calculateNoteDistributionEntropy(notes, transpose) * 6.0;
    score.intervals = calculateIntervalComplexity(notes, transpose) * 8.0;
    score.voiceLeading = calculateVoiceLeadingSmoothness(notes, transpose) * 5.0;
    score.harmonicSmoothness = calculateHarmonicSmoothness(notes, transpose) * 10.0;
    score.microtonal = calculateMicrotonalPenalty(notes, transpose);
    score.adaptiveTuning = calculateAdaptiveTuningAdjustment(notes, transpose);
    score.psychoacoustic = calculatePsychoacousticBalance(notes, transpose);
    int newKeyIndex = getPitchClass(minNote);
    score.genre = calculateGenreSpecificTransposeScore(newKeyIndex, KEY_COMPLEXITY[newKeyIndex], genre, transpose);
    score.baseScore = score.center;
    score.total = score.baseScore + score.entropy + score.intervals +
        score.voiceLeading + score.harmonicSmoothness +
        score.microtonal + score.adaptiveTuning + score.psychoacoustic +
        score.genre;
    if (transpose > 0)
        score.total += 3.0;
    return score;
}
