#include "midi_parser.h"
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstring>
#include <climits>
#include <fstream>

// Maximum allowed size for meta and SysEx event data (1 MB here).
constexpr uint32_t MAX_EVENT_LENGTH = 0x100000; // 1 MB

//==========================================================================
// Byte–swap helper functions
//==========================================================================
constexpr uint32_t MidiParser::swapUint32(uint32_t value) noexcept {
    return ((value >> 24) & 0x000000FF) | ((value >> 8) & 0x0000FF00) |
        ((value << 8) & 0x00FF0000) | ((value << 24) & 0xFF000000);
}

constexpr uint16_t MidiParser::swapUint16(uint16_t value) noexcept {
    return (value >> 8) | (value << 8);
}

//==========================================================================
// Methods using the file stream for header reading etc.
//==========================================================================
void MidiParser::reset() {
    if (file.is_open()) {
        file.close();
    }
    file.clear();
}

bool MidiParser::readInt32(uint32_t& value) {
    // READING 4 BYTES OF PURE MIDI MAGIC - OR COMPLETE GARBAGE
    if (!file.read(reinterpret_cast<char*>(&value), 4))
        return false;
    value = swapUint32(value);
    return true;
}

bool MidiParser::readInt16(uint16_t& value) {
    if (!file.read(reinterpret_cast<char*>(&value), 2))
        return false;
    value = swapUint16(value);
    return true;
}

bool MidiParser::readChunk(char* buffer, size_t size) {
    return file.read(buffer, size).good();
}

//==========================================================================
// Helper functions for parsing from a memory buffer
//==========================================================================
namespace {
    // Reads one byte from the buffer; throws if out-of-range.
    inline uint8_t readByte(const char*& ptr, const char* end) {
        if (ptr >= end)
            throw std::runtime_error("Unexpected end of track data while reading a byte");
        return static_cast<uint8_t>(*ptr++);
    }

    // Reads a variable–length quantity from the buffer with a limit on bytes.
    // MIDI'S VARIABLE LENGTH ENCODING: BECAUSE FIXED-SIZE INTEGERS WERE TOO DAMN SIMPLE
    inline void readVarLenFromBuffer(const char*& ptr, const char* end, uint32_t& value) {
        value = 0;
        uint8_t byte;
        int count = 0;
        do {
            if (count++ >= 4)
                throw std::runtime_error("Variable-length quantity exceeds maximum allowed length");
            byte = readByte(ptr, end);
            // Check for overflow before shifting
            if (value > (UINT32_MAX >> 7))
                throw std::runtime_error("Variable-length quantity overflow");
            value = (value << 7) | (byte & 0x7F);
        } while (byte & 0x80);
    }
} // unnamed namespace

bool hasPathTraversal(const std::string& path) {
    // Prevent directory traversal attacks
    size_t i = 0;
    while (i < path.size()) {
        // Skip any leading slashes
        while (i < path.size() && (path[i] == '/' || path[i] == '\\'))
            ++i;
        if (i >= path.size())
            break;
        // Find the next delimiter
        size_t j = i;
        while (j < path.size() && (path[j] != '/' && path[j] != '\\'))
            ++j;
        // Extract the current segment
        std::string segment = path.substr(i, j - i);
        if (segment == "..")
            return true;
        i = j;
    }
    return false;
}


void MidiParser::parseMetaEvent(MidiEvent& event, MidiFile& midiFile, uint32_t absoluteTick,
    const char* trackEnd, const char*& ptr) {
    uint8_t metaType = readByte(ptr, trackEnd);
    uint32_t length = 0;
    readVarLenFromBuffer(ptr, trackEnd, length);
    if (length > MAX_EVENT_LENGTH)
        throw std::runtime_error("Meta event length exceeds maximum allowed value");
    if (static_cast<size_t>(trackEnd - ptr) < length)
        throw std::runtime_error("Meta event data length exceeds track data");

    event.metaData.resize(length);
    std::memcpy(event.metaData.data(), ptr, length);
    ptr += length;

    event.status = 0xFF;
    event.data1 = metaType;

    switch (metaType) {
    case 0x51: { // Tempo event
        if (length == 3) {
            uint32_t microsecondsPerQuarter = (static_cast<uint8_t>(event.metaData[0]) << 16) |
                (static_cast<uint8_t>(event.metaData[1]) << 8) |
                (static_cast<uint8_t>(event.metaData[2]));
            midiFile.tempoChanges.push_back({ absoluteTick, microsecondsPerQuarter });
        }
        break;
    }
    case 0x58: { // Time Signature
        if (length == 4) {
            if (event.metaData[1] >= 8)
                throw std::runtime_error("Invalid time signature denominator");
            midiFile.timeSignatures.push_back({
                absoluteTick,
                static_cast<uint8_t>(event.metaData[0]),
                static_cast<uint8_t>(1 << (event.metaData[1])), // WHO THE HELL ENCODES DENOMINATORS AS POWERS OF 2???
                static_cast<uint8_t>(event.metaData[2]),
                static_cast<uint8_t>(event.metaData[3])
                });
        }
        break;
    }
    case 0x59: { // Key Signature
        if (length == 2) {
            midiFile.keySignatures.push_back({
                absoluteTick,
                static_cast<int8_t>(event.metaData[0]),
                static_cast<uint8_t>(event.metaData[1])
                });
        }
        break;
    }
    default:
        // Other meta events: simply store the data.
        break;
    }
}

//==========================================================================
// Main parse() method - WHERE SANITY GOES TO DIE
//==========================================================================
MidiFile MidiParser::parse(const std::string& filename) {
    reset();
    if (hasPathTraversal(filename))
        throw std::runtime_error("Invalid filename path");
    // Convert the UTF-8 filename to a wide string.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
    if (wlen == 0) {
        throw std::runtime_error("Failed to convert filename to wide string");
    }
    std::vector<wchar_t> wfilename(wlen);
    if (MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, wfilename.data(), wlen) == 0) {
        throw std::runtime_error("Failed to convert filename to wide string");
    }

    // Open file using wide-char path (Visual Studio supports this overload).
    file.open(wfilename.data(), std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file: " + filename);
    }

    MidiFile midiFile;
    char headerChunk[4];
    if (!readChunk(headerChunk, 4) || std::string(headerChunk, 4) != "MThd")
        throw std::runtime_error("Invalid MIDI file: Missing MThd header");

    uint32_t headerLength;
    if (!readInt32(headerLength) || headerLength != 6)
        throw std::runtime_error("Invalid MIDI header length");

    if (!readInt16(midiFile.format) || !readInt16(midiFile.numTracks) || !readInt16(midiFile.division))
        throw std::runtime_error("Error reading MIDI header fields");

    if (midiFile.division == 0)
        throw std::runtime_error("Invalid MIDI time division: 0");

    if (midiFile.format > 2)
        throw std::runtime_error("Invalid MIDI format");

    if (midiFile.numTracks == 0)
        throw std::runtime_error("Invalid number of tracks");

    // Process each track by reading its entire chunk into memory.
    for (int i = 0; i < midiFile.numTracks; ++i) {
        char trackChunk[4];
        if (!readChunk(trackChunk, 4) || std::string(trackChunk, 4) != "MTrk")
            throw std::runtime_error("Invalid MIDI file: Missing MTrk header for track " + std::to_string(i));

        uint32_t trackLength;
        if (!readInt32(trackLength))
            throw std::runtime_error("Error reading track length for track " + std::to_string(i));

        std::vector<char> trackData(trackLength);
        if (!file.read(trackData.data(), trackLength))
            throw std::runtime_error("Error reading track data for track " + std::to_string(i));

        const char* ptr = trackData.data();
        if (trackLength > std::numeric_limits<size_t>::max() - reinterpret_cast<size_t>(ptr))
            throw std::runtime_error("Track length causes pointer arithmetic overflow");
        const char* trackEnd = ptr + trackLength;
        MidiTrack track;
        uint32_t absoluteTick = 0;
        uint8_t lastStatus = 0;
        // Reserve an estimate of events to reduce reallocation overhead.
        // will never be enough for the fucking rush e players
        track.events.reserve(1000);

        while (ptr < trackEnd) {
            uint32_t deltaTime = 0;
            readVarLenFromBuffer(ptr, trackEnd, deltaTime);
            if (UINT32_MAX - absoluteTick < deltaTime)
                throw std::runtime_error("Absolute tick counter overflow");
            absoluteTick += deltaTime;

            uint8_t status = readByte(ptr, trackEnd);
            // Handle running status: if status byte is a data byte (< 0x80)
            if (status < 0x80) {
                if (lastStatus == 0)
                    throw std::runtime_error("Running status encountered with no previous status");
                status = lastStatus;
                ptr--;
            }
            else {
                lastStatus = status;
            }

            MidiEvent event;
            event.absoluteTick = absoluteTick;
            event.status = status;

            // Channel voice messages that use two data bytes:
            if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90 ||
                (status & 0xF0) == 0xA0 || (status & 0xF0) == 0xB0 ||
                (status & 0xF0) == 0xE0) {
                if (static_cast<size_t>(trackEnd - ptr) < 2)
                    throw std::runtime_error("Unexpected end of track data reading channel event");
                event.data1 = readByte(ptr, trackEnd);
                event.data2 = readByte(ptr, trackEnd);
                event.data1 = std::min(event.data1, static_cast<uint8_t>(127));
                event.data2 = std::min(event.data2, static_cast<uint8_t>(127));
                track.events.push_back(std::move(event));
            }
            // Channel voice messages that use one data byte:
            else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) {
                if (static_cast<size_t>(trackEnd - ptr) < 1)
                    throw std::runtime_error("Unexpected end of track data reading channel event (1 data byte)");
                event.data1 = readByte(ptr, trackEnd);
                event.data2 = 0;
                event.data1 = std::min(event.data1, static_cast<uint8_t>(127));
                track.events.push_back(std::move(event));
            }
            // System Exclusive events (F0 and F7)
            // SYSEX: THE BLACK HOLE WHERE DEBUGGING TOOLS GO TO DIE
            else if (status == 0xF0 || status == 0xF7) {
                uint32_t length = 0;
                readVarLenFromBuffer(ptr, trackEnd, length);
                if (length > MAX_EVENT_LENGTH)
                    throw std::runtime_error("SysEx event length exceeds maximum allowed value");
                if (static_cast<size_t>(trackEnd - ptr) < length)
                    throw std::runtime_error("SysEx event length exceeds track data");
                event.metaData.resize(length);
                std::copy_n(ptr, length, event.metaData.begin());
                ptr += length;
                track.events.push_back(std::move(event));
            }
            // Meta events (FF)
            else if (status == 0xFF) {
                parseMetaEvent(event, midiFile, absoluteTick, trackEnd, ptr);
                track.events.push_back(std::move(event));
            }
            // System common and realtime events (F1, F2, F3, F6, F8, FA, FB, FC, FE)
            else if (status >= 0xF0) {
                // Determine data byte count for common system messages.
                uint8_t dataCount = 0;
                switch (status) {
                case 0xF1: dataCount = 1; break; // MIDI Time Code Quarter Frame
                case 0xF2: dataCount = 2; break; // Song Position Pointer
                case 0xF3: dataCount = 1; break; // Song Select
                case 0xF6: dataCount = 0; break; // Tune Request - REQUEST DENIED, KEYBOARD STILL OUT OF TUNE
                    // Real-time messages (F8, FA, FB, FC, FE) have no data bytes.
                case 0xF8:
                case 0xFA:
                case 0xFB:
                case 0xFC:
                case 0xFE:
                    dataCount = 0;
                    break;
                default:
                    dataCount = 0;
                    break;
                }
                // Skip the data bytes (if any) for the unknown system event.
                for (uint8_t j = 0; j < dataCount; ++j) {
                    if (ptr >= trackEnd) break;
                    readByte(ptr, trackEnd);
                }
                continue; // Skip unknown event
            }
            else {
                // (Should not get here.) For safety, skip a byte.
                readByte(ptr, trackEnd);
                continue;
            }
        }
        midiFile.tracks.push_back(std::move(track));
    }
    file.close();
    return midiFile; // HERE'S YOUR MIDI FILE. I HOPE IT WAS WORTH THE TRAUMA
}