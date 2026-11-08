#include "Transpose.h"
#include "Input.hpp"
#include "Song.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

// =============================================================================
// Music theory sucks
// =============================================================================

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

namespace {

using Pc = std::array<double, 12>;
using Keys24 = std::array<double, 24>;

struct ProfilePair {
    Pc major;
    Pc minor;
};

constexpr ProfilePair kKrumhanslKessler{
    { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
    { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } };
constexpr ProfilePair kTemperleyKostkaPayne{
    { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 },
    { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 } };
constexpr ProfilePair kAardenEssen{
    { 17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587, 0.291248, 22.062, 0.145624, 8.15494, 0.232998, 4.95122 },
    { 18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362, 0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623 } };
constexpr ProfilePair kBellmanBudge{
    { 16.80, 0.86, 12.95, 1.41, 13.49, 11.93, 1.25, 20.28, 1.80, 8.04, 0.62, 10.57 },
    { 18.16, 0.69, 12.99, 13.34, 1.07, 11.15, 1.38, 21.07, 7.49, 1.53, 0.92, 10.21 } };
constexpr ProfilePair kSapp{
    { 2.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 2.0, 0.0, 1.0, 0.0, 1.0 },
    { 2.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 2.0, 1.0, 0.0, 0.5, 0.5 } };
constexpr ProfilePair kAlbrechtShanahanCorrelation{
    { 0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081 },
    { 0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052 } };
constexpr ProfilePair kAlbrechtShanahanDistance{
    { 0.21169, 0.00892766, 0.120448, 0.0100265, 0.131444, 0.0911768, 0.0215947, 0.204703, 0.012894, 0.0900445, 0.012617, 0.0844338 },
    { 0.201933, 0.009335, 0.107284, 0.124169, 0.0199224, 0.108324, 0.014314, 0.202699, 0.0653907, 0.0252515, 0.071959, 0.049419 } };

constexpr std::array<const ProfilePair*, 7> kProfiles = {
    &kKrumhanslKessler, &kTemperleyKostkaPayne, &kAardenEssen, &kBellmanBudge,
    &kSapp, &kAlbrechtShanahanCorrelation, &kAlbrechtShanahanDistance };

constexpr std::array<int, 12> kWeberMajorToMajor = { 1, 6, 4, 4, 5, 2, 9, 2, 5, 4, 4, 6 };
constexpr std::array<int, 12> kWeberMajorToMinor = { 2, 8, 3, 7, 3, 3, 7, 3, 8, 2, 5, 5 };

constexpr std::array<const char*, 12> kMajorNames = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
constexpr std::array<const char*, 12> kMinorNames = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
constexpr std::array<int, 12> kMajorAccidentals = { 0, 5, 2, 3, 4, 1, 6, 1, 4, 3, 2, 5 };
constexpr std::array<int, 12> kMinorAccidentals = { 3, 4, 1, 6, 1, 4, 3, 2, 5, 0, 5, 2 };

enum class Fit { Correlation, Distance };

struct Member {
    int profile;
    Fit fit;
    double weight;
};

struct Params {
    double durationMix = 0.25;
    double durationTau = 0.25;
    double durationCap = 4.0;
    double velocityFloor = 0.35;
    double bassMix = 0.5;
    double edgeMix = 0.5;
    double edgeBars = 8.0;
    double cadence = 2.0;
    double temperature = 1.0;
    int64_t windowNs = 1'500'000'000;
    double emitScale = 0.5;
    double emitCap = 24.0;
    double changeProb = 0.02;
    double rankBase = 4.0;
    double drop = 30.0;
    double folded = 5.0;
    double shift = 1.0;
    double ctrl = 1.6;
    double sameMoment = 15.0;
    double overlap = 6.0;
    double mixed = 0.5;
    double toggle = 0.5;
    double rateRef = 20.0;
    double rateCap = 3.0;
    int64_t sameMomentNs = 40'000'000;
    int64_t batchNs = 2'000'000;
    int64_t groupNs = 2'000'000;
    int64_t toggleNs = 120'000'000;
    double keyPrior = 0.03;
    double tapPrior = 0.02;
    double pitchPrior = 0.5;
    double octavePrior = 0.05;
    double voiceBoost = 1.5;
    std::array<Member, 8> members = { {
        { 0, Fit::Correlation, 1.0 }, { 1, Fit::Correlation, 1.0 }, { 2, Fit::Correlation, 1.0 },
        { 3, Fit::Correlation, 1.0 }, { 4, Fit::Correlation, 1.0 }, { 5, Fit::Correlation, 1.0 },
        { 6, Fit::Correlation, 1.0 }, { 6, Fit::Distance, 1.0 } } };
};

const Params kDefaults{};

constexpr double kSmoothing = 0.01;

struct KeyModel {
    std::array<Pc, 24> unit{};
    std::array<Pc, 24> centered{};
    std::array<Pc, 24> logp{};
    Keys24 norm{};
};

KeyModel make_model(const ProfilePair& p) {
    KeyModel m;
    for (int k = 0; k < 24; ++k) {
        const Pc& src = k < 12 ? p.major : p.minor;
        const int tonic = k % 12;
        const double sum = std::accumulate(src.begin(), src.end(), 0.0);
        const double mean = sum / 12.0;
        double ss = 0.0;
        for (int pc = 0; pc < 12; ++pc) {
            const double v = src[(pc - tonic + 12) % 12];
            m.unit[k][pc] = v / sum;
            m.centered[k][pc] = v - mean;
            m.logp[k][pc] = std::log((v / sum + kSmoothing) / (1.0 + 12.0 * kSmoothing));
            ss += (v - mean) * (v - mean);
        }
        m.norm[k] = std::sqrt(ss);
    }
    return m;
}

const std::array<KeyModel, 7>& models() {
    static const std::array<KeyModel, 7> cache = [] {
        std::array<KeyModel, 7> m;
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = make_model(*kProfiles[i]);
        return m;
    }();
    return cache;
}

int tonic_of(int key) { return key % 12; }
bool minor_key(int key) { return key >= 12; }

std::string key_name(int key) {
    if (key < 0 || key >= 24)
        return "Unknown";
    return std::string(minor_key(key) ? kMinorNames[tonic_of(key)] : kMajorNames[tonic_of(key)]) +
        (minor_key(key) ? " minor" : " major");
}

int accidentals(int key) {
    return minor_key(key) ? kMinorAccidentals[tonic_of(key)] : kMajorAccidentals[tonic_of(key)];
}

int moved_key(int key, int semitones) {
    if (key < 0)
        return -1;
    return (minor_key(key) ? 12 : 0) + ((tonic_of(key) + semitones) % 12 + 12) % 12;
}

int parse_key_name(const std::string& s) {
    static constexpr int letters[7] = { 9, 11, 0, 2, 4, 5, 7 };
    if (s.empty())
        return -1;
    const char l = static_cast<char>(s[0] & ~0x20);
    if (l < 'A' || l > 'G')
        return -1;
    int pc = letters[l - 'A'];
    size_t i = 1;
    if (i < s.size() && s[i] == '#') {
        ++pc;
        ++i;
    }
    else if (i < s.size() && s[i] == 'b') {
        --pc;
        ++i;
    }
    while (i < s.size() && s[i] == ' ')
        ++i;
    if (i + 5 > s.size())
        return -1;
    const char m = static_cast<char>(s[i] | 0x20);
    const char mode = static_cast<char>(s[i + 1] | 0x20);
    if (m != 'm' || (mode != 'a' && mode != 'i'))
        return -1;
    return (mode == 'i' ? 12 : 0) + (pc % 12 + 12) % 12;
}

int weber_rank(int from, int to) {
    const int d = (tonic_of(to) - tonic_of(from) + 12) % 12;
    if (minor_key(from) == minor_key(to))
        return kWeberMajorToMajor[d];
    if (!minor_key(from))
        return kWeberMajorToMinor[d];
    return kWeberMajorToMinor[(d + 9) % 12];
}

Keys24 fit_keys(const KeyModel& m, Fit fit, const Pc& x) {
    Keys24 s{};
    const double sum = std::accumulate(x.begin(), x.end(), 0.0);
    if (sum <= 0.0)
        return s;
    if (fit == Fit::Correlation) {
        const double mean = sum / 12.0;
        Pc c;
        double nx = 0.0;
        for (int pc = 0; pc < 12; ++pc) {
            c[pc] = x[pc] - mean;
            nx += c[pc] * c[pc];
        }
        nx = std::sqrt(nx);
        if (nx <= 0.0)
            return s;
        for (int k = 0; k < 24; ++k) {
            double dot = 0.0;
            for (int pc = 0; pc < 12; ++pc)
                dot += c[pc] * m.centered[k][pc];
            s[k] = dot / (nx * m.norm[k]);
        }
        return s;
    }
    for (int k = 0; k < 24; ++k) {
        double acc = 0.0;
        for (int pc = 0; pc < 12; ++pc) {
            const double v = x[pc] / sum - m.unit[k][pc];
            acc += v * v;
        }
        s[k] = -std::sqrt(acc);
    }
    return s;
}

Keys24 standardize(const Keys24& s) {
    const double mean = std::accumulate(s.begin(), s.end(), 0.0) / 24.0;
    double var = 0.0;
    for (double v : s)
        var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / 24.0);
    Keys24 z{};
    if (sd <= 1e-12)
        return z;
    for (int k = 0; k < 24; ++k)
        z[k] = (s[k] - mean) / sd;
    return z;
}

Pc normalized(const Pc& x) {
    const double sum = std::accumulate(x.begin(), x.end(), 0.0);
    Pc r{};
    if (sum > 0.0)
        for (int pc = 0; pc < 12; ++pc)
            r[pc] = x[pc] / sum;
    return r;
}

struct KeyEvidence {
    Pc duration{};
    Pc count{};
    Pc bass{};
    Pc edge{};
    int finalBass = -1;
    uint16_t finalPcs = 0;
    std::vector<Pc> windows;
    std::vector<double> windowNotes;
    int64_t windowNs = 0;
    int64_t length = 0;
};

struct KeyResult {
    int key = -1;
    double confidence = 0.0;
    Keys24 posterior{};
    std::vector<KeySpan> spans;
};

Pc blend(const KeyEvidence& e, const Params& P) {
    const Pc d = normalized(e.duration), c = normalized(e.count), b = normalized(e.bass), g = normalized(e.edge);
    Pc x{};
    for (int pc = 0; pc < 12; ++pc)
        x[pc] = P.durationMix * d[pc] + (1.0 - P.durationMix) * c[pc] + P.bassMix * b[pc] + P.edgeMix * g[pc];
    return x;
}

Keys24 ensemble(const Pc& x, const Params& P) {
    const auto& ms = models();
    Keys24 total{};
    for (const Member& mb : P.members) {
        if (mb.weight == 0.0)
            continue;
        const Keys24 z = standardize(fit_keys(ms[static_cast<size_t>(mb.profile)], mb.fit, x));
        for (int k = 0; k < 24; ++k)
            total[k] += mb.weight * z[k];
    }
    return total;
}

Keys24 cadence_cue(const KeyEvidence& e) {
    Keys24 cue{};
    if (e.finalBass < 0)
        return cue;
    const bool majorThird = (e.finalPcs >> ((e.finalBass + 4) % 12)) & 1;
    const bool minorThird = (e.finalPcs >> ((e.finalBass + 3) % 12)) & 1;
    cue[e.finalBass] = 1.0 + (majorThird && !minorThird ? 0.5 : 0.0);
    cue[12 + e.finalBass] = 1.0 + (minorThird && !majorThird ? 0.5 : 0.0);
    return cue;
}

std::array<Keys24, 24> transitions(const Params& P) {
    std::array<Keys24, 24> a{};
    for (int i = 0; i < 24; ++i) {
        double sum = 0.0;
        for (int j = 0; j < 24; ++j)
            if (j != i)
                sum += std::pow(P.rankBase, -(weber_rank(i, j) - 1));
        for (int j = 0; j < 24; ++j)
            a[i][j] = j == i ? std::log(1.0 - P.changeProb)
                : std::log(P.changeProb * std::pow(P.rankBase, -(weber_rank(i, j) - 1)) / sum);
    }
    return a;
}

std::vector<int> track_keys(const KeyEvidence& e, const Params& P) {
    const size_t w = e.windows.size();
    std::vector<int> path(w, -1);
    if (!w)
        return path;
    const auto& ms = models();
    std::array<Pc, 24> logp{};
    double used = 0.0;
    for (const Member& mb : P.members) {
        if (mb.weight == 0.0)
            continue;
        used += mb.weight;
        for (int k = 0; k < 24; ++k)
            for (int pc = 0; pc < 12; ++pc)
                logp[k][pc] += mb.weight * ms[static_cast<size_t>(mb.profile)].logp[k][pc];
    }
    if (used <= 0.0)
        return path;
    for (auto& row : logp)
        for (double& v : row)
            v /= used;
    const std::array<Keys24, 24> a = transitions(P);
    std::vector<std::array<uint8_t, 24>> back(w);
    Keys24 cur{};
    for (size_t i = 0; i < w; ++i) {
        Keys24 emit{};
        const double mass = std::accumulate(e.windows[i].begin(), e.windows[i].end(), 0.0);
        if (mass > 0.0) {
            const double scale = P.emitScale * std::min(e.windowNotes[i], P.emitCap) / mass;
            for (int k = 0; k < 24; ++k) {
                double acc = 0.0;
                for (int pc = 0; pc < 12; ++pc)
                    acc += e.windows[i][pc] * logp[k][pc];
                emit[k] = scale * acc;
            }
        }
        if (i == 0) {
            cur = emit;
            continue;
        }
        Keys24 next{};
        for (int j = 0; j < 24; ++j) {
            int best = 0;
            double bv = -std::numeric_limits<double>::infinity();
            for (int k = 0; k < 24; ++k) {
                const double v = cur[k] + a[k][j];
                if (v > bv) {
                    bv = v;
                    best = k;
                }
            }
            back[i][j] = static_cast<uint8_t>(best);
            next[j] = bv + emit[j];
        }
        cur = next;
    }
    int k = static_cast<int>(std::max_element(cur.begin(), cur.end()) - cur.begin());
    for (size_t i = w; i-- > 0;) {
        path[i] = k;
        k = back[i][k];
    }
    return path;
}

KeyResult find_key(const KeyEvidence& e, const Params& P) {
    KeyResult r;
    const Pc x = blend(e, P);
    if (std::accumulate(x.begin(), x.end(), 0.0) <= 0.0)
        return r;
    Keys24 score = ensemble(x, P);
    const Keys24 cue = cadence_cue(e);
    for (int k = 0; k < 24; ++k)
        score[k] += P.cadence * cue[k];
    const double top = *std::max_element(score.begin(), score.end());
    double sum = 0.0;
    for (int k = 0; k < 24; ++k) {
        r.posterior[k] = std::exp((score[k] - top) / P.temperature);
        sum += r.posterior[k];
    }
    for (double& v : r.posterior)
        v /= sum;
    r.key = static_cast<int>(std::max_element(r.posterior.begin(), r.posterior.end()) - r.posterior.begin());
    r.confidence = r.posterior[r.key];
    const std::vector<int> path = track_keys(e, P);
    for (size_t i = 0; i < path.size();) {
        size_t j = i;
        while (j < path.size() && path[j] == path[i])
            ++j;
        KeySpan s;
        s.start = static_cast<int64_t>(i) * e.windowNs;
        s.end = std::min(static_cast<int64_t>(j) * e.windowNs, std::max(e.length, s.start));
        s.key = path[i];
        s.name = key_name(path[i]);
        r.spans.push_back(std::move(s));
        i = j;
    }
    return r;
}

struct Layout {
    std::array<uint16_t, 128> scan{};
    std::array<uint8_t, 128> mods{};
    std::array<bool, 128> mapped{};
    std::array<int, 128> fold{};
    std::vector<std::pair<int, int>> pairs;
    std::array<std::array<uint64_t, 2>, 128> partners{};
    uint16_t modifiedPcs = 0;
};

Layout make_layout(const std::map<std::string, std::string>& keys, bool typed) {
    Layout l;
    const HKL hkl = typed ? GetKeyboardLayout(0) : nullptr;
    for (const auto& [note, key] : keys) {
        const int n = midi::noteNameToMidi(note);
        if (n < 0)
            continue;
        const std::optional<input::Stroke> s = typed ? input::parse_typed(key, hkl) : input::parse_key(key);
        if (!s || !*s)
            continue;
        l.mapped[n] = true;
        l.scan[n] = s->scan;
        l.mods[n] = s->mods;
    }
    for (int n = 0; n < 128; ++n) {
        l.fold[n] = -1;
        if (l.mapped[n])
            continue;
        for (int k = 12; k < 128 && l.fold[n] < 0; k += 12) {
            if (n + k < 128 && l.mapped[n + k])
                l.fold[n] = n + k;
            else if (n - k >= 0 && l.mapped[n - k])
                l.fold[n] = n - k;
        }
    }
    std::array<bool, 128> gaps{};
    for (int a = 0; a < 128; ++a)
        for (int b = a + 1; b < 128; ++b)
            if (l.mapped[a] && l.mapped[b] && l.scan[a] == l.scan[b]) {
                l.pairs.push_back({ a, b });
                gaps[b - a] = true;
            }
    for (int p = 0; p < 128; ++p)
        for (int d = 1; d < 128; ++d) {
            if (!gaps[d])
                continue;
            for (int q : { p - d, p + d })
                if (q >= 0 && q < 128)
                    l.partners[p][q >> 6] |= 1ull << (q & 63);
        }
    std::array<int, 12> modified{}, plain{};
    for (int n = 0; n < 128; ++n)
        if (l.mapped[n])
            ++(l.mods[n] ? modified : plain)[n % 12];
    for (int pc = 0; pc < 12; ++pc)
        if (modified[pc] > plain[pc])
            l.modifiedPcs |= static_cast<uint16_t>(1u << pc);
    return l;
}

uint16_t rotate12(uint16_t m, int r) {
    return static_cast<uint16_t>(((m << r) | (m >> (12 - r))) & 0xFFF);
}

struct Usage {
    std::array<double, 128> count{};
    std::array<double, 128> salience{};
    std::array<double, 128> density{};
    std::vector<double> sameMoment;
    std::vector<double> overlap;
    std::vector<int> touching;
    std::array<double, 12> mixedGroups{};
    std::array<double, 12> toggles{};
    double weight = 0.0;
    int notes = 0;
};

struct Scan {
    Usage use;
    KeyEvidence key;
};

double velocity_factor(int vel, const Params& P) {
    return P.velocityFloor + (1.0 - P.velocityFloor) * std::clamp(vel, 0, 127) / 127.0;
}

double duration_term(double seconds, const Params& P) {
    return std::log1p(std::clamp(seconds, 0.0, P.durationCap) / P.durationTau);
}

std::vector<double> duration_table(const Params& P) {
    std::vector<double> table(static_cast<size_t>(P.durationCap * 1000.0) + 1);
    for (size_t ms = 0; ms < table.size(); ++ms)
        table[ms] = duration_term(static_cast<double>(ms) / 1000.0, P);
    return table;
}

double beat_at(const Song& s, int64_t ns) {
    double beats = 0.0, bpm = 120.0;
    int64_t base = 0;
    for (const auto& [at, tempo] : s.tempos) {
        if (at > ns)
            break;
        beats += static_cast<double>(at - base) * bpm / 60e9;
        base = at;
        bpm = tempo > 0.0 ? tempo : bpm;
    }
    return beats + static_cast<double>(ns - base) * bpm / 60e9;
}

int64_t time_at_beat(const Song& s, double target) {
    double beats = 0.0, bpm = 120.0;
    int64_t base = 0;
    for (const auto& [at, tempo] : s.tempos) {
        const double next = beats + static_cast<double>(at - base) * bpm / 60e9;
        if (next >= target)
            break;
        beats = next;
        base = at;
        bpm = tempo > 0.0 ? tempo : bpm;
    }
    return base + static_cast<int64_t>((target - beats) * 60e9 / bpm);
}

Scan scan_song(const Song& song, const std::vector<uint8_t>& enabled, const Layout& layout, int64_t batchNs, const Params& P) {
    Scan out;
    Usage& u = out.use;
    KeyEvidence& ke = out.key;
    u.sameMoment.assign(128 * 128, 0.0);
    u.overlap.assign(128 * 128, 0.0);
    u.touching.assign(128 * 128, 0);

    const size_t n = song.t.size();
    const int64_t end = song.length > 0 ? song.length : (n ? song.t.back() : 0);
    int64_t win = P.windowNs;
    while (end / win > 16384)
        win *= 2;
    ke.windowNs = win;
    ke.length = end;
    const double perWindow = 1.0 / static_cast<double>(win);
    const size_t windows = static_cast<size_t>(end / win) + 1;
    const std::vector<double> durations = duration_table(P);
    const int64_t longest = static_cast<int64_t>(durations.size()) - 1;
    const double perRate = 1.0 / P.rateRef;
    const double batchSec = static_cast<double>(batchNs) / 1e9;
    const int64_t together = std::max(P.batchNs, batchNs);
    ke.windows.assign(windows, Pc{});
    ke.windowNotes.assign(windows, 0.0);

    const double edgeBeats = P.edgeBars * std::max(1.0, song.tsNum * 4.0 / std::max(1, song.tsDen));
    const int64_t edgeStart = time_at_beat(song, edgeBeats);
    const int64_t edgeEnd = time_at_beat(song, beat_at(song, end) - edgeBeats);

    std::array<uint16_t, 4096> statusOf{}, mixedOf{};
    for (int m = 0; m < 4096; ++m)
        for (int r = 0; r < 12; ++r) {
            const uint16_t rot = rotate12(static_cast<uint16_t>(m), r);
            const bool mod = (rot & layout.modifiedPcs) != 0;
            const bool plain = (rot & ~layout.modifiedPcs & 0xFFF) != 0;
            if (mod)
                statusOf[m] |= static_cast<uint16_t>(1u << r);
            if (mod && plain)
                mixedOf[m] |= static_cast<uint16_t>(1u << r);
        }
    std::array<double, 4096> mixedAcc{}, toggleAcc{};

    std::array<float, 128> lastVelocity{};
    std::array<uint32_t, 128> lastWindow{};
    std::array<uint8_t, 128> lastFlags{};
    std::array<int, 128> held{};
    std::array<uint64_t, 2> heldMask{}, recentMask{};
    std::array<int64_t, 128> lastOn;
    lastOn.fill(std::numeric_limits<int64_t>::min() / 2);

    int64_t groupStart = -1, prevGroup = -1;
    std::array<uint64_t, 2> groupMask{};
    uint16_t groupPcs = 0, prevStatus = 0;
    std::array<std::pair<int64_t, uint16_t>, 64> tail{};
    size_t tailCount = 0;
    int lastBass = -1;

    std::array<int, 128> buckets{};
    int64_t bucketAt = 0;
    int inWindow = 0;

    const auto finish = [&](int pitch, int64_t stop) {
        const int64_t a = std::max<int64_t>(0, lastOn[pitch]);
        const int64_t len = std::max<int64_t>(0, stop - a);
        const double term = durations[static_cast<size_t>(std::min(len / 1'000'000, longest))];
        const int pc = pitch % 12;
        const double v = lastVelocity[pitch];
        ke.duration[pc] += v * term;
        if (lastFlags[pitch] & 1)
            ke.bass[pc] += v * term;
        if (lastFlags[pitch] & 2)
            ke.edge[pc] += v * term;
        size_t wi = lastWindow[pitch];
        if (wi >= windows)
            return;
        if (!len) {
            ke.windows[wi][pc] += v * 0.05;
            return;
        }
        const int64_t b = a + std::min(len, 8 * win);
        for (int64_t at = a; at < b && wi < windows; ++wi) {
            const int64_t next = std::min(b, static_cast<int64_t>(wi + 1) * win);
            ke.windows[wi][pc] += v * static_cast<double>(next - at) * perWindow;
            at = next;
        }
    };

    const auto closeGroup = [&]() {
        if (groupStart < 0)
            return;
        const auto lowest = [](const std::array<uint64_t, 2>& m) {
            return m[0] ? std::countr_zero(m[0]) : (m[1] ? 64 + std::countr_zero(m[1]) : -1);
        };
        const auto highest = [](const std::array<uint64_t, 2>& m) {
            return m[1] ? 127 - std::countl_zero(m[1]) : (m[0] ? 63 - std::countl_zero(m[0]) : -1);
        };
        std::array<uint64_t, 2> sounding = { heldMask[0] | groupMask[0], heldMask[1] | groupMask[1] };
        const int lowP = lowest(sounding), highP = highest(sounding);
        if (lowP >= 0 && ((groupMask[lowP >> 6] >> (lowP & 63)) & 1)) {
            lastBass = lowP % 12;
            lastFlags[lowP] |= 1;
            u.salience[lowP] += (P.voiceBoost - 1.0) * lastVelocity[lowP];
        }
        if (highP >= 0 && highP != lowP && ((groupMask[highP >> 6] >> (highP & 63)) & 1))
            u.salience[highP] += (P.voiceBoost - 1.0) * lastVelocity[highP];
        const uint16_t status = statusOf[groupPcs];
        mixedAcc[mixedOf[groupPcs]] += 1.0;
        if (prevGroup >= 0 && groupStart - prevGroup < P.toggleNs)
            toggleAcc[status ^ prevStatus] += 1.0 - static_cast<double>(groupStart - prevGroup) / static_cast<double>(P.toggleNs);
        prevStatus = status;
        prevGroup = groupStart;
        tail[tailCount++ % tail.size()] = { groupStart, groupPcs };
        groupStart = -1;
        groupMask = {};
        groupPcs = 0;
    };

    for (size_t i = 0; i < n; ++i) {
        const uint32_t e = song.ev[i];
        const uint32_t kind = Song::kind(e);
        if (kind == Song::Pedal)
            continue;
        const size_t tr = static_cast<size_t>(Song::track(e));
        if (tr >= enabled.size() || !enabled[tr])
            continue;
        const int p = Song::note(e);
        const int64_t t = song.t[i];
        if (kind == Song::Off) {
            if (!held[p])
                continue;
            finish(p, t);
            if (--held[p] == 0)
                heldMask[p >> 6] &= ~(1ull << (p & 63));
            continue;
        }
        if (groupStart >= 0 && t - groupStart > P.groupNs)
            closeGroup();
        if (groupStart < 0)
            groupStart = t;
        const int64_t bucket = std::max<int64_t>(0, t) >> 23;
        if (bucket - bucketAt >= 128) {
            buckets.fill(0);
            inWindow = 0;
        }
        else
            for (int64_t k = bucketAt + 1; k <= bucket; ++k) {
                inWindow -= buckets[static_cast<size_t>(k & 127)];
                buckets[static_cast<size_t>(k & 127)] = 0;
            }
        bucketAt = std::max(bucketAt, bucket);
        ++buckets[static_cast<size_t>(bucket & 127)];
        ++inWindow;
        const double rate = static_cast<double>(inWindow);
        const double vel = velocity_factor(Song::vel(e), P);
        const double shared = batchSec > 0.0 ? vel / std::max(1.0, rate * batchSec) : vel;
        ++u.notes;
        u.count[p] += 1.0;
        u.salience[p] += vel;
        u.density[p] += shared * std::min(P.rateCap, 1.0 + rate * perRate);
        const int pc = p % 12;
        ke.count[pc] += vel;
        const size_t wi = static_cast<size_t>(static_cast<double>(std::max<int64_t>(0, t)) * perWindow);
        if (wi < windows)
            ke.windowNotes[wi] += 1.0;
        const uint8_t flags = t < edgeStart || t > edgeEnd ? 2 : 0;

        const std::array<uint64_t, 2>& pm = layout.partners[p];
        for (int h = 0; h < 2; ++h) {
            uint64_t cand = (heldMask[h] | recentMask[h]) & pm[h];
            while (cand) {
                const int bit = std::countr_zero(cand);
                cand &= cand - 1;
                const int q = h * 64 + bit;
                const int64_t dt = t - lastOn[q];
                const int a = std::min(p, q), b = std::max(p, q);
                if (dt <= together || (held[q] > 0 && dt <= P.sameMomentNs))
                    u.sameMoment[a * 128 + b] += shared;
                else if (held[q] > 0)
                    u.overlap[a * 128 + b] += shared;
                else {
                    recentMask[h] &= ~(1ull << bit);
                    continue;
                }
                ++u.touching[a * 128 + b];
            }
        }
        lastOn[p] = t;
        lastVelocity[p] = static_cast<float>(vel);
        lastWindow[p] = static_cast<uint32_t>(std::min(wi, windows));
        lastFlags[p] = flags;
        recentMask[p >> 6] |= 1ull << (p & 63);
        if (held[p]++ == 0)
            heldMask[p >> 6] |= 1ull << (p & 63);
        groupMask[p >> 6] |= 1ull << (p & 63);
        groupPcs |= static_cast<uint16_t>(1u << pc);
    }
    closeGroup();
    for (int p = 0; p < 128; ++p)
        for (int k = 0; k < held[p]; ++k)
            finish(p, end);

    for (int m = 0; m < 4096; ++m) {
        for (int r = 0; r < 12; ++r) {
            if ((m >> r) & 1) {
                u.mixedGroups[r] += mixedAcc[m];
                u.toggles[r] += toggleAcc[m];
            }
        }
    }
    for (double v : u.salience)
        u.weight += v;

    ke.finalBass = lastBass;
    if (tailCount) {
        const int64_t lastStart = tail[(tailCount - 1) % tail.size()].first;
        for (size_t k = 0; k < std::min(tailCount, tail.size()); ++k) {
            const auto& g = tail[(tailCount - 1 - k) % tail.size()];
            if (lastStart - g.first > 2'500'000'000)
                break;
            ke.finalPcs |= g.second;
        }
    }
    return out;
}

double prior(int moved, int playedKey, bool keepPitch, double heard, const Params& P) {
    double c = playedKey >= 0 ? P.keyPrior * heard * accidentals(playedKey) / 6.0 : 0.0;
    if (keepPitch)
        return c + P.tapPrior * std::abs(moved) / 12.0;
    if (moved % 12)
        c += P.pitchPrior * (1.0 + std::abs(moved) / 12.0);
    return c + P.octavePrior * std::abs(moved) / 12.0;
}

ShiftScore score_shift(const Usage& u, const Layout& l, int s, int moved, int key, const TransposeOptions& opt, const Params& P) {
    ShiftScore r;
    r.noteShift = s;
    double cost = 0.0, heard = 0.0;
    for (int p = 0; p < 128; ++p) {
        if (u.count[p] <= 0.0)
            continue;
        int k = p + s;
        bool in = k >= 0 && k < 128 && l.mapped[k];
        if (!in) {
            r.outOfRange += static_cast<int>(u.count[p]);
            const int f = k >= 0 && k < 128 ? l.fold[k] : -1;
            if (!opt.fold || f < 0) {
                cost += P.drop * u.salience[p];
                continue;
            }
            cost += P.folded * u.salience[p];
            k = f;
        }
        heard += u.salience[p];
        if (l.mods[k]) {
            r.needsModifier += static_cast<int>(u.count[p]);
            cost += (l.mods[k] & input::Ctrl ? P.ctrl : P.shift) * u.density[p];
        }
    }
    for (const auto& [a, b] : l.pairs) {
        const int pa = a - s, pb = b - s;
        if (pa < 0 || pb > 127 || u.touching.empty())
            continue;
        const size_t at = static_cast<size_t>(pa) * 128 + static_cast<size_t>(pb);
        r.sameKeyCollisions += u.touching[at];
        cost += P.sameMoment * u.sameMoment[at] + P.overlap * u.overlap[at];
    }
    const int rot = ((s % 12) + 12) % 12;
    r.mixedChords = static_cast<int>(u.mixedGroups[rot]);
    cost += P.mixed * u.mixedGroups[rot] + P.toggle * u.toggles[rot];
    if (u.weight > 0.0)
        r.cost = cost / u.weight + prior(moved, moved_key(key, s), opt.keepPitch, heard / u.weight, P);
    return r;
}

void choose(TransposePlan& plan, const Usage& u, const Layout& l, const TransposeOptions& opt, int key, const Params& P) {
    const int range = std::clamp(opt.range, 0, 48);
    const int base = std::clamp(opt.key, -48, 48);
    plan.shifts.clear();
    plan.noteShift = base;
    plan.semitones = 0;
    plan.total = u.notes;
    if (!u.notes)
        return;
    double best = std::numeric_limits<double>::infinity();
    int bestMove = 0;
    for (int d = -range; d <= range; ++d) {
        const ShiftScore r = score_shift(u, l, base + d, d, key, opt, P);
        if (d == 0)
            plan.unshifted = r;
        if (r.cost < best - 1e-9 || (std::abs(r.cost - best) <= 1e-9 && std::abs(d) < std::abs(bestMove))) {
            best = r.cost;
            bestMove = d;
            plan.outOfRange = r.outOfRange;
            plan.needsModifier = r.needsModifier;
            plan.sameKeyCollisions = r.sameKeyCollisions;
        }
        plan.shifts.push_back(r);
    }
    plan.noteShift = base + bestMove;
    plan.semitones = opt.keepPitch ? -bestMove : 0;
}

std::vector<uint8_t> enabled_tracks(const Song& song, const std::vector<bool>& tracks) {
    std::vector<uint8_t> en(song.tracks.size(), 0);
    for (size_t i = 0; i < en.size(); ++i)
        en[i] = i < tracks.size() ? static_cast<uint8_t>(tracks[i]) : static_cast<uint8_t>(!song.tracks[i].drums);
    return en;
}

std::vector<uint8_t> tonal_tracks(const Song& song, const std::vector<uint8_t>& played) {
    std::vector<uint8_t> en(song.tracks.size(), 0);
    bool any = false;
    for (size_t i = 0; i < en.size(); ++i) {
        en[i] = static_cast<uint8_t>(!song.tracks[i].drums);
        any = any || (en[i] && song.tracks[i].notes);
    }
    return any ? en : played;
}

int64_t batch_ns(const Song& song, int batchMs) {
    const int ms = batchMs >= 0 ? batchMs : (song.peakNotesPerSec >= 2000.0 ? 4 : 0);
    return static_cast<int64_t>(ms) * 1'000'000;
}

TransposePlan run(const Song& song, const Layout& layout, const TransposeOptions& opt, const Params& P) {
    TransposePlan plan;
    const std::vector<uint8_t> played = enabled_tracks(song, opt.tracks);
    const std::vector<uint8_t> tonal = tonal_tracks(song, played);
    const Scan sc = scan_song(song, played, layout, batch_ns(song, opt.batchMs), P);
    const KeyResult kr = find_key(tonal == played ? sc.key : scan_song(song, tonal, Layout{}, 0, P).key, P);
    plan.keyIndex = kr.key;
    plan.key = key_name(kr.key);
    plan.keyConfidence = kr.confidence;
    plan.keys = kr.spans;
    choose(plan, sc.use, layout, opt, kr.key, P);
    plan.playedKey = key_name(moved_key(kr.key, plan.noteShift));
    return plan;
}

KeyEvidence evidence_from_list(const std::vector<int>& notes, const std::vector<double>& durations, const Params& P) {
    KeyEvidence e;
    const size_t n = std::min(notes.size(), durations.size());
    const size_t edge = std::max<size_t>(1, n / 10);
    for (size_t i = 0; i < n; ++i) {
        const int pc = ((notes[i] % 12) + 12) % 12;
        const double term = duration_term(durations[i], P);
        e.duration[pc] += term;
        e.count[pc] += 1.0;
        if (i < edge || i + edge >= n)
            e.edge[pc] += term;
    }
    return e;
}

const std::map<std::string, std::string>& layout_keys(const std::string& name) {
    static const std::map<std::string, std::string> none;
    const auto& km = midi::Config::getInstance().key_mappings;
    auto it = km.find(name);
    return it == km.end() ? none : it->second;
}

}

std::string TransposeEngine::estimateKey(const std::vector<int>& notes,
    const std::vector<double>& durations) const {
    if (notes.empty() || durations.empty())
        return "Unknown";
    return key_name(find_key(evidence_from_list(notes, durations, kDefaults), kDefaults).key);
}

// =============================================================================
// Genre Detection with Enhanced Features (including spectral centroid)
// =============================================================================
std::string TransposeEngine::detectGenre(const std::vector<int>& notes,
    const std::vector<double>& durations, double tempo, int timeSignatureNum) const {
    if (notes.empty())
        return "Unknown";
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
    const std::string&) const {
    if (notes.empty())
        return 0;
    const auto& cfg = midi::Config::getInstance();
    const Layout layout = make_layout(layout_keys(cfg.playback.keys88 ? "FULL" : "LIMITED"), cfg.playback.typedKeys);
    Usage u;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i] < 0 || notes[i] > 127)
            continue;
        u.count[notes[i]] += 1.0;
        u.density[notes[i]] += 1.0;
        u.salience[notes[i]] += 1.0;
        ++u.notes;
    }
    for (double v : u.salience)
        u.weight += v;
    int key = parse_key_name(detectedKey);
    if (key < 0)
        key = find_key(evidence_from_list(notes, durations, kDefaults), kDefaults).key;
    TransposePlan plan;
    TransposeOptions opt;
    opt.fold = cfg.playback.outOfRange;
    choose(plan, u, layout, opt, key, kDefaults);
    return plan.semitones;
}

TransposePlan TransposeEngine::plan(const Song& song, const TransposeOptions& options) const {
    return plan(song, layout_keys(options.layout), options);
}

TransposePlan TransposeEngine::plan(const Song& song, const std::map<std::string, std::string>& keys,
    const TransposeOptions& options) const {
    return run(song, make_layout(keys, midi::Config::getInstance().playback.typedKeys), options, kDefaults);
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
