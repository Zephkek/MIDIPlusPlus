#pragma once
#define NOMINMAX
#include "midi_structures.h"
#include <fstream>
#include <string>
#include <cstdint>
#include <windows.h>

class MidiParser {
public:
    void reset();
    [[nodiscard]] MidiFile parse(const std::string& filename);
private:
    mutable std::ifstream file;
    static constexpr uint32_t swapUint32(uint32_t value) noexcept;
    static constexpr uint16_t swapUint16(uint16_t value) noexcept;
    [[nodiscard]] bool readInt32(uint32_t& value);
    [[nodiscard]] bool readInt16(uint16_t& value);  // For unsigned fields
    [[nodiscard]] bool readInt16(int16_t& value);   // For signed division
    [[nodiscard]] bool readChunk(char* buffer, size_t size);
    void parseMetaEvent(MidiEvent& event, MidiFile& midiFile, uint32_t absoluteTick,
        const char* trackEnd, const char*& ptr);
};