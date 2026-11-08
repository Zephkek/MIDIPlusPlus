#include "Song.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace {

    constexpr uint32_t NONE = UINT32_MAX;

    const char* const kGm[128] = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavi","Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion","Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)","Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar harmonics",
        "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2","Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","SynthStrings 1","SynthStrings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit","Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","SynthBrass 1","SynthBrass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet","Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope)","Lead 4 (chiff)","Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)","Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)","Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
        "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)","FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)","Sitar","Banjo","Shamisen","Koto","Kalimba","Bag pipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal","Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
    };

    // READING 4 BYTES OF PURE MIDI MAGIC - OR COMPLETE GARBAGE
    uint32_t be32(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }

    uint32_t le32(const uint8_t* p) {
        return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
    }

    uint16_t be16(const uint8_t* p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    bool vlq(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end)
                return false;
            uint8_t b = *p++;
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80))
                return true;
        }
        return false;
    }

    std::vector<uint8_t> read_file(const std::filesystem::path& path) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Unable to open file");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size) || size.QuadPart > 0x7FFFFFFF) {
            CloseHandle(h);
            throw std::runtime_error("File is too big");
        }
        std::vector<uint8_t> data(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        bool ok = true;
        while (ok && done < data.size()) {
            DWORD got = 0;
            DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - done, 64u << 20));
            ok = ReadFile(h, data.data() + done, want, &got, nullptr) && got;
            done += got;
        }
        CloseHandle(h);
        if (!ok)
            throw std::runtime_error("Error reading file");
        return data;
    }

    struct Stat {
        std::array<uint32_t, 128> hist{};
        std::array<uint64_t, 16> chans{};
        uint64_t total = 0, offs = 0, sumPitch = 0, velN = 0;
        double velSum = 0.0, velSq = 0.0;
        int minPitch = 127, maxPitch = 0;
        uint32_t firstTick = UINT32_MAX, lastTick = 0;
    };

    struct Cursor {
        const uint8_t* p = nullptr;
        const uint8_t* e = nullptr;
        uint32_t tick = 0;
        uint32_t tempo = 0;
        uint8_t running = 0;
        uint8_t status = 0;
        uint8_t d1 = 0;
        uint8_t d2 = 0;
    };

    struct Meta {
        uint32_t tsTick = UINT32_MAX;
        int tsNum = 4, tsDen = 4;
    };

    bool next(Cursor& c, SongTrack& info, Stat& st, Meta& meta) {
        while (c.p < c.e) {
            uint32_t delta;
            if (!vlq(c.p, c.e, delta) || c.p >= c.e)
                return false;
            c.tick = UINT32_MAX - c.tick < delta ? UINT32_MAX : c.tick + delta;

            uint8_t status = *c.p;
            if (status & 0x80) {
                ++c.p;
                if (status < 0xF0)
                    c.running = status;
            }
            else if (c.running) {
                status = c.running;
            }
            else {
                ++c.p;
                continue;
            }

            if (status < 0xF0) {
                ptrdiff_t need = (status & 0xE0) == 0xC0 ? 1 : 2;
                if (c.e - c.p < need)
                    return false;
                if ((c.p[0] & 0x80) || (need == 2 && (c.p[1] & 0x80)))
                    continue;
                const uint8_t d1 = c.p[0], d2 = need == 2 ? c.p[1] : 0;
                c.p += need;
                const uint8_t type = status & 0xF0;
                if (type == 0x80 || type == 0x90) {
                    ++st.total;
                    if (type == 0x80)
                        ++st.offs;
                    ++st.hist[d1];
                    st.sumPitch += d1;
                    st.minPitch = std::min<int>(st.minPitch, d1);
                    st.maxPitch = std::max<int>(st.maxPitch, d1);
                    st.firstTick = std::min(st.firstTick, c.tick);
                    st.lastTick = std::max(st.lastTick, c.tick);
                    if (type == 0x90 && d2) {
                        ++st.chans[status & 0x0F];
                        ++st.velN;
                        st.velSum += d2;
                        st.velSq += double(d2) * d2;
                    }
                }
                else if (type == 0xC0) {
                    info.program = d1;
                    continue;
                }
                else if (!(type == 0xB0 && d1 == 64)) {
                    continue;
                }
                c.status = status;
                c.d1 = d1;
                c.d2 = d2;
                return true;
            }

            if (status == 0xFF) {
                if (c.p >= c.e)
                    return false;
                const uint8_t type = *c.p++;
                uint32_t len;
                if (!vlq(c.p, c.e, len))
                    return false;
                const size_t take = std::min<size_t>(len, static_cast<size_t>(c.e - c.p));
                const uint8_t* m = c.p;
                c.p += take;
                if (type == 0x2F)
                    return false;
                if (type == 0x03 && info.name.empty()) {
                    info.name.assign(reinterpret_cast<const char*>(m), take);
                }
                else if (type == 0x58 && take >= 4 && m[1] < 8 && c.tick < meta.tsTick) {
                    meta.tsTick = c.tick;
                    meta.tsNum = m[0];
                    meta.tsDen = 1 << m[1]; // WHO THE HELL ENCODES DENOMINATORS AS POWERS OF 2???
                }
                else if (type == 0x51 && take >= 3) {
                    c.status = 0xFF;
                    c.tempo = (uint32_t(m[0]) << 16) | (uint32_t(m[1]) << 8) | m[2];
                    return true;
                }
                continue;
            }

            if (status == 0xF0 || status == 0xF7) {
                // SYSEX: THE BLACK HOLE WHERE DEBUGGING TOOLS GO TO DIE
                uint32_t len;
                if (!vlq(c.p, c.e, len))
                    return false;
                c.p += std::min<size_t>(len, static_cast<size_t>(c.e - c.p));
                continue;
            }

            static constexpr uint8_t extra[16] = {
                0, 1, 2, 1, 0, 0,
                0, // Tune Request - REQUEST DENIED, KEYBOARD STILL OUT OF TUNE
                0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            c.p += std::min<ptrdiff_t>(extra[status & 0x0F], c.e - c.p);
        }
        return false;
    }

    double drum_confidence(const SongTrack& info, const Stat& st) {
        double confidence = 0.0;
        if (!info.name.empty()) {
            std::string lower = info.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            static const char* const keywords[] = {
                "drum", "drums", "ezdrummer", "addictive drums", "superior drummer",
                "bfd", "drum kit", "drumkit", "percussion"
            };
            for (const char* k : keywords)
                if (lower.find(k) != std::string::npos) {
                    confidence += 1.0;
                    break;
                }
        }
        if (!st.total)
            return confidence;
        const double total = static_cast<double>(st.total);
        double offRatio = st.offs / total;
        confidence += offRatio < 0.1 ? 0.1 : (offRatio > 0.3 ? -0.1 : 0.0);
        int range = st.maxPitch - st.minPitch;
        confidence += range < 20 ? 0.3 : (range < 30 ? 0.2 : 0.0);
        double dominant = *std::max_element(st.hist.begin(), st.hist.end()) / total;
        confidence += dominant > 0.7 ? 0.2 : (dominant > 0.5 ? 0.1 : 0.0);
        double avgPitch = st.sumPitch / total;
        if (avgPitch >= 35 && avgPitch <= 81)
            confidence += 0.1;
        int used = 0, last = -1;
        for (int ch = 0; ch < 16; ++ch)
            if (st.chans[ch]) {
                ++used;
                last = ch;
            }
        if (used == 1 && last == 9)
            confidence += 0.5;
        if (st.lastTick > st.firstTick) {
            double density = total / (st.lastTick - st.firstTick + 1.0);
            if (density > 0.03) confidence += 0.05;
            if (density > 0.05) confidence += 0.05;
            if (density > 0.1) confidence += 0.05;
        }
        if (st.velN) {
            double mean = st.velSum / st.velN;
            double var = st.velSq / st.velN - mean * mean;
            if (std::sqrt(std::max(0.0, var)) < 10)
                confidence += 0.05;
        }
        return confidence;
    }

    void humanize(Song& song, const midi::LegitModeSettings& legit, midi::NoteHandlingMode pairing) {
        auto& t = song.t;
        auto& ev = song.ev;
        const size_t n = t.size();
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_real_distribution<double> u01(0.0, 1.0);
        const double var = std::clamp(legit.TIMING_VARIATION, 0.0, 1.0);
        const double pause = std::clamp(legit.EXTRA_DELAY_CHANCE, 0.0, 1.0);
        const double skip = std::clamp(legit.NOTE_SKIP_CHANCE, 0.0, 1.0);
        const double pmin = std::max(0.0, std::min(legit.EXTRA_DELAY_MIN, legit.EXTRA_DELAY_MAX));
        const double pmax = std::max(legit.EXTRA_DELAY_MIN, legit.EXTRA_DELAY_MAX);

        int64_t prevOld = 0, prevNew = 0;
        for (size_t i = 0; i < n;) {
            const int64_t t0 = t[i];
            size_t j = i;
            bool note = false;
            for (; j < n && t[j] == t0; ++j)
                note |= Song::kind(ev[j]) == Song::On;
            double f = 1.0 + (u01(rng) * 2.0 - 1.0) * var;
            int64_t nt = prevNew + static_cast<int64_t>(static_cast<double>(t0 - prevOld) * f);
            if (note && pause > 0.0 && u01(rng) < pause)
                nt += static_cast<int64_t>((pmin + u01(rng) * (pmax - pmin)) * 1e9);
            nt = std::max(nt, prevNew);
            for (size_t k = i; k < j; ++k)
                t[k] = nt;
            prevOld = t0;
            prevNew = nt;
            i = j;
        }

        if (skip <= 0.0)
            return;
        struct Chain {
            uint32_t head = NONE;
            uint32_t tail = NONE;
        };
        const bool lifo = pairing == midi::NoteHandlingMode::LIFO;
        std::vector<Chain> chains(song.tracks.size() * 128);
        std::vector<uint32_t> link(n, NONE);
        std::vector<uint8_t> dead(n, 0);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t e = ev[i];
            const uint32_t k = Song::kind(e);
            if (k == Song::Pedal)
                continue;
            Chain& c = chains[static_cast<size_t>(Song::track(e)) * 128 + Song::note(e)];
            if (k == Song::On) {
                if (u01(rng) < skip) {
                    dead[i] = 1;
                    ++song.skipped;
                }
                if (lifo) {
                    link[i] = c.head;
                    c.head = i;
                }
                else if (c.tail == NONE) {
                    c.head = c.tail = i;
                }
                else {
                    link[c.tail] = i;
                    c.tail = i;
                }
                continue;
            }
            const uint32_t on = c.head;
            if (on == NONE) {
                dead[i] = 1;
                continue;
            }
            c.head = link[on];
            if (!lifo && c.head == NONE)
                c.tail = NONE;
            if (dead[on])
                dead[i] = 1;
        }
        size_t w = 0;
        for (size_t i = 0; i < n; ++i)
            if (!dead[i]) {
                t[w] = t[i];
                ev[w] = ev[i];
                ++w;
            }
        t.resize(w);
        ev.resize(w);
    }

}

const char* gm_name(int program) {
    return program >= 0 && program < 128 ? kGm[program] : "Unknown";
}

size_t Song::index_at(int64_t ns) const {
    return static_cast<size_t>(std::lower_bound(t.begin(), t.end(), ns) - t.begin());
}

double Song::bpm_at(int64_t ns) const {
    auto it = std::upper_bound(tempos.begin(), tempos.end(), ns, [](int64_t v, const auto& p) { return v < p.first; });
    return it == tempos.begin() ? bpm : std::prev(it)->second;
}

void Song::notes_and_durations(std::vector<int>& noteList, std::vector<double>& durations, size_t limit) const {
    noteList.clear();
    durations.clear();
    const uint64_t stride = std::max<uint64_t>(1, notes / std::max<size_t>(1, limit));
    std::unordered_map<uint32_t, int64_t> open;
    uint64_t ordinal = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        const uint32_t e = ev[i];
        const uint32_t k = Song::kind(e);
        if (k == Pedal)
            continue;
        const uint32_t key = (static_cast<uint32_t>(Song::track(e)) << 7) | Song::note(e);
        if (k == On) {
            if (ordinal++ % stride == 0)
                open[key] = t[i];
            continue;
        }
        auto it = open.find(key);
        if (it == open.end())
            continue;
        noteList.push_back(Song::note(e));
        durations.push_back((t[i] - it->second) / 1e9);
        open.erase(it);
    }
}

// Main parse() method - WHERE SANITY GOES TO DIE
std::shared_ptr<Song> load_song(const std::filesystem::path& path, const LoadOptions& opt,
    std::atomic<int>* progress, const std::atomic<bool>* cancel) {
    std::vector<uint8_t> data = read_file(path);
    const uint8_t* d = data.data();
    const size_t n = data.size();
    size_t pos = 0, end = n;

    if (n >= 12 && !std::memcmp(d, "RIFF", 4) && !std::memcmp(d + 8, "RMID", 4)) {
        bool found = false;
        for (size_t p = 12; p + 8 <= n;) {
            size_t clen = le32(d + p + 4);
            if (!std::memcmp(d + p, "data", 4)) {
                pos = p + 8;
                end = std::min(n, pos + clen);
                found = true;
                break;
            }
            p += 8 + clen + (clen & 1);
        }
        if (!found)
            throw std::runtime_error("Invalid MIDI file: RIFF file without MIDI data");
    }
    if (end - pos < 14 || std::memcmp(d + pos, "MThd", 4))
        throw std::runtime_error("Invalid MIDI file: Missing MThd header");
    const uint32_t hlen = be32(d + pos + 4);
    if (hlen < 6 || hlen > end - pos - 8)
        throw std::runtime_error("Invalid MIDI header length");

    auto song = std::make_shared<Song>();
    song->path = path;
    song->fileBytes = n;
    song->format = be16(d + pos + 8);
    const uint16_t division = be16(d + pos + 12);
    song->division = division;
    if (!division)
        throw std::runtime_error("Invalid MIDI time division: 0");
    const bool smpte = (division & 0x8000) != 0;
    double nsPerTick;
    if (smpte) {
        int fps = -static_cast<int8_t>(division >> 8);
        if ((fps != 24 && fps != 25 && fps != 29 && fps != 30) || !(division & 0xFF))
            throw std::runtime_error("Invalid SMPTE time division");
        nsPerTick = 1e9 / ((fps == 29 ? 29.97 : fps) * (division & 0xFF));
    }
    else {
        nsPerTick = 500000.0 * 1000.0 / division;
    }
    pos += 8 + hlen;

    std::vector<std::pair<size_t, size_t>> chunks;
    size_t trackBytes = 0;
    while (end - pos >= 8) {
        size_t clen = be32(d + pos + 4);
        size_t body = pos + 8;
        size_t avail = std::min(clen, end - body);
        if (!std::memcmp(d + pos, "MTrk", 4)) {
            chunks.push_back({ body, avail });
            trackBytes += avail;
        }
        if (clen > end - body)
            break;
        pos = body + clen;
    }
    if (chunks.empty())
        throw std::runtime_error("Invalid MIDI file: no tracks");

    const size_t nt = chunks.size();
    song->tracks.resize(nt);
    std::vector<Stat> stats(nt);
    std::vector<Cursor> cur(nt);
    Meta meta;
    // will never be enough for the fucking rush e players
    song->t.reserve(trackBytes / 4);
    song->ev.reserve(trackBytes / 4);

    std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> heap;
    for (size_t i = 0; i < nt; ++i) {
        cur[i].p = d + chunks[i].first;
        cur[i].e = cur[i].p + chunks[i].second;
        if (next(cur[i], song->tracks[i], stats[i], meta))
            heap.push((uint64_t(cur[i].tick) << 32) | i);
    }

    uint32_t baseTick = 0;
    double baseNs = 0.0;
    bool firstTempo = true;
    size_t consumed = 0, counter = 0;

    while (!heap.empty()) {
        const uint64_t top = heap.top();
        heap.pop();
        const uint32_t ti = static_cast<uint32_t>(top & 0xFFFFFFFF);
        Cursor& c = cur[ti];
        const double at = baseNs + static_cast<double>(c.tick - baseTick) * nsPerTick;

        if (c.status == 0xFF) {
            if (c.tempo && !smpte) {
                baseNs = at;
                baseTick = c.tick;
                nsPerTick = c.tempo * 1000.0 / division;
                song->tempos.emplace_back(std::llround(at), 60000000.0 / c.tempo);
                if (firstTempo) {
                    song->bpm = 60000000.0 / c.tempo;
                    firstTempo = false;
                }
                ++song->tempoChanges;
            }
        }
        else {
            const uint8_t type = c.status & 0xF0;
            const uint32_t track = std::min<uint32_t>(ti, 0xFFFF);
            uint32_t kind = type == 0xB0 ? Song::Pedal : (type == 0x90 && c.d2) ? Song::On : Song::Off;
            song->t.push_back(std::llround(at));
            song->ev.push_back(Song::pack(kind, track, kind == Song::Pedal ? 64 : c.d1, c.d2));
            if (kind == Song::On) {
                ++song->notes;
                ++song->tracks[ti].notes;
            }
        }

        const uint8_t* before = c.p;
        if (next(c, song->tracks[ti], stats[ti], meta))
            heap.push((uint64_t(c.tick) << 32) | ti);
        consumed += static_cast<size_t>(c.p - before);
        if ((++counter & 0xFFFF) == 0) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return nullptr;
            if (progress)
                progress->store(static_cast<int>(consumed * 90 / std::max<size_t>(1, trackBytes)), std::memory_order_relaxed);
        }
    }
    data.clear();
    data.shrink_to_fit();

    if (song->tempoChanges)
        --song->tempoChanges;
    song->tsNum = meta.tsNum;
    song->tsDen = meta.tsDen;

    if (opt.legit.ENABLED)
        humanize(*song, opt.legit, opt.pairing);
    song->notes -= song->skipped;
    song->length = song->t.empty() ? 0 : song->t.back();

    if (song->t.size() <= 4'000'000) {
        const size_t count = song->t.size();
        song->len.assign(count, 0);
        std::vector<uint32_t> head(song->tracks.size() * 128, NONE), tail(head.size(), NONE), link(count, NONE);
        auto ms = [](int64_t ns) { return static_cast<uint32_t>(std::clamp<int64_t>(ns / 1'000'000, 0, UINT32_MAX)); };
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t e = song->ev[i];
            const uint32_t k = Song::kind(e);
            if (k == Song::Pedal)
                continue;
            const size_t slot = static_cast<size_t>(Song::track(e)) * 128 + static_cast<size_t>(Song::note(e));
            if (k == Song::On) {
                if (tail[slot] == NONE)
                    head[slot] = i;
                else
                    link[tail[slot]] = i;
                tail[slot] = i;
            }
            else if (head[slot] != NONE) {
                const uint32_t on = head[slot];
                head[slot] = link[on];
                if (head[slot] == NONE)
                    tail[slot] = NONE;
                song->len[on] = ms(song->t[i] - song->t[on]);
            }
        }
        for (size_t slot = 0; slot < head.size(); ++slot)
            for (uint32_t on = head[slot]; on != NONE; on = link[on])
                song->len[on] = ms(song->length - song->t[on]);
    }

    {
        constexpr int64_t window = 100'000'000;
        size_t lo = 0;
        uint64_t inWindow = 0, best = 0;
        for (size_t i = 0; i < song->t.size(); ++i) {
            if (Song::kind(song->ev[i]) != Song::On)
                continue;
            ++inWindow;
            while (song->t[i] - song->t[lo] > window) {
                if (Song::kind(song->ev[lo]) == Song::On)
                    --inWindow;
                ++lo;
            }
            best = std::max(best, inWindow);
        }
        song->peakNotesPerSec = static_cast<double>(best) * 1e9 / window;
    }

    for (size_t i = 0; i < nt; ++i) {
        SongTrack& info = song->tracks[i];
        const Stat& st = stats[i];
        if (info.name.empty())
            info.name = "Track " + std::to_string(i + 1);
        uint64_t best = 0;
        for (int ch = 0; ch < 16; ++ch)
            if (st.chans[ch] > best) {
                best = st.chans[ch];
                info.channel = ch;
            }
        info.instrument = info.program >= 0 ? gm_name(info.program) : info.channel == 9 ? "Drum Kit" : gm_name(0);
        if (opt.detectDrums && info.notes)
            info.drums = std::min(drum_confidence(info, st), 1.0) >= 0.8;
    }
    if (progress)
        progress->store(100, std::memory_order_relaxed);
    return song; // HERE'S YOUR MIDI FILE. I HOPE IT WAS WORTH THE TRAUMA
}
