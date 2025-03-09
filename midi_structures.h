#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct MidiEvent {
    uint32_t absoluteTick = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    std::vector<uint8_t> metaData;
    int trackIndex = 0;  // Initialized to zero

    [[nodiscard]] constexpr uint32_t getAbsoluteTick() const noexcept { return absoluteTick; }
    [[nodiscard]] constexpr uint8_t getStatus() const noexcept { return status; }
    [[nodiscard]] constexpr uint8_t getData1() const noexcept { return data1; }
    [[nodiscard]] constexpr uint8_t getData2() const noexcept { return data2; }
    [[nodiscard]] const std::vector<uint8_t>& getMetaData() const noexcept { return metaData; }

    void setMetaData(std::vector<uint8_t>&& md) noexcept { metaData = std::move(md); }
};

struct TempoChange {
    uint32_t tick;
    uint32_t microsecondsPerQuarter;
};

struct TimeSignature {
    uint32_t tick;
    uint8_t numerator;
    uint8_t denominator;
    uint8_t clocksPerClick;
    uint8_t thirtySecondNotesPerQuarter;
};

struct KeySignature {
    uint32_t tick;
    int8_t key;
    uint8_t scale;
};

struct MidiTrack {
    std::string name;
    std::vector<MidiEvent> events;
};

struct MidiFile {
    uint16_t format = 0;
    uint16_t numTracks = 0;
    uint16_t division = 0; // For PPQ: ticks per quarter note.
    // For SMPTE: MSB set; low byte = ticks per frame, high byte (as signed) = -frames per second
    std::vector<MidiTrack> tracks;
    std::vector<TempoChange> tempoChanges;
    std::vector<TimeSignature> timeSignatures;
    std::vector<KeySignature> keySignatures;
};
