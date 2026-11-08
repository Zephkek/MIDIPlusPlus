/*
    KSMidi Public API - KSMidi.h
    Author: Mohamed Maatallah
    Date: June 28, 2025 (1.1.0: August 2026)
    Version: 1.1.0 (see CHANGELOG.md)

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
        /// Test seam: declared here, defined only by the hardware-free unit tests
        /// (tests/unit), which compile KSMidi.cpp into their own translation unit.
        /// Library users never need it.
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
        std::string name;    ///< UTF-8 friendly name
        std::wstring path;   ///< device interface path (UTF-16); print it with toUtf8()
        DWORD pinId = 0;
        /// The driver allows a pin on a new filter handle. It cannot tell whether other
        /// applications already use the device: the instance count of a pin is per filter handle.
        bool isAvailable = true;
        bool supportsMidi2 = false;
        /// The name Windows gives this port (its WinMM name, through the Windows MIDI service),
        /// or empty if Windows does not list it. It can differ from `name` for some devices.
        std::string windowsName;
        /// The port is also reachable through Windows (Transport::Shared).
        bool sharedAvailable = false;
        /// How Windows MIDI Services presents this port: the MIDI 2.0 endpoint of the device
        /// (interface id and name) and the UMP group of this pin in it, 0-15. Empty and -1 when the
        /// service does not list the device (service off, Legacy API mode, device not handled).
        std::wstring serviceEndpointId;
        std::string serviceEndpointName;
        int serviceGroup = -1;
    };

    /**
     * @enum Transport
     * @brief How MidiIn and MidiOut reach a port.
     */
    enum class Transport {
        /// (Default) KSMidi opens the port's kernel-streaming pin itself: the shortest path to
        /// the driver, with no service process in between.
        Direct,
        /// Through Windows (WinMM, which reaches the device through the Windows MIDI service),
        /// like any other application: the port stays shared with every other client. MIDI 1.0.
        Shared,
        /// Direct when the driver gives KSMidi a pin of its own, Shared otherwise (for example
        /// when another client holds the only pin instance of the device).
        Auto
    };

    /// Converts UTF-16 text (e.g. DeviceInfo::path) to UTF-8, e.g. for std::cout.
    /// (Streaming a wchar_t* into std::cout prints a pointer in C++17 and does not
    /// compile in C++20.)
    std::string toUtf8(std::wstring_view text);

    namespace ump {
        /**
         * @enum MessageType
         * @brief Message Type (MT) of a Universal MIDI Packet: the top 4 bits of word 0.
         * @details Names and sizes follow M2-104-UM "UMP Format and MIDI 2.0 Protocol" v1.1,
         * Table 4 "Message Type (MT) Allocation". See getPacketSizeInWords().
         */
        enum MessageType : uint8_t {
            UTILITY = 0x0,              ///< 32-bit Utility messages
            SYSTEM = 0x1,               ///< 32-bit System Real Time and System Common
            MIDI1_CHANNEL_VOICE = 0x2,  ///< 32-bit MIDI 1.0 Channel Voice
            DATA_64 = 0x3,              ///< 64-bit Data messages, including System Exclusive 7
            MIDI2_CHANNEL_VOICE = 0x4,  ///< 64-bit MIDI 2.0 Channel Voice
            DATA_128 = 0x5,             ///< 128-bit Data messages (System Exclusive 8, Mixed Data Set)
            RESERVED_32_A = 0x6,        ///< reserved, 32-bit
            RESERVED_32_B = 0x7,        ///< reserved, 32-bit
            RESERVED_64_A = 0x8,        ///< reserved, 64-bit
            RESERVED_64_B = 0x9,        ///< reserved, 64-bit
            RESERVED_64_C = 0xA,        ///< reserved, 64-bit
            RESERVED_96_A = 0xB,        ///< reserved, 96-bit
            RESERVED_96_B = 0xC,        ///< reserved, 96-bit
            FLEX_DATA = 0xD,            ///< 128-bit Flex Data messages
            RESERVED_128_A = 0xE,       ///< reserved, 128-bit
            UMP_STREAM = 0xF,           ///< 128-bit UMP Stream messages

            // KSMidi 1.0 names, kept so existing code still compiles:
            DATA_MESSAGES_32 [[deprecated("MT 0x3 is a 64-bit Data message: use DATA_64")]] = 0x3,
            DATA_MESSAGES_128 [[deprecated("use DATA_128")]] = 0x5,
            STREAM_MESSAGES_128 [[deprecated("MT 0xD is Flex Data (use FLEX_DATA); UMP Stream is 0xF (UMP_STREAM)")]] = 0xD,
            RESERVED_128_B [[deprecated("MT 0xF is not reserved: use UMP_STREAM")]] = 0xF
        };

        constexpr MessageType SYSEX7_DATA = DATA_64;
        constexpr MessageType SYSEX8_DATA = DATA_128;

        /// Number of 32-bit words in a UMP of the given Message Type (M2-104-UM v1.1, Table 4):
        /// MT 0x0-0x2, 0x6-0x7: 1; 0x3, 0x4, 0x8-0xA: 2; 0xB-0xC: 3; 0x5, 0xD-0xF: 4.
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
        /// MIDI 2.0 Program Change (M2-104-UM v1.1, 7.4.9).
        /// word 0: MT 0x4 | group | 0xC | channel | reserved | option flags (bit 0 = Bank Valid);
        /// word 1: program [31:24] | reserved [23:16] | bank MSB [15:8] | bank LSB [7:0].
        /// @param bank 14-bit bank number: MSB = bank >> 7, LSB = bank & 0x7F. Used only when
        /// bankValid is true; otherwise both bank fields are zero, as the specification requires.
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
     * @brief Bounded, lock-free single-producer / single-consumer ring buffer (Lamport queue).
     * @tparam T The type of elements in the queue (must be move-constructible; it does not
     * need to be default-constructible).
     * @details
     * - Exactly one thread may call try_push() and exactly one thread may call try_pop() /
     *   pop() at any given time.
     * - `size` must be a power of two. One slot is kept free to tell "full" from "empty", so
     *   the usable capacity is size - 1 (see capacity()); e.g. the default MidiIn queue size
     *   256 holds 255 messages.
     * - Slots are raw, suitably aligned storage. try_push() constructs the element in place,
     *   and every element is destroyed exactly once: by try_pop()/pop(), or by the queue's
     *   destructor if it is still queued then.
     * - The producer publishes an element with a release store of the write index and the
     *   consumer reads it after an acquire load of that index (and symmetrically for the read
     *   index), so an element is fully constructed before the consumer can see it. The two
     *   indices are on separate cache lines to avoid false sharing.
     */
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // padding from alignas(): intended, keeps the indices apart
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

        /// Number of elements the queue can hold (size - 1).
        size_t capacity() const noexcept { return size_ - 1; }

        /// Consumer side: true if no element is queued.
        bool empty() const noexcept {
            return readIndex_.load(std::memory_order_relaxed) ==
                writeIndex_.load(std::memory_order_acquire);
        }

        /// Producer side. Returns false (and leaves `value` untouched) if the queue is full.
        bool try_push(T&& value) noexcept {
            const auto write_idx = writeIndex_.load(std::memory_order_relaxed);
            const auto next_write_idx = (write_idx + 1) & (size_ - 1);
            if (next_write_idx == readIndex_.load(std::memory_order_acquire))
                return false;
            ::new (static_cast<void*>(slots_[write_idx].bytes)) T(std::move(value));
            writeIndex_.store(next_write_idx, std::memory_order_release);
            return true;
        }

        /// Consumer side. Moves the oldest element into `value`; false if the queue is empty.
        bool try_pop(T& value) noexcept {
            const auto read_idx = readIndex_.load(std::memory_order_relaxed);
            if (read_idx == writeIndex_.load(std::memory_order_acquire)) return false;
            T* e = element(read_idx);
            value = std::move(*e);
            e->~T();
            readIndex_.store((read_idx + 1) & (size_ - 1), std::memory_order_release);
            return true;
        }

        /// Consumer side. Returns the oldest element, or std::nullopt if the queue is empty.
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
        std::unique_ptr<Slot[]> slots_;  // raw storage: no element is constructed up front

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
        // Each call below enumerates the KS filters again (opening every filter of the
        // category), and port numbers are positions in the current list.
        static unsigned int getPortCountIn();
        static unsigned int getPortCountOut();
        static DeviceInfo getPortInfoIn(unsigned int portNumber);
        static DeviceInfo getPortInfoOut(unsigned int portNumber);
        /// All input / output ports from ONE enumeration (DeviceInfo::id == index). Prefer it
        /// to getPortCount*() + getPortInfo*(i) for every i, which enumerates n + 1 times.
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

            Driver  ///< The driver's own time stamp of each event: KSMUSICFORMAT::TimeDeltaMs,
                    ///< the milliseconds since the pin started running (1 ms resolution),
                    ///< read the way Windows' own KS transport reads it.
        };

        /// How the input threads wait for data: the reader thread for the driver's read
        /// completions, the callback thread for the reader (see Settings::waitMode).
        enum class WaitMode {
            Block,          ///< (Default) sleep until data arrives. No CPU use while idle; every
                            ///< buffer costs a thread wake-up.
            SpinThenBlock,  ///< after each buffer, poll for Settings::spinMicroseconds, then
                            ///< sleep: data that follows within that time skips the wake-up.
            Spin            ///< poll and never sleep: the lowest latency, but each polling thread
                            ///< keeps one CPU core fully busy while the port is open, and with
                            ///< useMmcss it uses up the MMCSS CPU share of the whole machine.
        };

        struct Settings {
            DWORD bufferSize = 512;  
            unsigned int bufferCount = 4;  ///< overlapped reads kept in flight, 2..63
            size_t sysexChunkSize = 1024;
            bool ignoreSysex = true;   ///< drop SysEx (F0 ... F7), see ignoreTypes()
            bool ignoreTime = true;    ///< drop MTC quarter frame (F1) and timing clock (F8)
            bool ignoreSense = true;   ///< drop active sensing (FE)
            TimestampMode timestampMode = TimestampMode::QPC;
            size_t messageQueueSize = 256;
            size_t umpMessageQueueSize = 256;
            size_t errorQueueSize = 16;
            /// Alignment of the KSMUSICFORMAT records in read buffers. 4 (DWORD) is what
            /// ksmedia.h requires ("MUST be dword aligned") and the default. 8 restores the
            /// record stride of KSMidi 1.0, for a driver that pads every event to 8 bytes.
            unsigned int eventAlignment = 4;
            /// How the reader thread, and the callback thread while a message or UMP callback is
            /// registered, wait for data (see WaitMode). Spin takes the thread wake-up out of the
            /// input latency at the cost of one fully busy CPU core per polling thread. Direct
            /// path only: on the shared path WinMM's own thread delivers the input.
            WaitMode waitMode = WaitMode::Block;
            unsigned int spinMicroseconds = 1000;  ///< polling time of WaitMode::SpinThenBlock
            /// On a CPU with cores of different efficiency classes (Intel performance and
            /// efficiency cores), keep the input threads on the cores of the highest class
            /// (SetThreadSelectedCpuSets). No effect on a CPU with a single class, nor on the
            /// shared path.
            bool preferPerformanceCores = false;
            /// Opt the input threads out of execution speed throttling (EcoQoS: lower clock or
            /// efficiency cores to save power, SetThreadInformation(ThreadPowerThrottling)), also
            /// when the process itself is in EcoQoS (e.g. Task Manager's efficiency mode). No
            /// effect on the shared path.
            bool disablePowerThrottling = false;
            /// Register the reader thread and the callback thread with the Multimedia Class
            /// Scheduler Service (MMCSS, AvSetMmThreadCharacteristicsW) for their lifetime.
            /// "Pro Audio" is in MMCSS scheduling category High (thread priority 23-26).
            /// If registration fails the threads keep running at normal priority; the
            /// failure is reported through getMmcssStatus() and, for the reader thread,
            /// as a KsMidiError in the error queue.
            bool useMmcss = true;
            std::wstring mmcssTaskName = L"Pro Audio";  ///< MMCSS task (registry SystemProfile\Tasks)
            /// How the port is reached (see Transport). With Transport::Shared the messages come
            /// through WinMM: bufferCount and bufferSize then set the System Exclusive buffers,
            /// TimestampMode::Driver gives WinMM's time stamp (milliseconds since the port
            /// started), and eventAlignment and the UMP API do not apply.
            Transport transport = Transport::Direct;
        };

        /// Real-time scheduling state of the input threads (see Settings::useMmcss).
        struct MmcssStatus {
            bool requested = false;           ///< Settings::useMmcss of the open port
            bool readerRegistered = false;    ///< the running reader thread joined the MMCSS task
            bool callbackRegistered = false;  ///< the running callback thread joined the MMCSS task
            DWORD lastError = 0;              ///< GetLastError() of the last failed registration
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
        /// The path the open port uses: Transport::Direct or Transport::Shared (Auto has been
        /// resolved by openPort). Direct when no port is open.
        Transport activeTransport() const noexcept;
        /// The settings openPort(portNumber) uses: those of the last opening, with the changes
        /// made by ignoreTypes() since.
        const Settings& getSettings() const noexcept;
        /// MMCSS registration state of the reader and callback threads of the open port.
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
         * @warning The callback is executed on KSMidi's reader thread (a user-mode thread
         * that waits for the driver's read completions), which runs in the MMCSS "Pro Audio"
         * task by default (Settings::useMmcss). It MUST be non-blocking, lock-free, and
         * return as quickly as possible, otherwise MIDI input is delayed or lost.
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
         * @brief Configures which MIDI 1.0 message types the parser drops. Same semantics
         * as RtMidi's ignoreTypes().
         * @note Call it while the port is closed; it applies at the next openPort().
         * Calling it while the port is open throws KsMidiError (E_ACCESSDENIED).
         * @param midiSysex If true, System Exclusive messages (0xF0 ... 0xF7) are dropped.
         * @param midiTime If true, MIDI Time Code quarter frames (0xF1 xx) and Timing
         * Clock (0xF8) are dropped.
         * @param midiSense If true, Active Sensing (0xFE) is dropped.
         * Every other message is always delivered: channel messages, Song Position
         * (0xF2), Song Select (0xF3), Tune Request (0xF6), Start/Continue/Stop
         * (0xFA/0xFB/0xFC) and System Reset (0xFF). The undefined status bytes 0xF4,
         * 0xF5, 0xF9 and 0xFD are always ignored (MIDI 1.0 Detailed Specification 4.2).
         * The direct callback receives the raw, unfiltered bytes.
         */
        void ignoreTypes(bool midiSysex = true, bool midiTime = true,
            bool midiSense = true);

    private:
        friend struct detail::TestAccess;
        class MidiInImplBase;
        template <TimestampMode TMode>
        class MidiInImpl;
        class MidiInSharedImpl;
        // Creates the implementation for `settings` and lets `open` start it
        // (openPort() passes a lambda that opens the KS pin).
        void openPortWith(const Settings& settings,
            const std::function<void(MidiInImplBase&)>& open);
        // Opens the port through WinMM (Transport::Shared).
        void openShared(unsigned int portNumber, const Settings& settings);
        void applyCallbacks();
        std::unique_ptr<MidiInImplBase> pimpl_;
        Settings settings_{};
        // Callbacks registered through the public API; applied at every openPort().
        std::shared_ptr<MessageCallback> callback_;
        std::shared_ptr<UmpCallback> umpCallback_;
        std::shared_ptr<ErrorCallback> errorCallback_;
        DirectMessageCallback directCallback_ = nullptr;
        void* directUserData_ = nullptr;
    };

    /**
     * @class MidiOut
     * @brief A class for sending MIDI messages to an output port.
     * @details Movable, not copyable. A moved-from MidiOut behaves like a closed one: it
     * can be destroyed, assigned to or reopened with openPort().
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
        /// Opens the port through the given path (see Transport). Transport::Shared sends through
        /// WinMM and the Windows MIDI service, so the port stays shared with other applications.
        void openPort(unsigned int portNumber, Transport transport);
        void closePort();
        bool isPortOpen() const noexcept;
        bool isUmpStream() const noexcept;
        /// The path the open port uses: Transport::Direct or Transport::Shared.
        Transport activeTransport() const noexcept;

        /**
         * @name Sending
         * Writes are SYNCHRONOUS and BLOCKING: each call issues one IOCTL_KS_WRITE_STREAM and
         * returns only when the driver has completed it (Transport::Shared: when WinMM has taken
         * the message). KSMidi does no buffering and no scheduling (KSMUSICFORMAT::TimeDeltaMs is
         * always 0). Calls on one MidiOut are serialized by an internal mutex, so a driver that
         * stalls a write also blocks other sendMessage() and closePort() calls on that object;
         * there is no timeout. Do not send from a thread that must never block (e.g. a direct
         * input callback). Sending on a port that is not open does nothing.
         * @throws KsMidiError if the driver rejects the write.
         */
        ///@{
        /// One MIDI 1.0 message, or a complete SysEx (F0 ... F7), as raw bytes.
        void sendMessage(const std::vector<BYTE>& message);
        void sendMessage(const BYTE* message, size_t size);
        /// The same, written in place: sendMessage({ 0x90, 60, 100 }).
        void sendMessage(std::initializer_list<BYTE> message);

        /// One Universal MIDI Packet (UMP ports), written as host-order 32-bit words.
        void sendMessage(const ump::UmpMessage& message);
        ///@}

    private:
        friend struct detail::TestAccess;
        class MidiOutImpl;
        std::unique_ptr<MidiOutImpl> pimpl_;
    };

}  // namespace ksmidi
