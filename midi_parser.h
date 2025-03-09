#pragma once
#define NOMINMAX
#include "midi_structures.h"
#include <fstream>
#include <string>
#include <cstdint>
#include <windows.h>
class MidiParser {
public:
    // Resets the internal file stream.
    void reset();

    // Parses a MIDI file and returns a MidiFile object.
    [[nodiscard]] MidiFile parse(const std::string& filename);

private:
    mutable std::ifstream file;

    // Byte–swapping helper functions.
    static constexpr uint32_t swapUint32(uint32_t value) noexcept;
    static constexpr uint16_t swapUint16(uint16_t value) noexcept;

    // Functions to read multi–byte integers from the file stream.
    [[nodiscard]] bool readInt32(uint32_t& value);
    [[nodiscard]] bool readInt16(uint16_t& value);
    [[nodiscard]] bool readChunk(char* buffer, size_t size);

    // Updated meta event parser that works on an in–memory track buffer.
    // 'ptr' is updated to point past the parsed meta event.
    void parseMetaEvent(MidiEvent& event,
        MidiFile& midiFile,
        uint32_t absoluteTick,
        const char* trackEnd,
        const char*& ptr);
};
