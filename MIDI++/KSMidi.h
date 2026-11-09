/*
    KSMidi Public API - KSMidi.h
    Author: Mohamed Maatallah
    Date: June 28, 2025 (1.1.0: August 2026)
    Version: 1.1.0

    This is the public-facing C++ interface for the KSMidi library.

    MIT License

    Copyright (c) 2025 Mohamed Maatallah

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ksmidi {
    namespace detail {
        struct TestAccess;
    }

    /**
     * @class KsMidiError
     * @brief Exception class for all KSMidi library errors.
     * @details Inherits from std::runtime_error and includes the Windows HRESULT
     * code for detailed diagnostics.
     */
    class KsMidiError : public std::runtime_error {
    public:
        KsMidiError(std::string_view what = "Unknown KSMidi Error", HRESULT code = 0);

        /// @brief Gets the Windows HRESULT error code associated with the exception.
        HRESULT code() const noexcept;

    private:
        HRESULT code_;
    };

    /**
     * @struct MidiMessage
     * @brief Represents a single MIDI 1.0 message or a chunk of a SysEx message.
     */
    struct MidiMessage {
        double timestamp = 0.0;
        std::vector<BYTE> bytes;
        std::string source;
        bool isSysExChunk = false;
    };

    /**
     * @struct DeviceInfo
     * @brief Contains detailed information about a MIDI device port.
     */
    struct DeviceInfo {
        unsigned int id = 0;
        std::string name;
        std::wstring path;
        DWORD pinId = 0;
        bool isAvailable = true;
        bool supportsMidi2 = false;
        std::string windowsName;
        bool sharedAvailable = false;
        std::wstring serviceEndpointId;
        std::string serviceEndpointName;
        int serviceGroup = -1;
    };

    /**
     * @enum Transport
     * @brief How MidiIn and MidiOut reach a port.
     */
    enum class Transport {
        Direct,
        Shared,
        Auto
    };

    std::string toUtf8(std::wstring_view text);

    namespace ump {
        /**
         * @enum MessageType
         * @brief Defines the Message Type (MT) for a Universal MIDI Packet.
         */
        enum MessageType : uint8_t {
            UTILITY = 0x0,
            SYSTEM = 0x1,
            MIDI1_CHANNEL_VOICE = 0x2,
            DATA_64 = 0x3,
            MIDI2_CHANNEL_VOICE = 0x4,
            DATA_128 = 0x5,
            RESERVED_32_A = 0x6,
            RESERVED_32_B = 0x7,
            RESERVED_64_A = 0x8,
            RESERVED_64_B = 0x9,
            RESERVED_64_C = 0xA,
            RESERVED_96_A = 0xB,
            RESERVED_96_B = 0xC,
            FLEX_DATA = 0xD,
            RESERVED_128_A = 0xE,
            UMP_STREAM = 0xF,

            DATA_MESSAGES_32 [[deprecated("MT 0x3 is a 64-bit Data message: use DATA_64")]] = 0x3,
            DATA_MESSAGES_128 [[deprecated("use DATA_128")]] = 0x5,
            STREAM_MESSAGES_128 [[deprecated("MT 0xD is Flex Data (use FLEX_DATA); UMP Stream is 0xF (UMP_STREAM)")]] = 0xD,
            RESERVED_128_B [[deprecated("MT 0xF is not reserved: use UMP_STREAM")]] = 0xF
        };

        constexpr MessageType SYSEX7_DATA = DATA_64;
        constexpr MessageType SYSEX8_DATA = DATA_128;

        constexpr uint8_t getPacketSizeInWords(uint8_t messageType) noexcept {
            constexpr uint8_t kWords[16] = { 1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4 };
            return kWords[messageType & 0x0F];
        }

        /**
         * @struct UmpMessage
         * @brief Represents a single, complete Universal MIDI Packet (UMP).
         * @details The size can be 32, 64, 96, or 128 bits.
         */
        struct UmpMessage {
            double timestamp = 0.0;
            std::array<uint32_t, 4> words{};
            uint8_t size_in_words = 0;  // Can be 1, 2, 3, or 4
            std::string source;
        };

        // --- UMP Helper Functions ---
        inline MessageType getMessageType(const UmpMessage& msg) {
            return static_cast<MessageType>((msg.words[0] >> 28) & 0x0F);
        }
        inline uint8_t getGroup(const UmpMessage& msg) {
            return (msg.words[0] >> 24) & 0x0F;
        }
        inline uint8_t getStatus(const UmpMessage& msg) {
            return (msg.words[0] >> 16) & 0xFF;
        }
        inline uint8_t getMidi1Channel(const UmpMessage& msg) {
            return (msg.words[0] >> 16) & 0x0F;
        }
        inline uint8_t getMidi1Status(const UmpMessage& msg) {
            return (msg.words[0] >> 20) & 0x0F;
        }
        inline uint8_t getMidi2Channel(const UmpMessage& msg) {
            return (msg.words[0] >> 16) & 0x0F;
        }
        inline uint8_t getMidi2Status(const UmpMessage& msg) {
            return (msg.words[0] >> 20) & 0x0F;
        }
        inline uint8_t getNoteNumber(const UmpMessage& msg) {
            return (msg.words[0] >> 8) & 0x7F;
        }
        inline uint8_t getMidi1Velocity(const UmpMessage& msg) {
            return msg.words[0] & 0x7F;
        }
        inline uint16_t getMidi2Velocity(const UmpMessage& msg) {
            return (msg.words[1] >> 16) & 0xFFFF;
        }
        inline uint8_t getMidi1Data1(const UmpMessage& msg) {
            return (msg.words[0] >> 8) & 0x7F;
        }
        inline uint8_t getMidi1Data2(const UmpMessage& msg) {
            return msg.words[0] & 0x7F;
        }
        inline uint32_t getMidi2Data(const UmpMessage& msg) { return msg.words[1]; }
        inline uint8_t getAttributeType(const UmpMessage& msg) {
            return msg.words[0] & 0xFF;
        }
        inline uint16_t getAttributeData(const UmpMessage& msg) {
            return msg.words[1] & 0xFFFF;
        }

        // --- UMP Message Creation Functions ---
        inline UmpMessage makeUtilityMessage(uint8_t group, uint8_t status,
            uint16_t data = 0) {
            UmpMessage m{};
            m.size_in_words = 1;
            m.words[0] = (uint32_t(MessageType::UTILITY) << 28) |
                (uint32_t(group & 0xF) << 24) | (uint32_t(status) << 16) | data;
            return m;
        }
        inline UmpMessage makeMidi1NoteOn(uint8_t group, uint8_t channel,
            uint8_t noteNumber, uint8_t velocity) {
            UmpMessage m{};
            m.size_in_words = 1;
            m.words[0] = (uint32_t(MessageType::MIDI1_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0x9 << 20) |
                (uint32_t(channel & 0xF) << 16) |
                (uint32_t(noteNumber & 0x7F) << 8) | (velocity & 0x7F);
            return m;
        }
        inline UmpMessage makeMidi1NoteOff(uint8_t group, uint8_t channel,
            uint8_t noteNumber, uint8_t velocity = 0) {
            UmpMessage m{};
            m.size_in_words = 1;
            m.words[0] = (uint32_t(MessageType::MIDI1_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0x8 << 20) |
                (uint32_t(channel & 0xF) << 16) |
                (uint32_t(noteNumber & 0x7F) << 8) | (velocity & 0x7F);
            return m;
        }
        inline UmpMessage makeNoteOn(uint8_t group, uint8_t channel, uint8_t noteNumber,
            uint16_t velocity, uint8_t attributeType = 0,
            uint16_t attributeData = 0) {
            UmpMessage m{};
            m.size_in_words = 2;
            m.words[0] = (uint32_t(MessageType::MIDI2_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0x9 << 20) |
                (uint32_t(channel & 0xF) << 16) |
                (uint32_t(noteNumber & 0x7F) << 8) | attributeType;
            m.words[1] = (uint32_t(velocity) << 16) | attributeData;
            return m;
        }
        inline UmpMessage makeNoteOff(uint8_t group, uint8_t channel,
            uint8_t noteNumber, uint16_t velocity = 0,
            uint8_t attributeType = 0,
            uint16_t attributeData = 0) {
            UmpMessage m{};
            m.size_in_words = 2;
            m.words[0] = (uint32_t(MessageType::MIDI2_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0x8 << 20) |
                (uint32_t(channel & 0xF) << 16) |
                (uint32_t(noteNumber & 0x7F) << 8) | attributeType;
            m.words[1] = (uint32_t(velocity) << 16) | attributeData;
            return m;
        }
        inline UmpMessage makeControlChange(uint8_t group, uint8_t channel,
            uint8_t controller, uint32_t value) {
            UmpMessage m{};
            m.size_in_words = 2;
            m.words[0] = (uint32_t(MessageType::MIDI2_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0xB << 20) |
                (uint32_t(channel & 0xF) << 16) |
                (uint32_t(controller & 0x7F) << 8);
            m.words[1] = value;
            return m;
        }
        inline UmpMessage makeProgramChange(uint8_t group, uint8_t channel,
            uint8_t program, bool bankValid = false,
            uint16_t bank = 0) {
            UmpMessage m{};
            m.size_in_words = 2;
            m.words[0] = (uint32_t(MessageType::MIDI2_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0xC << 20) |
                (uint32_t(channel & 0xF) << 16) | (bankValid ? 0x01 : 0x00);
            const uint32_t bankMsb = bankValid ? ((uint32_t(bank) >> 7) & 0x7F) : 0;
            const uint32_t bankLsb = bankValid ? (uint32_t(bank) & 0x7F) : 0;
            m.words[1] = (uint32_t(program & 0x7F) << 24) | (bankMsb << 8) | bankLsb;
            return m;
        }
        inline UmpMessage makePitchBend(uint8_t group, uint8_t channel,
            uint32_t value) {
            UmpMessage m{};
            m.size_in_words = 2;
            m.words[0] = (uint32_t(MessageType::MIDI2_CHANNEL_VOICE) << 28) |
                (uint32_t(group & 0xF) << 24) | (0xE << 20) |
                (uint32_t(channel & 0xF) << 16);
            m.words[1] = value;
            return m;
        }
    }  // namespace ump

    /**
     * @class LockFreeSPSCQueue
     * @brief A high-performance, single-producer, single-consumer, lock-free
     * circular queue.
     * @tparam T The type of elements in the queue.
     */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    template <typename T>
    class LockFreeSPSCQueue {
    public:
        explicit LockFreeSPSCQueue(size_t size)
            : size_(checkedSize(size)), slots_(new Slot[size]) {
        }

        ~LockFreeSPSCQueue() noexcept {
            size_t r = readIndex_.load(std::memory_order_relaxed);
            const size_t w = writeIndex_.load(std::memory_order_relaxed);
            while (r != w) {
                element(r)->~T();
                r = (r + 1) & (size_ - 1);
            }
        }

        LockFreeSPSCQueue(const LockFreeSPSCQueue&) = delete;
        LockFreeSPSCQueue& operator=(const LockFreeSPSCQueue&) = delete;

        size_t capacity() const noexcept { return size_ - 1; }

        bool empty() const noexcept {
            return readIndex_.load(std::memory_order_relaxed) ==
                writeIndex_.load(std::memory_order_acquire);
        }

        bool try_push(T&& value) noexcept {
            const auto write_idx = writeIndex_.load(std::memory_order_relaxed);
            const auto next_write_idx = (write_idx + 1) & (size_ - 1);
            if (next_write_idx == readIndex_.load(std::memory_order_acquire))
                return false;
            ::new (static_cast<void*>(slots_[write_idx].bytes)) T(std::move(value));
            writeIndex_.store(next_write_idx, std::memory_order_release);
            return true;
        }

        bool try_pop(T& value) noexcept {
            const auto read_idx = readIndex_.load(std::memory_order_relaxed);
            if (read_idx == writeIndex_.load(std::memory_order_acquire)) return false;
            T* e = element(read_idx);
            value = std::move(*e);
            e->~T();
            readIndex_.store((read_idx + 1) & (size_ - 1), std::memory_order_release);
            return true;
        }

        std::optional<T> pop() noexcept {
            const auto read_idx = readIndex_.load(std::memory_order_relaxed);
            if (read_idx == writeIndex_.load(std::memory_order_acquire)) return std::nullopt;
            T* e = element(read_idx);
            std::optional<T> out(std::move(*e));
            e->~T();
            readIndex_.store((read_idx + 1) & (size_ - 1), std::memory_order_release);
            return out;
        }

    private:
        struct alignas(T) Slot {
            unsigned char bytes[sizeof(T)];
        };

        static size_t checkedSize(size_t size) {
            if (!size || (size & (size - 1)) != 0) {
                throw std::invalid_argument(
                    "LockFreeSPSCQueue size must be a power of two.");
            }
            return size;
        }
        T* element(size_t index) noexcept {
            return std::launder(reinterpret_cast<T*>(slots_[index].bytes));
        }

        const size_t size_;
        std::unique_ptr<Slot[]> slots_;

        alignas(std::hardware_destructive_interference_size)
            std::atomic<size_t> writeIndex_{ 0 };
        alignas(std::hardware_destructive_interference_size)
            std::atomic<size_t> readIndex_{ 0 };
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    /**
     * @class Api
     * @brief A static-only class for querying available MIDI ports.
     */
    class Api {
    public:
        virtual ~Api() = default;
        static unsigned int getPortCountIn();
        static unsigned int getPortCountOut();
        static DeviceInfo getPortInfoIn(unsigned int portNumber);
        static DeviceInfo getPortInfoOut(unsigned int portNumber);
        static std::vector<DeviceInfo> getPortsIn();
        static std::vector<DeviceInfo> getPortsOut();
    };

    /**
     * @class MidiIn
     * @brief A class for receiving MIDI messages from an input port.
     */
    class MidiIn : public Api {
    public:
        using MessageCallback = std::function<void(const MidiMessage& message)>;
        using UmpCallback = std::function<void(const ump::UmpMessage& message)>;
        using ErrorCallback = std::function<void(const KsMidiError& error)>;
        using DirectMessageCallback = void (*)(const BYTE* data, size_t size,
            double timestamp, void* userData);

        enum class TimestampMode {
            None,   ///< No timestamping. Message timestamp is always 0.0.

            QPC,    ///< (Default) Timestamped via QueryPerformanceCounter 

            Driver  ///< The driver's own time stamp of each event.
        };

        enum class WaitMode {
            Block,
            SpinThenBlock,
            Spin
        };

        struct Settings {
            DWORD bufferSize = 512;  
            unsigned int bufferCount = 4;
            size_t sysexChunkSize = 1024;
            bool ignoreSysex = true;
            bool ignoreTime = true;
            bool ignoreSense = true;
            TimestampMode timestampMode = TimestampMode::QPC;
            size_t messageQueueSize = 256;
            size_t umpMessageQueueSize = 256;
            size_t errorQueueSize = 16;
            unsigned int eventAlignment = 4;
            WaitMode waitMode = WaitMode::Block;
            unsigned int spinMicroseconds = 1000;
            bool preferPerformanceCores = false;
            bool disablePowerThrottling = false;
            bool useMmcss = true;
            std::wstring mmcssTaskName = L"Pro Audio";
            Transport transport = Transport::Direct;
        };

        struct MmcssStatus {
            bool requested = false;
            bool readerRegistered = false;
            bool callbackRegistered = false;
            DWORD lastError = 0;
        };

        MidiIn();
        ~MidiIn() noexcept;
        MidiIn(const MidiIn&) = delete;
        MidiIn& operator=(const MidiIn&) = delete;
        MidiIn(MidiIn&&) noexcept;
        MidiIn& operator=(MidiIn&&) noexcept;

        void openPort(unsigned int portNumber);
        void openPort(unsigned int portNumber, const Settings& settings);
        void closePort();
        bool isPortOpen() const noexcept;
        bool isUmpStream() const noexcept;
        Transport activeTransport() const noexcept;
        const Settings& getSettings() const noexcept;
        MmcssStatus getMmcssStatus() const noexcept;

        // --- MIDI 1.0 Byte Stream API ---
        bool try_pop_message(MidiMessage& message) noexcept;
        std::optional<MidiMessage> pop_message() noexcept;
        void setCallback(MessageCallback callback);
        void cancelCallback();

        // --- MIDI 2.0 Universal MIDI Packet API ---
        bool try_pop_ump_message(ump::UmpMessage& message) noexcept;
        std::optional<ump::UmpMessage> pop_ump_message() noexcept;
        void setUmpCallback(UmpCallback callback);
        void cancelUmpCallback();

        // --- Direct Callback API ---
        /**
         * @brief Sets a direct, raw data callback for the lowest possible latency.
         * @warning The callback is executed on KSMidi's reader thread. It MUST be
         * non-blocking, lock-free, and return as quickly as possible.
         * @param callback A function pointer to your callback.
         * @param userData A pointer to your custom data, passed to the callback.
         */
        void setDirectCallback(DirectMessageCallback callback,
            void* userData = nullptr);
        void cancelDirectCallback();

        // --- Common API ---
        bool try_pop_error(KsMidiError& error) noexcept;
        std::optional<KsMidiError> pop_error() noexcept;
        void setErrorCallback(ErrorCallback callback);

        /**
         * @brief Configures which MIDI message types to ignore, like RtMidi's ignoreTypes().
         * @note This method must be called BEFORE opening the port.
         */
        void ignoreTypes(bool midiSysex = true, bool midiTime = true,
            bool midiSense = true);

    private:
        friend struct detail::TestAccess;
        class MidiInImplBase;
        template <TimestampMode TMode>
        class MidiInImpl;
        class MidiInSharedImpl;
        void openPortWith(const Settings& settings,
            const std::function<void(MidiInImplBase&)>& open);
        void openShared(unsigned int portNumber, const Settings& settings);
        void applyCallbacks();
        std::unique_ptr<MidiInImplBase> pimpl_;
        Settings settings_{};
        std::shared_ptr<MessageCallback> callback_;
        std::shared_ptr<UmpCallback> umpCallback_;
        std::shared_ptr<ErrorCallback> errorCallback_;
        DirectMessageCallback directCallback_ = nullptr;
        void* directUserData_ = nullptr;
    };

    /**
     * @class MidiOut
     * @brief A class for sending MIDI messages to an output port.
     */
    class MidiOut : public Api {
    public:
        MidiOut();
        ~MidiOut() noexcept;
        MidiOut(const MidiOut&) = delete;
        MidiOut& operator=(const MidiOut&) = delete;
        MidiOut(MidiOut&&) noexcept;
        MidiOut& operator=(MidiOut&&) noexcept;

        void openPort(unsigned int portNumber);
        void openPort(unsigned int portNumber, Transport transport);
        void closePort();
        bool isPortOpen() const noexcept;
        bool isUmpStream() const noexcept;
        Transport activeTransport() const noexcept;

        // MIDI 1.0
        void sendMessage(const std::vector<BYTE>& message);
        void sendMessage(const BYTE* message, size_t size);
        void sendMessage(std::initializer_list<BYTE> message);

        // MIDI 2.0
        void sendMessage(const ump::UmpMessage& message);

    private:
        friend struct detail::TestAccess;
        class MidiOutImpl;
        std::unique_ptr<MidiOutImpl> pimpl_;
    };

}  // namespace ksmidi
