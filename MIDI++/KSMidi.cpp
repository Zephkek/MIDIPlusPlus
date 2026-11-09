/*
    KSMidi Core Implementation - KSMidi.cpp
    Author: Mohamed Maatallah
    Date: June 28, 2025 (1.1.0: August 2026)
    Version: 1.1.0

    This is the full internal implementation of the KSMidi library,
    responsible for interfacing directly with Windows Kernel Streaming (KS)
    to achieve low latency for MIDI input/output.

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

#include "KSMidi.h"

#include <avrt.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <setupapi.h>

#ifndef STATIC_KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET
#define STATIC_KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET \
    0xfbffd49e, 0xce26, 0x464a, 0x9d, 0xfc, 0xfe, 0xe4, 0x24, 0x56, 0xc8, 0x1c
DEFINE_GUIDSTRUCT("FBFFD49E-CE26-464A-9DFC-FEE42456C81C", KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET);
#define KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET DEFINE_GUIDNAMED(KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET)
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ksuser.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "cfgmgr32.lib")

// Aligns a value up to the nearest alignment boundary
#define KS_ALIGN_UP(v, a) (((v) + (a) - 1) & ~((a) - 1))

namespace ksmidi {
    namespace internal {

        struct HandleDeleter {
            void operator()(HANDLE h) const {
                if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
            }
        };
        using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

        struct DevInfoDeleter {
            void operator()(HDEVINFO h) const {
                if (h && h != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(h);
            }
        };
        using UniqueDevInfo =
            std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DevInfoDeleter>;

        static std::string FormatWinError(HRESULT err) {
            if (err == S_OK) return {};
            char* msg = nullptr;
            DWORD len = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&msg), 0, nullptr);
            std::string text = (len > 0 && msg) ? std::string(msg, len) : std::string();
            if (msg) LocalFree(msg);
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                text.back() == ' ' || text.back() == '\t'))
                text.pop_back();

            std::ostringstream os;
            os << " (0x" << std::hex << err << std::dec << ")";
            if (!text.empty()) os << ": " << text;
            return os.str();
        }

        // --- Pin I/O ---
        class PinIo {
        public:
            virtual ~PinIo() = default;
            virtual HANDLE openFilter(const std::wstring& path, DWORD access, DWORD shareMode,
                DWORD flags) = 0;
            virtual HRESULT createPin(HANDLE filter, PKSPIN_CONNECT connect, ACCESS_MASK access,
                HANDLE* pin) = 0;
            virtual BOOL submitRead(HANDLE pin, KSSTREAM_HEADER& header,
                OVERLAPPED& overlapped) = 0;
            virtual BOOL cancelAll(HANDLE pin) = 0;
            virtual BOOL setState(HANDLE pin, KSSTATE state) = 0;
        };

        inline BOOL syncIoctl(HANDLE handle, DWORD code, void* in, DWORD inSize, void* out,
            DWORD outSize, DWORD* returned, HANDLE event) {
            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            if (DeviceIoControl(handle, code, in, inSize, out, outSize, returned, &overlapped))
                return TRUE;
            if (GetLastError() != ERROR_IO_PENDING) return FALSE;
            return GetOverlappedResult(handle, &overlapped, returned, TRUE);
        }

        class KsPinIo final : public PinIo {
        public:
            HANDLE openFilter(const std::wstring& path, DWORD access, DWORD shareMode,
                DWORD flags) override {
                return CreateFileW(path.c_str(), access, shareMode, nullptr, OPEN_EXISTING,
                    flags, nullptr);
            }
            HRESULT createPin(HANDLE filter, PKSPIN_CONNECT connect, ACCESS_MASK access,
                HANDLE* pin) override {
                const DWORD err = KsCreatePin(filter, connect, access, pin);
                return err == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(err);
            }
            BOOL submitRead(HANDLE pin, KSSTREAM_HEADER& header,
                OVERLAPPED& overlapped) override {
                DWORD br = 0;
                return DeviceIoControl(pin, IOCTL_KS_READ_STREAM, nullptr, 0, &header,
                    sizeof(header), &br, &overlapped);
            }
            BOOL cancelAll(HANDLE pin) override { return CancelIoEx(pin, nullptr); }
            BOOL setState(HANDLE pin, KSSTATE state) override {
                KSPROPERTY p{ KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE,
                             KSPROPERTY_TYPE_SET };
                DWORD br = 0;
                UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!event) return FALSE;
                const BOOL ok = syncIoctl(pin, IOCTL_KS_PROPERTY, &p, sizeof(p), &state,
                    sizeof(state), &br, event.get());
                const DWORD error = GetLastError();
                event.reset();
                SetLastError(error);
                return ok;
            }
        };

        inline PinIo& ksPinIo() {
            static KsPinIo io;
            return io;
        }

        class MmcssScope {
        public:
            MmcssScope(bool enable, const std::wstring& task) {
                if (!enable) return;
                DWORD taskIndex = 0;
                handle_ = AvSetMmThreadCharacteristicsW(task.c_str(), &taskIndex);
                if (!handle_) error_ = GetLastError();
            }
            ~MmcssScope() {
                if (handle_) AvRevertMmThreadCharacteristics(handle_);
            }
            MmcssScope(const MmcssScope&) = delete;
            MmcssScope& operator=(const MmcssScope&) = delete;
            bool active() const noexcept { return handle_ != nullptr; }
            DWORD error() const noexcept { return error_; }

        private:
            HANDLE handle_ = nullptr;
            DWORD error_ = 0;
        };

        struct ThreadPowerThrottlingState {
            ULONG Version;
            ULONG ControlMask;
            ULONG StateMask;
        };
        constexpr int kThreadPowerThrottling = 3;
        constexpr ULONG kThrottlingCurrentVersion = 1;
        constexpr ULONG kThrottleExecutionSpeed = 0x1;

        class ThreadApi {
        public:
            using GetCpuSetInfoFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG,
                HANDLE, ULONG);
            using SetSelectedCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
            using SetThreadInfoFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);

            static const ThreadApi& get() {
                static const ThreadApi api;
                return api;
            }
            GetCpuSetInfoFn getCpuSetInfo = nullptr;
            SetSelectedCpuSetsFn setSelectedCpuSets = nullptr;
            SetThreadInfoFn setThreadInfo = nullptr;

        private:
            ThreadApi() {
                const HMODULE k = GetModuleHandleW(L"kernel32.dll");
                if (!k) return;
                getCpuSetInfo = reinterpret_cast<GetCpuSetInfoFn>(
                    GetProcAddress(k, "GetSystemCpuSetInformation"));
                setSelectedCpuSets = reinterpret_cast<SetSelectedCpuSetsFn>(
                    GetProcAddress(k, "SetThreadSelectedCpuSets"));
                setThreadInfo = reinterpret_cast<SetThreadInfoFn>(
                    GetProcAddress(k, "SetThreadInformation"));
            }
        };

        inline std::vector<ULONG> performanceCpuSets() {
            std::vector<ULONG> ids;
            const auto getInfo = ThreadApi::get().getCpuSetInfo;
            if (!getInfo) return ids;
            ULONG len = 0;
            getInfo(nullptr, 0, &len, GetCurrentProcess(), 0);
            if (len == 0) return ids;
            std::vector<BYTE> buf(len);
            if (!getInfo(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()), len, &len,
                GetCurrentProcess(), 0))
                return ids;
            std::vector<std::pair<ULONG, BYTE>> sets;
            for (ULONG off = 0; off + 2 * sizeof(DWORD) <= len;) {
                const auto* e =
                    reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buf.data() + off);
                if (e->Size < 2 * sizeof(DWORD) || e->Size > len - off) break;
                if (e->Type == CpuSetInformation && e->Size >= sizeof(SYSTEM_CPU_SET_INFORMATION))
                    sets.emplace_back(e->CpuSet.Id, e->CpuSet.EfficiencyClass);
                off += e->Size;
            }
            BYTE lowest = 0xFF, highest = 0;
            for (const auto& s : sets) {
                lowest = (std::min)(lowest, s.second);
                highest = (std::max)(highest, s.second);
            }
            if (lowest >= highest) return ids;
            for (const auto& s : sets)
                if (s.second == highest) ids.push_back(s.first);
            return ids;
        }

        inline bool tuneCurrentThread(bool performanceCores, bool noPowerThrottling) {
            bool ok = true;
            if (performanceCores) {
                const auto setSets = ThreadApi::get().setSelectedCpuSets;
                const auto ids = performanceCpuSets();
                if (!setSets)
                    ok = false;
                else if (!ids.empty() &&
                    !setSets(GetCurrentThread(), ids.data(), static_cast<ULONG>(ids.size())))
                    ok = false;
            }
            if (noPowerThrottling) {
                const auto setInfo = ThreadApi::get().setThreadInfo;
                ThreadPowerThrottlingState state{ kThrottlingCurrentVersion,
                    kThrottleExecutionSpeed, 0 };
                ok = setInfo && setInfo(GetCurrentThread(), kThreadPowerThrottling, &state,
                    sizeof(state)) && ok;
            }
            return ok;
        }

        inline long long spinTicks(unsigned int microseconds) {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return static_cast<long long>(microseconds) * f.QuadPart / 1000000;
        }
        inline long long qpcTicks() {
            LARGE_INTEGER n;
            QueryPerformanceCounter(&n);
            return n.QuadPart;
        }
        inline bool readCompleted(const OVERLAPPED& o) {
            return *static_cast<const volatile ULONG_PTR*>(&o.Internal) != STATUS_PENDING;
        }

        // --- Output framing ---
        inline size_t umpToBytes(const ump::UmpMessage& message, BYTE(&out)[16]) {
            const size_t words = message.size_in_words == 0
                ? ump::getPacketSizeInWords(ump::getMessageType(message))
                : (std::min)(static_cast<size_t>(message.size_in_words), size_t{ 4 });
            std::memcpy(out, message.words.data(), words * 4);
            return words * 4;
        }

        constexpr size_t kKsEventAlignment = 4;

        inline void buildWriteFrame(std::vector<BYTE>& buffer, const BYTE* message,
            size_t size, KSSTREAM_HEADER& header) {
            const size_t frameSize = KS_ALIGN_UP(sizeof(KSMUSICFORMAT) + size, kKsEventAlignment);
            if (buffer.size() < frameSize) {
                buffer.resize(frameSize);
            }
            const KSMUSICFORMAT musicHeader{ 0, static_cast<ULONG>(size) };
            std::memcpy(buffer.data(), &musicHeader, sizeof(musicHeader));
            if (size > 0) {
                std::memcpy(buffer.data() + sizeof(KSMUSICFORMAT), message, size);
            }
            std::memset(buffer.data() + sizeof(KSMUSICFORMAT) + size, 0,
                frameSize - sizeof(KSMUSICFORMAT) - size);
            header = KSSTREAM_HEADER{};
            header.Size = sizeof(header);
            header.Data = buffer.data();
            header.FrameExtent = static_cast<ULONG>(frameSize);
            header.DataUsed = header.FrameExtent;
            header.PresentationTime.Numerator = 1;
            header.PresentationTime.Denominator = 1;
        }

        // --- MIDI 1.0 Byte Stream Parser ---
        namespace {
            // Lookup table for MIDI message lengths (in data bytes, excluding status byte)
            constexpr std::array<uint8_t, 256> kBytesNeeded = [] {
                std::array<uint8_t, 256> t{};
                for (int s = 0; s < 256; ++s) {
                    uint8_t b = 0;
                    const uint8_t nibble = s & 0xF0;
                    if (nibble == 0xC0 || nibble == 0xD0 || s == 0xF1 || s == 0xF3)
                        b = 1;
                    else if ((nibble >= 0x80 && nibble <= 0xE0) || s == 0xF2)
                        b = 2;
                    t[s] = b;
                }
                return t;
                }();
        }  // namespace

        class MidiParser {
        public:
            struct Config {
                bool ignoreSysex{ true };
                bool ignoreTime{ true };
                bool ignoreSense{ true };
                size_t sysexChunkSize{ 1024 };
            };
            Config config;
            size_t signalsSent = 0;

            MidiParser() {
                message_buffer_.reserve(4);
                if (config.sysexChunkSize > 0) {
                    sysex_buffer_.reserve(config.sysexChunkSize);
                }
            }

            bool process(const BYTE* data, DWORD size,
                LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp) {
                bool messagePushed = false;
                for (DWORD i = 0; i < size; ++i) {
                    if (parseByte(data[i], queue, sourceName, timestamp)) {
                        messagePushed = true;
                    }
                }
                return messagePushed;
            }
            void process(const BYTE* data, DWORD size,
                LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp,
                HANDLE eventToSignal) {
                if (process(data, size, queue, sourceName, timestamp) && eventToSignal) {
                    SetEvent(eventToSignal);
                    ++signalsSent;
                }
            }

            bool flush(LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName,
                double timestamp,
                HANDLE eventToSignal)
            {
                const bool open = state_ == State::SysEx;
                state_ = State::Idle;
                if (!open || config.ignoreSysex || sysex_buffer_.empty())
                    return false;
                if (!emitSysEx(true, queue, sourceName, timestamp))
                    return false;
                if (eventToSignal) {
                    SetEvent(eventToSignal);
                    ++signalsSent;
                }
                return true;
            }

        private:
            bool parseByte(BYTE byte, LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp)
            {
                if (byte >= 0xF8)
                    return handleRealTime(byte, queue, sourceName, timestamp);

                if (state_ == State::SysEx) {
                    if (byte < 0x80)
                        return appendSysEx(byte, queue, sourceName, timestamp);
                    if (byte == 0xF7) {
                        const bool pushed = appendSysEx(byte, queue, sourceName, timestamp);
                        state_ = State::Idle;
                        runningStatus_ = 0;
                        if (config.ignoreSysex) return pushed;
                        return emitSysEx(false, queue, sourceName, timestamp) || pushed;
                    }
                    state_ = State::Idle;
                    runningStatus_ = 0;
                    const bool pushed = !config.ignoreSysex && !sysex_buffer_.empty() &&
                        emitSysEx(false, queue, sourceName, timestamp);
                    return handleStatus(byte, queue, sourceName, timestamp) || pushed;
                }

                if (byte >= 0x80)
                    return handleStatus(byte, queue, sourceName, timestamp);

                if (state_ == State::Idle) {
                    if (runningStatus_ == 0) return false;
                    message_buffer_.clear();
                    message_buffer_.push_back(runningStatus_);
                    bytesNeeded_ = kBytesNeeded[runningStatus_];
                    state_ = State::ExpectData;
                }
                message_buffer_.push_back(byte);
                if (message_buffer_.size() == static_cast<size_t>(bytesNeeded_ + 1)) {
                    state_ = State::Idle;
                    return handleCompleteMessage(queue, sourceName, timestamp);
                }
                return false;
            }

            bool handleStatus(BYTE byte, LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp)
            {
                if (byte == 0xF0) {
                    state_ = State::SysEx;
                    runningStatus_ = 0;
                    sysex_buffer_.clear();
                    if (!config.ignoreSysex) {
                        sysex_buffer_.reserve(config.sysexChunkSize ? config.sysexChunkSize : 64);
                        sysex_buffer_.push_back(byte);
                    }
                    return false;
                }

                message_buffer_.clear();
                message_buffer_.push_back(byte);
                bytesNeeded_ = kBytesNeeded[byte];
                state_ = (bytesNeeded_ > 0) ? State::ExpectData : State::Idle;
                runningStatus_ = (byte < 0xF0) ? byte : 0;
                return (bytesNeeded_ == 0) ? handleCompleteMessage(queue, sourceName, timestamp) : false;
            }

            bool handleRealTime(BYTE byte, LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp)
            {
                bool shouldPush = true;  // FA start, FB continue, FC stop, FF system reset
                if (byte == 0xF8) {
                    shouldPush = !config.ignoreTime;
                }
                else if (byte == 0xFE) {
                    shouldPush = !config.ignoreSense;
                }
                else if (byte == 0xF9 || byte == 0xFD) {
                    shouldPush = false;
                }
                if (shouldPush) {
                    return queue.try_push({ timestamp, {byte}, sourceName, false });
                }
                return false;
            }

            bool appendSysEx(BYTE byte, LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp)
            {
                if (config.ignoreSysex) return false;
                bool pushed = false;
                if (config.sysexChunkSize > 0 && sysex_buffer_.size() >= config.sysexChunkSize)
                    pushed = emitSysEx(true, queue, sourceName, timestamp);
                sysex_buffer_.push_back(byte);
                return pushed;
            }

            bool emitSysEx(bool isChunk, LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp)
            {
                MidiMessage msg;
                msg.timestamp = timestamp;
                msg.bytes = std::move(sysex_buffer_);
                msg.source = sourceName;
                msg.isSysExChunk = isChunk;
                sysex_buffer_.clear();
                sysex_buffer_.reserve(config.sysexChunkSize ? config.sysexChunkSize : 64);
                return queue.try_push(std::move(msg));
            }

            bool handleCompleteMessage(LockFreeSPSCQueue<MidiMessage>& queue,
                const std::string& sourceName, double timestamp) {
                BYTE status = message_buffer_[0];
                bool shouldPush = false;
                if (status < 0xF0) {
                    shouldPush = true;  // channel messages
                }
                else if (status == 0xF1) {
                    shouldPush = !config.ignoreTime;  // MTC quarter frame
                }
                else if (status == 0xF2 || status == 0xF3 || status == 0xF6) {
                    shouldPush = true;  // song position, song select, tune request
                }
                if (shouldPush) {
                    MidiMessage msg;
                    msg.timestamp = timestamp;
                    msg.bytes = std::move(message_buffer_);
                    msg.source = sourceName;
                    message_buffer_.clear();
                    message_buffer_.reserve(4);  // Keep capacity for next message
                    return queue.try_push(std::move(msg));
                }
                return false;
            }

            enum class State { Idle, ExpectData, SysEx };
            State state_{ State::Idle };
            std::vector<BYTE> message_buffer_;
            std::vector<BYTE> sysex_buffer_;
            BYTE runningStatus_ = 0;
            int bytesNeeded_ = 0;
        };

        // --- MIDI 2.0 UMP Parser ---
        class UmpParser {
        public:
            size_t signalsSent = 0;

            bool process(const BYTE* data, DWORD size,
                LockFreeSPSCQueue<ump::UmpMessage>& queue,
                const std::string& sourceName, double timestamp) {
                bool messagePushed = false;
                for (DWORD i = 0; i < size; ++i) {
                    if (parseByte(data[i], queue, sourceName, timestamp)) {
                        messagePushed = true;
                    }
                }
                return messagePushed;
            }
            void process(const BYTE* data, DWORD size,
                LockFreeSPSCQueue<ump::UmpMessage>& queue,
                const std::string& sourceName, double timestamp,
                HANDLE eventToSignal) {
                if (process(data, size, queue, sourceName, timestamp) && eventToSignal) {
                    SetEvent(eventToSignal);
                    ++signalsSent;
                }
            }

        private:
            bool parseByte(BYTE byte, LockFreeSPSCQueue<ump::UmpMessage>& queue,
                const std::string& sourceName, double timestamp) {
                wordBytes_[wordFill_++] = byte;
                if (wordFill_ < 4) return false;
                wordFill_ = 0;
                uint32_t word;
                std::memcpy(&word, wordBytes_.data(), sizeof(word));  // host order
                if (wordsReceived_ == 0) {
                    wordsExpected_ = ump::getPacketSizeInWords(static_cast<uint8_t>(word >> 28));
                }
                packet_[wordsReceived_++] = word;
                if (wordsReceived_ < wordsExpected_) return false;

                ump::UmpMessage msg;
                msg.timestamp = timestamp;
                msg.source = sourceName;
                msg.size_in_words = wordsExpected_;
                for (uint8_t i = 0; i < wordsExpected_; ++i) msg.words[i] = packet_[i];
                wordsReceived_ = 0;
                wordsExpected_ = 0;
                return queue.try_push(std::move(msg));
            }

            std::array<BYTE, 4> wordBytes_{};
            uint8_t wordFill_ = 0;
            std::array<uint32_t, 4> packet_{};
            uint8_t wordsReceived_ = 0;
            uint8_t wordsExpected_ = 0;
        };

        // --- Ports as Windows lists them ---
        inline std::wstring filterKey(std::wstring path) {
            const size_t open = path.find(L"#{");
            if (open != std::wstring::npos) {
                const size_t close = path.find(L'}', open);
                if (close != std::wstring::npos) path.erase(open + 1, close - open);
            }
            for (auto& c : path) c = static_cast<wchar_t>(towlower(c));
            return path;
        }

        struct WinmmPort {
            UINT index = 0;
            std::string name;
            std::wstring key;
        };

        inline std::wstring winmmInterface(bool input, UINT index) {
            ULONG size = 0;
            const MMRESULT r = input
                ? midiInMessage(reinterpret_cast<HMIDIIN>(static_cast<UINT_PTR>(index)),
                    DRV_QUERYDEVICEINTERFACESIZE, reinterpret_cast<DWORD_PTR>(&size), 0)
                : midiOutMessage(reinterpret_cast<HMIDIOUT>(static_cast<UINT_PTR>(index)),
                    DRV_QUERYDEVICEINTERFACESIZE, reinterpret_cast<DWORD_PTR>(&size), 0);
            if (r != MMSYSERR_NOERROR || size < sizeof(wchar_t)) return {};
            std::wstring path(size / sizeof(wchar_t), L'\0');
            const MMRESULT q = input
                ? midiInMessage(reinterpret_cast<HMIDIIN>(static_cast<UINT_PTR>(index)),
                    DRV_QUERYDEVICEINTERFACE, reinterpret_cast<DWORD_PTR>(path.data()), size)
                : midiOutMessage(reinterpret_cast<HMIDIOUT>(static_cast<UINT_PTR>(index)),
                    DRV_QUERYDEVICEINTERFACE, reinterpret_cast<DWORD_PTR>(path.data()), size);
            if (q != MMSYSERR_NOERROR) return {};
            path.resize(wcsnlen(path.c_str(), path.size()));
            return path;
        }

        inline std::vector<WinmmPort> winmmPorts(bool input) {
            std::vector<WinmmPort> ports;
            const UINT count = input ? midiInGetNumDevs() : midiOutGetNumDevs();
            for (UINT i = 0; i < count; ++i) {
                WinmmPort port;
                port.index = i;
                if (input) {
                    MIDIINCAPSW caps{};
                    if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
                    port.name = toUtf8(caps.szPname);
                }
                else {
                    MIDIOUTCAPSW caps{};
                    if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
                    port.name = toUtf8(caps.szPname);
                }
                const std::wstring path = winmmInterface(input, i);
                if (!path.empty()) port.key = filterKey(path);
                ports.push_back(std::move(port));
            }
            return ports;
        }

        inline size_t matchWinmm(const std::vector<DeviceInfo>& devices, size_t i,
            const std::vector<WinmmPort>& winmm) {
            const std::wstring key = filterKey(devices[i].path);
            size_t rank = 0;
            for (size_t j = 0; j < devices.size(); ++j) {
                if (j != i && devices[j].pinId < devices[i].pinId && filterKey(devices[j].path) == key)
                    ++rank;
            }
            size_t seen = 0;
            for (size_t w = 0; w < winmm.size(); ++w) {
                if (!winmm[w].key.empty() && winmm[w].key == key) {
                    if (seen == rank) return w;
                    ++seen;
                }
            }
            for (size_t w = 0; w < winmm.size(); ++w) {
                if (winmm[w].key.empty() && winmm[w].name == devices[i].name) return w;
            }
            return SIZE_MAX;
        }

        // --- Windows MIDI Services view of the KS ports ---
        struct ServicePin {
            std::wstring key;
            DWORD pinId = 0;
            bool input = false;
            int group = -1;
            std::wstring endpointId;
            std::string endpointName;
        };

        // {E7CCE071-3C03-423F-88D3-F1045D02552B}
        constexpr GUID kUmpBidiInterface = { 0xe7cce071, 0x3c03, 0x423f,
            { 0x88, 0xd3, 0xf1, 0x04, 0x5d, 0x02, 0x55, 0x2b } };
        // DEVPKEY_KsAggMidiGroupPinMap {05279CB1-002F-4E6B-A3A3-29A87D82B4F7},16
        constexpr DEVPROPKEY kKsaPinMapKey = { { 0x05279cb1, 0x002f, 0x4e6b,
            { 0xa3, 0xa3, 0x29, 0xa8, 0x7d, 0x82, 0xb4, 0xf7 } }, 16 };
        // PKEY_MIDI_CustomEndpointName {3F114A6A-11FA-4BD0-9D2C-6B7780CD80AD},500
        constexpr DEVPROPKEY kCustomEndpointNameKey = { { 0x3f114a6a, 0x11fa, 0x4bd0,
            { 0x9d, 0x2c, 0x6b, 0x77, 0x80, 0xcd, 0x80, 0xad } }, 500 };

        inline std::vector<BYTE> interfaceProperty(const wchar_t* iface, const DEVPROPKEY& key,
            DEVPROPTYPE expected) {
            DEVPROPTYPE type = 0;
            ULONG size = 0;
            if (CM_Get_Device_Interface_PropertyW(iface, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
                type != expected || size == 0)
                return {};
            std::vector<BYTE> value(size);
            if (CM_Get_Device_Interface_PropertyW(iface, &key, &type, value.data(), &size, 0) != CR_SUCCESS)
                return {};
            value.resize(size);
            return value;
        }
        inline std::wstring stringProperty(const wchar_t* iface, const DEVPROPKEY& key) {
            const auto v = interfaceProperty(iface, key, DEVPROP_TYPE_STRING);
            if (v.size() < sizeof(wchar_t)) return {};
            std::wstring s(reinterpret_cast<const wchar_t*>(v.data()), v.size() / sizeof(wchar_t));
            s.resize(wcsnlen(s.c_str(), s.size()));
            return s;
        }

        inline std::vector<ServicePin> parseKsaPinMap(const std::vector<BYTE>& blob) {
            std::vector<ServicePin> pins;
            if (blob.size() < 4) return pins;
            uint32_t total = 0;
            std::memcpy(&total, blob.data(), 4);
            const size_t end = (std::min)(blob.size(), static_cast<size_t>(total));
            size_t at = 4;
            while (at + 18 <= end) {
                uint32_t entrySize = 0, pinId = 0;
                int32_t flow = 0;
                std::memcpy(&entrySize, blob.data() + at, 4);
                if (entrySize < 18 || at + entrySize > end) break;
                std::memcpy(&pinId, blob.data() + at + 8, 4);
                std::memcpy(&flow, blob.data() + at + 12, 4);
                ServicePin pin;
                pin.group = blob[at + 4];
                pin.pinId = pinId;
                pin.input = flow == 1;
                const size_t chars = (entrySize - 16) / sizeof(wchar_t);
                std::wstring filter(chars, L'\0');
                std::memcpy(filter.data(), blob.data() + at + 16, chars * sizeof(wchar_t));
                filter.resize(wcsnlen(filter.c_str(), filter.size()));
                pin.key = filterKey(filter);
                pins.push_back(std::move(pin));
                at += entrySize;
            }
            return pins;
        }

        inline std::vector<ServicePin> servicePins() {
            std::vector<ServicePin> pins;
            ULONG length = 0;
            GUID cls = kUmpBidiInterface;
            if (CM_Get_Device_Interface_List_SizeW(&length, &cls, nullptr,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length <= 1)
                return pins;
            std::vector<wchar_t> list(length);
            if (CM_Get_Device_Interface_ListW(&cls, nullptr, list.data(), length,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
                return pins;
            for (const wchar_t* iface = list.data(); *iface; iface += wcslen(iface) + 1) {
                auto entries = parseKsaPinMap(interfaceProperty(iface, kKsaPinMapKey, DEVPROP_TYPE_BINARY));
                if (entries.empty()) continue;
                std::wstring name = stringProperty(iface, kCustomEndpointNameKey);
                if (name.empty()) name = stringProperty(iface, DEVPKEY_DeviceInterface_FriendlyName);
                const std::string endpointName = toUtf8(name);
                for (auto& e : entries) {
                    e.endpointId = iface;
                    e.endpointName = endpointName;
                    pins.push_back(std::move(e));
                }
            }
            return pins;
        }

        inline void attachWindowsPorts(std::vector<DeviceInfo>& devices, bool input) {
            const auto winmm = winmmPorts(input);
            const auto service = servicePins();
            for (size_t i = 0; i < devices.size(); ++i) {
                const size_t w = matchWinmm(devices, i, winmm);
                devices[i].sharedAvailable = w != SIZE_MAX;
                devices[i].windowsName = w != SIZE_MAX ? winmm[w].name : std::string();
                const std::wstring key = filterKey(devices[i].path);
                for (const auto& pin : service) {
                    if (pin.input == input && pin.pinId == devices[i].pinId && pin.key == key) {
                        devices[i].serviceEndpointId = pin.endpointId;
                        devices[i].serviceEndpointName = pin.endpointName;
                        devices[i].serviceGroup = pin.group;
                        break;
                    }
                }
            }
        }

        inline UINT winmmIndexFor(const std::vector<DeviceInfo>& devices, size_t portNumber, bool input) {
            const auto winmm = winmmPorts(input);
            const size_t w = matchWinmm(devices, portNumber, winmm);
            return w == SIZE_MAX ? UINT_MAX : winmm[w].index;
        }

        inline KsMidiError winmmError(const std::string& what, MMRESULT result) {
            wchar_t text[MAXERRORLENGTH]{};
            midiInGetErrorTextW(result, text, MAXERRORLENGTH);
            std::string message = what;
            if (text[0]) message += " (WinMM: " + toUtf8(text) + ")";
            HRESULT hr = E_FAIL;
            switch (result) {
            case MMSYSERR_ALLOCATED: hr = HRESULT_FROM_WIN32(ERROR_BUSY); break;
            case MMSYSERR_BADDEVICEID: hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND); break;
            case MMSYSERR_NODRIVER: hr = HRESULT_FROM_WIN32(ERROR_DEV_NOT_EXIST); break;
            case MMSYSERR_NOMEM: hr = E_OUTOFMEMORY; break;
            case MMSYSERR_INVALPARAM: hr = E_INVALIDARG; break;
            case MIDIERR_NOTREADY: hr = HRESULT_FROM_WIN32(ERROR_NOT_READY); break;
            default: break;
            }
            return KsMidiError(message, hr);
        }

        // --- Device Enumeration ---
        class DeviceEnumerator {
        public:
            using GetClassDevsFn = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);

            static std::vector<DeviceInfo> enumerate(const GUID& category,
                KSPIN_DATAFLOW flow, GetClassDevsFn getClassDevs = &SetupDiGetClassDevsW,
                bool withWindowsPorts = false) {
                HDEVINFO rawDevInfo = getClassDevs(
                    &category, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
                if (rawDevInfo == INVALID_HANDLE_VALUE || rawDevInfo == nullptr) {
                    const DWORD err = GetLastError();
                    throw KsMidiError("SetupDiGetClassDevs failed", HRESULT_FROM_WIN32(err));
                }
                UniqueDevInfo devInfo(rawDevInfo);

                std::vector<DeviceInfo> devices;
                SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };

                for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo.get(), nullptr,
                    &category, i, &ifd);
                    ++i) {
                    DWORD neededBytes = 0;
                    SetupDiGetDeviceInterfaceDetailW(devInfo.get(), &ifd, nullptr, 0,
                        &neededBytes, nullptr);
                    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

                    std::vector<BYTE> detailBuffer(neededBytes);
                    auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(
                        detailBuffer.data());
                    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                    if (!SetupDiGetDeviceInterfaceDetailW(devInfo.get(), &ifd, detail,
                        neededBytes, nullptr, nullptr))
                        continue;

                    UniqueHandle filter(CreateFileW(detail->DevicePath, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr));
                    if (!filter || filter.get() == INVALID_HANDLE_VALUE) continue;

                    KSPROPERTY pinProp{ KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES,
                                       KSPROPERTY_TYPE_GET };
                    DWORD pinCount = 0, bytesReturned = 0;
                    if (!DeviceIoControl(filter.get(), IOCTL_KS_PROPERTY, &pinProp,
                        sizeof(pinProp), &pinCount, sizeof(pinCount),
                        &bytesReturned, nullptr))
                        continue;

                    for (DWORD pinId = 0; pinId < pinCount; ++pinId) {
                        auto support = getPinMidiSupport(filter.get(), pinId, flow);
                        if (support.first) {
                            DeviceInfo info;
                            info.id = static_cast<unsigned int>(devices.size());
                            info.name = getFriendlyName(devInfo.get(), &ifd);
                            info.path = detail->DevicePath;
                            info.pinId = pinId;
                            info.isAvailable = getAvailableInstances(filter.get(), pinId) > 0;
                            info.supportsMidi2 = support.second;
                            devices.push_back(std::move(info));
                        }
                    }
                }
                if (withWindowsPorts) attachWindowsPorts(devices, flow == KSPIN_DATAFLOW_OUT);
                return devices;
            }

        private:
            static std::pair<bool, bool> getPinMidiSupport(HANDLE filter, DWORD pinId,
                KSPIN_DATAFLOW desiredFlow) {
                KSP_PIN pinFlowProp{
                    {KSPROPSETID_Pin, KSPROPERTY_PIN_DATAFLOW, KSPROPERTY_TYPE_GET},
                    pinId,
                    0 };
                KSPIN_DATAFLOW flow;
                DWORD bytesReturned = 0;
                if (!DeviceIoControl(filter, IOCTL_KS_PROPERTY, &pinFlowProp,
                    sizeof(pinFlowProp), &flow, sizeof(flow),
                    &bytesReturned, nullptr) ||
                    flow != desiredFlow)
                    return { false, false };

                KSP_PIN pinRangeProp{
                    {KSPROPSETID_Pin, KSPROPERTY_PIN_DATARANGES, KSPROPERTY_TYPE_GET},
                    pinId,
                    0 };
                ULONG size = 0;
                DeviceIoControl(filter, IOCTL_KS_PROPERTY, &pinRangeProp,
                    sizeof(pinRangeProp), nullptr, 0, &size, nullptr);
                if (size == 0) return { false, false };

                std::vector<BYTE> buffer(size);
                if (!DeviceIoControl(filter, IOCTL_KS_PROPERTY, &pinRangeProp,
                    sizeof(pinRangeProp), buffer.data(), size,
                    &bytesReturned, nullptr))
                    return { false, false };

                bool supportsMidi1 = false, supportsMidi2 = false;
                auto* multipleItem = reinterpret_cast<PKSMULTIPLE_ITEM>(buffer.data());
                auto* dataRange = reinterpret_cast<PKSDATARANGE>(multipleItem + 1);

                for (ULONG i = 0; i < multipleItem->Count; ++i) {
                    if (IsEqualGUID(dataRange->MajorFormat, KSDATAFORMAT_TYPE_MUSIC)) {
                        if (IsEqualGUID(dataRange->SubFormat, KSDATAFORMAT_SUBTYPE_MIDI))
                            supportsMidi1 = true;
                        else if (IsEqualGUID(dataRange->SubFormat,
                            KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET))
                            supportsMidi2 = true;
                    }
                    dataRange =
                        reinterpret_cast<PKSDATARANGE>(reinterpret_cast<PBYTE>(dataRange) +
                            KS_ALIGN_UP(dataRange->FormatSize, 8));
                }
                return { supportsMidi1 || supportsMidi2, supportsMidi2 };
            }

            static long getAvailableInstances(HANDLE filter, DWORD pinId) {
                KSP_PIN pinInstancesProp{
                    {KSPROPSETID_Pin, KSPROPERTY_PIN_CINSTANCES, KSPROPERTY_TYPE_GET},
                    pinId,
                    0 };
                KSPIN_CINSTANCES instances{};
                DWORD bytesReturned = 0;
                if (DeviceIoControl(filter, IOCTL_KS_PROPERTY, &pinInstancesProp,
                    sizeof(pinInstancesProp), &instances, sizeof(instances),
                    &bytesReturned, nullptr)) {
                    return instances.PossibleCount - instances.CurrentCount;
                }
                return 0;
            }

            static std::string getFriendlyName(HDEVINFO devInfo,
                SP_DEVICE_INTERFACE_DATA* ifd) {
                char name[256] = "Unknown Device";
                HKEY regKey = SetupDiOpenDeviceInterfaceRegKey(devInfo, ifd, 0, KEY_READ);
                if (regKey != INVALID_HANDLE_VALUE) {
                    WCHAR wName[256]{};
                    DWORD size = sizeof(wName);
                    if (RegQueryValueExW(regKey, L"FriendlyName", nullptr, nullptr,
                        reinterpret_cast<LPBYTE>(wName),
                        &size) == ERROR_SUCCESS) {
                        WideCharToMultiByte(CP_UTF8, 0, wName, -1, name, sizeof(name), nullptr,
                            nullptr);
                    }
                    RegCloseKey(regKey);
                }
                return name;
            }
        };
    }  // namespace internal

    std::string toUtf8(std::wstring_view text) {
        if (text.empty()) return {};
        const int wideLen = static_cast<int>((std::min)(text.size(), size_t{ 0x7FFFFFFF }));
        const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), wideLen, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), wideLen, out.data(), n, nullptr, nullptr);
        return out;
    }

    namespace internal {
        inline std::vector<DeviceInfo> listPorts(bool input, bool withWindowsPorts) {
            return input
                ? DeviceEnumerator::enumerate(KSCATEGORY_CAPTURE, KSPIN_DATAFLOW_OUT,
                    &SetupDiGetClassDevsW, withWindowsPorts)
                : DeviceEnumerator::enumerate(KSCATEGORY_RENDER, KSPIN_DATAFLOW_IN,
                    &SetupDiGetClassDevsW, withWindowsPorts);
        }
        inline DeviceInfo directPortInfo(bool input, unsigned int portNumber) {
            auto devices = listPorts(input, false);
            if (portNumber >= devices.size())
                throw KsMidiError(input ? "Invalid input port number specified."
                    : "Invalid output port number specified.", E_INVALIDARG);
            return devices[portNumber];
        }
    }  // namespace internal

    KsMidiError::KsMidiError(std::string_view what, HRESULT code)
        : std::runtime_error(std::string(what) + internal::FormatWinError(code)),
        code_(code) {
    }
    HRESULT KsMidiError::code() const noexcept { return code_; }
    unsigned int Api::getPortCountIn() {
        return static_cast<unsigned int>(internal::DeviceEnumerator::enumerate(
            KSCATEGORY_CAPTURE, KSPIN_DATAFLOW_OUT)
            .size());
    }
    unsigned int Api::getPortCountOut() {
        return static_cast<unsigned int>(internal::DeviceEnumerator::enumerate(
            KSCATEGORY_RENDER, KSPIN_DATAFLOW_IN)
            .size());
    }
    std::vector<DeviceInfo> Api::getPortsIn() {
        return internal::DeviceEnumerator::enumerate(KSCATEGORY_CAPTURE, KSPIN_DATAFLOW_OUT,
            &SetupDiGetClassDevsW, true);
    }
    std::vector<DeviceInfo> Api::getPortsOut() {
        return internal::DeviceEnumerator::enumerate(KSCATEGORY_RENDER, KSPIN_DATAFLOW_IN,
            &SetupDiGetClassDevsW, true);
    }
    DeviceInfo Api::getPortInfoIn(unsigned int portNumber) {
        auto devices = internal::DeviceEnumerator::enumerate(KSCATEGORY_CAPTURE,
            KSPIN_DATAFLOW_OUT, &SetupDiGetClassDevsW, true);
        if (portNumber >= devices.size())
            throw KsMidiError("Invalid input port number specified.", E_INVALIDARG);
        return devices[portNumber];
    }
    DeviceInfo Api::getPortInfoOut(unsigned int portNumber) {
        auto devices = internal::DeviceEnumerator::enumerate(KSCATEGORY_RENDER,
            KSPIN_DATAFLOW_IN, &SetupDiGetClassDevsW, true);
        if (portNumber >= devices.size())
            throw KsMidiError("Invalid output port number specified.", E_INVALIDARG);
        return devices[portNumber];
    }

    // --- MidiOut Implementation ---
    class MidiOut::MidiOutImpl {
        friend struct ::ksmidi::detail::TestAccess;

    public:
        MidiOutImpl() {
            writeBuffer_.reserve(2048);
        }
        ~MidiOutImpl() noexcept { closePort(); }

        void openPort(unsigned int portNumber) {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
            openDevice(internal::directPortInfo(false, portNumber));
        }

        void openPort(unsigned int portNumber, Transport transport) {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
            if (transport == Transport::Direct) {
                openDevice(internal::directPortInfo(false, portNumber));
                return;
            }
            if (transport == Transport::Auto) {
                try {
                    openDevice(internal::directPortInfo(false, portNumber));
                    return;
                }
                catch (const KsMidiError& e) {
                    if (e.code() == E_INVALIDARG) throw;
                    try {
                        openSharedPort(portNumber);
                    }
                    catch (const KsMidiError&) {
                        throw e;
                    }
                    return;
                }
            }
            openSharedPort(portNumber);
        }

        void openSharedPort(unsigned int portNumber) {
            const auto devices = internal::listPorts(false, false);
            if (portNumber >= devices.size())
                throw KsMidiError("Invalid output port number specified.", E_INVALIDARG);
            openShared(devices[portNumber], internal::winmmIndexFor(devices, portNumber, false));
        }

        void openShared(const DeviceInfo& info, UINT winmmIndex) {
            if (winmmIndex == UINT_MAX)
                throw KsMidiError("Output port '" + info.name +
                    "' is not listed by Windows, so it cannot be opened shared.",
                    HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
            internal::UniqueHandle done(CreateEvent(nullptr, FALSE, FALSE, nullptr));
            if (!done) throw KsMidiError("CreateEvent failed.", HRESULT_FROM_WIN32(GetLastError()));
            HMIDIOUT handle = nullptr;
            const MMRESULT r = midiOutOpen(&handle, winmmIndex,
                reinterpret_cast<DWORD_PTR>(done.get()), 0, CALLBACK_EVENT);
            if (r != MMSYSERR_NOERROR)
                throw internal::winmmError("Failed to open output port '" + info.name +
                    "' through Windows", r);
            winmm_ = handle;
            winmmDone_ = std::move(done);
            info_ = info;
            info_.supportsMidi2 = false;
        }

        Transport activeTransport() const noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            return winmm_ ? Transport::Shared : Transport::Direct;
        }

        void openDevice(const DeviceInfo& info) {
            try {
                info_ = info;
                if (!info_.isAvailable)
                    throw KsMidiError("Output port '" + info_.name + "' is not available.",
                        E_ACCESSDENIED);

                if (!writeEvent_) writeEvent_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!writeEvent_)
                    throw KsMidiError("CreateEvent failed.", HRESULT_FROM_WIN32(GetLastError()));

                filter_.reset(io_->openFilter(info_.path, GENERIC_WRITE, 0, 0));
                if (!filter_ || filter_.get() == INVALID_HANDLE_VALUE)
                    throw KsMidiError("Failed to open device filter",
                        HRESULT_FROM_WIN32(GetLastError()));

                const size_t connectSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT);
                std::vector<BYTE> connectBuffer(connectSize);
                auto* connect = reinterpret_cast<PKSPIN_CONNECT>(connectBuffer.data());
                auto* dataFormat = reinterpret_cast<PKSDATAFORMAT>(connect + 1);
                connect->Interface = { KSINTERFACESETID_Standard,
                                      KSINTERFACE_STANDARD_STREAMING, 0 };
                connect->Medium = { KSMEDIUMSETID_Standard, 0, 0 };
                connect->PinId = info_.pinId;
                connect->Priority = { KSPRIORITY_NORMAL, 1 };
                *dataFormat = { sizeof(KSDATAFORMAT),
                               0,
                               0,
                               0,
                               KSDATAFORMAT_TYPE_MUSIC,
                               info_.supportsMidi2
                                   ? KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET
                                   : KSDATAFORMAT_SUBTYPE_MIDI,
                               KSDATAFORMAT_SPECIFIER_NONE };

                HANDLE rawPinHandle = nullptr;
                HRESULT hr =
                    io_->createPin(filter_.get(), connect, GENERIC_WRITE, &rawPinHandle);
                if (FAILED(hr)) throw KsMidiError("Failed to create output pin.", hr);
                pin_.reset(rawPinHandle);

                setPinState(KSSTATE_ACQUIRE);
                setPinState(KSSTATE_PAUSE);
                setPinState(KSSTATE_RUN);
            }
            catch (...) {
                closePortImpl();
                throw;
            }
        }

        void closePort() {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
        }

        bool isPortOpen() const noexcept {
            std::lock_guard<std::mutex> lock(mutex_);
            return pin_ != nullptr || winmm_ != nullptr;
        }

        bool isUmpStream() const noexcept { return info_.supportsMidi2; }

        void sendMessageImpl(const BYTE* message, size_t size) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!message || size == 0) return;
            if (winmm_) {
                sendShared(message, size);
                return;
            }
            if (!pin_) return;

            if (size > (std::numeric_limits<DWORD>::max() - sizeof(KSMUSICFORMAT)))
                throw KsMidiError("MIDI message is too large.", E_OUTOFMEMORY);

            KSSTREAM_HEADER streamHeader{};
            internal::buildWriteFrame(writeBuffer_, message, size, streamHeader);

            DWORD bytesReturned = 0;
            if (!internal::syncIoctl(pin_.get(), IOCTL_KS_WRITE_STREAM, nullptr, 0,
                &streamHeader, sizeof(streamHeader), &bytesReturned, writeEvent_.get())) {
                throw KsMidiError("Failed to write to MIDI stream.",
                    HRESULT_FROM_WIN32(GetLastError()));
            }
        }

    private:
        void sendShared(const BYTE* message, size_t size) {
            if (size <= 3 && message[0] >= 0x80 && message[0] != 0xF0) {
                DWORD packed = message[0];
                if (size > 1) packed |= DWORD(message[1]) << 8;
                if (size > 2) packed |= DWORD(message[2]) << 16;
                const MMRESULT r = midiOutShortMsg(winmm_, packed);
                if (r != MMSYSERR_NOERROR) throw internal::winmmError("Failed to send a MIDI message", r);
                return;
            }
            if (size > (std::numeric_limits<DWORD>::max)())
                throw KsMidiError("MIDI message is too large.", E_OUTOFMEMORY);
            writeBuffer_.assign(message, message + size);
            MIDIHDR header{};
            header.lpData = reinterpret_cast<LPSTR>(writeBuffer_.data());
            header.dwBufferLength = static_cast<DWORD>(size);
            MMRESULT r = midiOutPrepareHeader(winmm_, &header, sizeof(header));
            if (r != MMSYSERR_NOERROR) throw internal::winmmError("Failed to prepare a MIDI buffer", r);
            r = midiOutLongMsg(winmm_, &header, sizeof(header));
            if (r == MMSYSERR_NOERROR) {
                while (!(header.dwFlags & MHDR_DONE)) WaitForSingleObject(winmmDone_.get(), 100);
            }
            midiOutUnprepareHeader(winmm_, &header, sizeof(header));
            if (r != MMSYSERR_NOERROR) throw internal::winmmError("Failed to send a MIDI message", r);
        }

        void closePortImpl() {
            if (winmm_) {
                midiOutClose(winmm_);
                winmm_ = nullptr;
                winmmDone_.reset();
            }
            if (pin_) {
                for (KSSTATE state : { KSSTATE_PAUSE, KSSTATE_ACQUIRE, KSSTATE_STOP }) {
                    if (state >= pinState_) continue;
                    try {
                        setPinState(state);
                    }
                    catch (const KsMidiError&) {
                    }
                    pinState_ = state;
                }
                pin_.reset();
            }
            filter_.reset();
            info_ = {};
            pinState_ = KSSTATE_STOP;
        }

        void setPinState(KSSTATE state) {
            if (io_->setState(pin_.get(), state))
                pinState_ = state;
            else if (state != KSSTATE_STOP)
                throw KsMidiError("Failed to set pin state",
                    HRESULT_FROM_WIN32(GetLastError()));
        }

        internal::PinIo* io_ = &internal::ksPinIo();
        mutable std::mutex mutex_;
        DeviceInfo info_;
        internal::UniqueHandle filter_, pin_;
        KSSTATE pinState_ = KSSTATE_STOP;
        std::vector<BYTE> writeBuffer_;
        internal::UniqueHandle writeEvent_;
        HMIDIOUT winmm_ = nullptr;
        internal::UniqueHandle winmmDone_;
    };
    MidiOut::MidiOut() : pimpl_(std::make_unique<MidiOutImpl>()) {}
    MidiOut::~MidiOut() noexcept {
        if (pimpl_) {
            try {
                pimpl_->closePort();
            }
            catch (...) {
            }
        }
    }
    MidiOut::MidiOut(MidiOut&&) noexcept = default;
    MidiOut& MidiOut::operator=(MidiOut&&) noexcept = default;
    void MidiOut::openPort(unsigned int portNumber) {
        if (!pimpl_) pimpl_ = std::make_unique<MidiOutImpl>();
        pimpl_->openPort(portNumber);
    }
    void MidiOut::openPort(unsigned int portNumber, Transport transport) {
        if (!pimpl_) pimpl_ = std::make_unique<MidiOutImpl>();
        pimpl_->openPort(portNumber, transport);
    }
    void MidiOut::closePort() {
        if (pimpl_) pimpl_->closePort();
    }
    bool MidiOut::isPortOpen() const noexcept {
        return pimpl_ ? pimpl_->isPortOpen() : false;
    }
    bool MidiOut::isUmpStream() const noexcept {
        return pimpl_ ? pimpl_->isUmpStream() : false;
    }
    Transport MidiOut::activeTransport() const noexcept {
        return pimpl_ ? pimpl_->activeTransport() : Transport::Direct;
    }
    void MidiOut::sendMessage(const std::vector<BYTE>& message) {
        if (pimpl_) pimpl_->sendMessageImpl(message.data(), message.size());
    }
    void MidiOut::sendMessage(const BYTE* message, size_t size) {
        if (pimpl_) pimpl_->sendMessageImpl(message, size);
    }
    void MidiOut::sendMessage(std::initializer_list<BYTE> message) {
        if (pimpl_) pimpl_->sendMessageImpl(message.begin(), message.size());
    }
    void MidiOut::sendMessage(const ump::UmpMessage& message) {
        if (message.size_in_words > 4)
            throw KsMidiError("A UMP is 1 to 4 words long (size_in_words).", E_INVALIDARG);
        if (!pimpl_) return;
        if (pimpl_->activeTransport() == Transport::Shared)
            throw KsMidiError("UMP is not available through Transport::Shared (WinMM is MIDI 1.0).",
                E_NOTIMPL);
        BYTE bytes[16];
        const size_t n = internal::umpToBytes(message, bytes);
        pimpl_->sendMessageImpl(bytes, n);
    }

    // --- MidiIn Implementation ---
    class MidiIn::MidiInImplBase {
    public:
        virtual ~MidiInImplBase() = default;
        virtual void openPort(unsigned int) = 0;
        virtual void closePort() = 0;
        virtual bool isPortOpen() const noexcept = 0;
        virtual bool isUmpStream() const noexcept = 0;
        virtual bool try_pop_message(MidiMessage&) noexcept = 0;
        virtual std::optional<MidiMessage> pop_message() noexcept = 0;
        virtual void setCallback(MessageCallback) = 0;
        virtual void cancelCallback() = 0;
        virtual bool try_pop_ump_message(ump::UmpMessage&) noexcept = 0;
        virtual std::optional<ump::UmpMessage> pop_ump_message() noexcept = 0;
        virtual void setUmpCallback(UmpCallback) = 0;
        virtual void cancelUmpCallback() = 0;
        virtual bool try_pop_error(KsMidiError&) noexcept = 0;
        virtual std::optional<KsMidiError> pop_error() noexcept = 0;
        virtual void setDirectCallback(DirectMessageCallback, void*) = 0;
        virtual void cancelDirectCallback() = 0;
        virtual void setErrorCallback(ErrorCallback) = 0;
        virtual MmcssStatus getMmcssStatus() const noexcept = 0;
        virtual Transport transport() const noexcept = 0;
    };

    template <MidiIn::TimestampMode TMode>
    class MidiIn::MidiInImpl final : public MidiIn::MidiInImplBase {
        friend struct ::ksmidi::detail::TestAccess;

        struct ReadRequest {
            internal::UniqueHandle event;
            std::vector<BYTE> data;
            KSSTREAM_HEADER header{};
            OVERLAPPED overlapped{};
            bool inFlight = false;
            ReadRequest(DWORD bufferSize) : data(bufferSize) {
                event.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
                overlapped.hEvent = event.get();
                header.Size = sizeof(header);
                header.Data = data.data();
                header.FrameExtent = bufferSize;
            }
        };

    public:
        explicit MidiInImpl(const MidiIn::Settings& settings)
            : settings_(settings),
            messageQueue_(settings.messageQueueSize),
            umpMessageQueue_(settings.umpMessageQueueSize),
            errorQueue_(settings.errorQueueSize) {
            if (settings.bufferCount < 2 || settings.bufferCount > MAXIMUM_WAIT_OBJECTS - 1)
                throw KsMidiError("Buffer count must be between 2 and 63.", E_INVALIDARG);
            if (settings.eventAlignment != 4 && settings.eventAlignment != 8)
                throw KsMidiError("eventAlignment must be 4 or 8.", E_INVALIDARG);
            if constexpr (TMode == MidiIn::TimestampMode::QPC) {
                LARGE_INTEGER freq;
                QueryPerformanceFrequency(&freq);
                perf_freq_reciprocal_ = 1.0 / static_cast<double>(freq.QuadPart);
            }
            spinTicks_ = internal::spinTicks(settings.spinMicroseconds);
            dispatch_event_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));  // auto reset
            stop_event_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));       // manual reset
            if (!dispatch_event_ || !stop_event_)
                throw KsMidiError("CreateEvent failed.", HRESULT_FROM_WIN32(GetLastError()));
        }
        ~MidiInImpl() noexcept override { closePort(); }

        void openPort(unsigned int portNumber) override {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
            openDevice(internal::directPortInfo(true, portNumber));
        }
        Transport transport() const noexcept override { return Transport::Direct; }
        void openDevice(const DeviceInfo& info) {
            try {
                info_ = info;
                isUmpStream_ = info_.supportsMidi2;
                if (!info_.isAvailable)
                    throw KsMidiError("Input port '" + info_.name + "' is not available.",
                        E_ACCESSDENIED);

                filter_.reset(io_->openFilter(info_.path, GENERIC_READ, FILE_SHARE_READ,
                    FILE_FLAG_OVERLAPPED));
                if (!filter_ || filter_.get() == INVALID_HANDLE_VALUE)
                    throw KsMidiError("Failed to open device filter",
                        HRESULT_FROM_WIN32(GetLastError()));

                const size_t connectSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT);
                std::vector<BYTE> connectBuffer(connectSize);
                auto* connect = reinterpret_cast<PKSPIN_CONNECT>(connectBuffer.data());
                auto* dataFormat = reinterpret_cast<PKSDATAFORMAT>(connect + 1);
                connect->Interface = { KSINTERFACESETID_Standard,
                                      KSINTERFACE_STANDARD_STREAMING, 0 };
                connect->Medium = { KSMEDIUMSETID_Standard, 0, 0 };
                connect->PinId = info_.pinId;
                connect->Priority = { KSPRIORITY_NORMAL, 1 };
                *dataFormat = { sizeof(KSDATAFORMAT),
                               0,
                               0,
                               0,
                               KSDATAFORMAT_TYPE_MUSIC,
                               isUmpStream_ ? KSDATAFORMAT_SUBTYPE_UNIVERSALMIDIPACKET
                                            : KSDATAFORMAT_SUBTYPE_MIDI,
                               KSDATAFORMAT_SPECIFIER_NONE };

                HANDLE rawPinHandle = nullptr;
                HRESULT hr =
                    io_->createPin(filter_.get(), connect, GENERIC_READ, &rawPinHandle);
                if (FAILED(hr)) throw KsMidiError("Failed to create input pin.", hr);
                pin_.reset(rawPinHandle);

                transitionToRun();
                startStreamingImpl();
            }
            catch (...) {
                closePortImpl();
                throw;
            }
        }
        void transitionToRun() {
            setPinState(KSSTATE_ACQUIRE);
            setPinState(KSSTATE_PAUSE);
            setPinState(KSSTATE_RUN);
        }
        void startStreamingImpl() {
            ResetEvent(stop_event_.get());
            parser_.config = { settings_.ignoreSysex, settings_.ignoreTime,
                              settings_.ignoreSense, settings_.sysexChunkSize };
            stop_flag_ = false;
            timestamp_baseline_ = 0.0;
            baseline_is_set_ = false;
            reader_thread_ = std::thread(&MidiInImpl::readerLoop, this);
        }
        void closePort() override {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
        }
        bool isPortOpen() const noexcept override {
            return !stop_flag_.load(std::memory_order_relaxed);
        }
        bool isUmpStream() const noexcept override { return isUmpStream_; }
        MmcssStatus getMmcssStatus() const noexcept override {
            MmcssStatus st;
            st.requested = settings_.useMmcss;
            st.readerRegistered = reader_mmcss_.load(std::memory_order_acquire);
            st.callbackRegistered = dispatcher_mmcss_.load(std::memory_order_acquire);
            st.lastError = mmcss_error_.load(std::memory_order_acquire);
            return st;
        }
        bool try_pop_message(MidiMessage& m) noexcept override {
            if (message_cb_active_.load(std::memory_order_acquire)) return false;
            return messageQueue_.try_pop(m);
        }
        std::optional<MidiMessage> pop_message() noexcept override {
            if (message_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return messageQueue_.pop();
        }
        void setCallback(MessageCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            cancelDirectCallbackImpl();
            ump_callback_ = nullptr;
            message_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }
        void cancelCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            message_callback_ = nullptr;
            startDispatcherIfNeeded();
        }
        bool try_pop_ump_message(ump::UmpMessage& m) noexcept override {
            if (ump_cb_active_.load(std::memory_order_acquire)) return false;
            return umpMessageQueue_.try_pop(m);
        }
        std::optional<ump::UmpMessage> pop_ump_message() noexcept override {
            if (ump_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return umpMessageQueue_.pop();
        }
        void setUmpCallback(UmpCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            cancelDirectCallbackImpl();
            message_callback_ = nullptr;
            ump_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }
        void cancelUmpCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            ump_callback_ = nullptr;
            startDispatcherIfNeeded();
        }
        bool try_pop_error(KsMidiError& e) noexcept override {
            if (error_cb_active_.load(std::memory_order_acquire)) return false;
            return errorQueue_.try_pop(e);
        }
        std::optional<KsMidiError> pop_error() noexcept override {
            if (error_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return errorQueue_.pop();
        }
        void setDirectCallback(DirectMessageCallback cb, void* ud) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            message_callback_ = nullptr;
            ump_callback_ = nullptr;
            startDispatcherIfNeeded();
            cancelDirectCallbackImpl();
            direct_callback_user_data_.store(ud, std::memory_order_release);
            direct_callback_.store(cb, std::memory_order_release);
        }
        void cancelDirectCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelDirectCallbackImpl();
        }
        void setErrorCallback(ErrorCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            error_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }

    private:
        void closePortImpl() {
            stop_flag_.store(true, std::memory_order_release);
            if (reader_thread_.joinable() || pin_) {
                SetEvent(stop_event_.get());
                if (reader_thread_.joinable()) reader_thread_.join();
                cancelDirectCallbackImpl();
                if (pin_) {
                    if (parser_.flush(messageQueue_, info_.name, 0.0, nullptr))
                        SetEvent(dispatch_event_.get());
                    for (KSSTATE state : { KSSTATE_PAUSE, KSSTATE_ACQUIRE, KSSTATE_STOP }) {
                        if (state >= pinState_) continue;
                        try {
                            setPinState(state);
                        }
                        catch (...) {
                        }
                        pinState_ = state;
                    }
                }
                pin_.reset();
            }
            filter_.reset();
            info_ = {};
            isUmpStream_ = false;
            pinState_ = KSSTATE_STOP;
            stopDispatcher();
            message_callback_ = nullptr;
            ump_callback_ = nullptr;
            error_callback_ = nullptr;
        }
        void cancelDirectCallbackImpl() {
            direct_callback_.store(nullptr, std::memory_order_seq_cst);
            if (GetCurrentThreadId() != reader_thread_id_.load(std::memory_order_acquire)) {
                while (direct_in_use_.load(std::memory_order_seq_cst)) SwitchToThread();
            }
            direct_callback_user_data_.store(nullptr, std::memory_order_release);
        }

        // --- Queued callbacks ---
        void stopDispatcher() {
            if (dispatcher_thread_.joinable()) {
                stop_dispatch_.store(true, std::memory_order_release);
                SetEvent(dispatch_event_.get());
                dispatcher_thread_.join();
            }
            message_cb_active_.store(false, std::memory_order_release);
            ump_cb_active_.store(false, std::memory_order_release);
            error_cb_active_.store(false, std::memory_order_release);
        }
        void startDispatcherIfNeeded() {
            message_cb_active_.store(static_cast<bool>(message_callback_), std::memory_order_release);
            ump_cb_active_.store(static_cast<bool>(ump_callback_), std::memory_order_release);
            error_cb_active_.store(static_cast<bool>(error_callback_), std::memory_order_release);
            if (!message_callback_ && !ump_callback_ && !error_callback_) return;
            stop_dispatch_.store(false, std::memory_order_release);
            dispatcher_thread_ = std::thread(&MidiInImpl::dispatchLoop, this);
        }
        void dispatchLoop() {
            internal::MmcssScope mmcss(settings_.useMmcss, settings_.mmcssTaskName);
            dispatcher_mmcss_.store(mmcss.active(), std::memory_order_release);
            if (mmcss.error()) mmcss_error_.store(mmcss.error(), std::memory_order_release);
            internal::tuneCurrentThread(settings_.preferPerformanceCores,
                settings_.disablePowerThrottling);
            const bool poll = settings_.waitMode != WaitMode::Block &&
                (message_callback_ || ump_callback_);
            for (;;) {
                deliverPending();
                if (stop_dispatch_.load(std::memory_order_acquire)) break;
                if (poll && pollForWork()) continue;
                WaitForSingleObject(dispatch_event_.get(), INFINITE);
            }
            dispatcher_mmcss_.store(false, std::memory_order_release);
        }
        bool pollForWork() {
            WaitForSingleObject(dispatch_event_.get(), 0);
            const bool forever = settings_.waitMode == WaitMode::Spin;
            const long long deadline = forever ? 0 : internal::qpcTicks() + spinTicks_;
            for (unsigned int i = 0;; ++i) {
                if (stop_dispatch_.load(std::memory_order_acquire)) return true;
                if ((message_callback_ && !messageQueue_.empty()) ||
                    (ump_callback_ && !umpMessageQueue_.empty()) ||
                    (error_callback_ && !errorQueue_.empty()))
                    return true;
                if (!forever && (i & 15) == 0 && internal::qpcTicks() >= deadline) return false;
                YieldProcessor();
            }
        }
        void deliverPending() noexcept {
            if (message_callback_) {
                MidiMessage m;
                while (messageQueue_.try_pop(m)) {
                    try { message_callback_(m); }
                    catch (...) {}
                }
            }
            if (ump_callback_) {
                ump::UmpMessage m;
                while (umpMessageQueue_.try_pop(m)) {
                    try { ump_callback_(m); }
                    catch (...) {}
                }
            }
            if (error_callback_) {
                KsMidiError e;
                while (errorQueue_.try_pop(e)) {
                    try { error_callback_(e); }
                    catch (...) {}
                }
            }
        }

        static constexpr DWORD kCancelGraceMs = 2000;

        DWORD waitForRead(ReadRequest& req) {
            if (settings_.waitMode != WaitMode::Block) {
                const bool forever = settings_.waitMode == WaitMode::Spin;
                const long long deadline = forever ? 0 : internal::qpcTicks() + spinTicks_;
                for (unsigned int i = 0;; ++i) {
                    if (internal::readCompleted(req.overlapped)) return WAIT_OBJECT_0 + 1;
                    if (stop_flag_.load(std::memory_order_relaxed)) return WAIT_OBJECT_0;
                    if (!forever && (i & 15) == 0 && internal::qpcTicks() >= deadline) break;
                    YieldProcessor();
                }
            }
            const HANDLE waits[2] = { stop_event_.get(), req.event.get() };
            return WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        }

        void readerLoop() {
            reader_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
            internal::MmcssScope mmcss(settings_.useMmcss, settings_.mmcssTaskName);
            reader_mmcss_.store(mmcss.active(), std::memory_order_release);
            if (mmcss.error()) {
                mmcss_error_.store(mmcss.error(), std::memory_order_release);
                reportError("MMCSS registration of the MIDI reader thread failed; "
                    "it runs at normal priority.", HRESULT_FROM_WIN32(mmcss.error()));
            }
            if (!internal::tuneCurrentThread(settings_.preferPerformanceCores,
                settings_.disablePowerThrottling)) {
                reportError("The performance core or power throttling setting could not be "
                    "applied to the MIDI reader thread.", HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            }
            std::vector<std::unique_ptr<ReadRequest>> reqs;
            for (unsigned int i = 0; i < settings_.bufferCount; ++i) {
                reqs.push_back(std::make_unique<ReadRequest>(settings_.bufferSize));
            }
            size_t inFlight = 0;
            for (auto& req : reqs) {
                if (!queueRead(*req)) break;
                ++inFlight;
            }
            size_t head = 0;
            while (inFlight > 0 && !stop_flag_.load(std::memory_order_acquire)) {
                while (!reqs[head]->inFlight) head = (head + 1) % reqs.size();
                ReadRequest& req = *reqs[head];
                const DWORD w = waitForRead(req);
                if (w == WAIT_OBJECT_0) break;  // closePort()
                if (w != WAIT_OBJECT_0 + 1) {
                    reportError("MIDI listener wait failed.", HRESULT_FROM_WIN32(GetLastError()));
                    break;
                }
                DWORD br = 0;
                const BOOL ok = GetOverlappedResult(pin_.get(), &req.overlapped, &br, FALSE);
                const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
                req.inFlight = false;
                --inFlight;
                if (stop_flag_.load(std::memory_order_acquire)) break;
                if (ok) {
                    if (br > 0) processData(req.header);
                }
                else {
                    reportError("MIDI stream read failed.", HRESULT_FROM_WIN32(err));
                }
                if (queueRead(req)) ++inFlight;
                head = (head + 1) % reqs.size();
            }
            if (inFlight == 0 && !stop_flag_.load(std::memory_order_acquire)) {
                reportError("MIDI input stopped: no read request could be queued.",
                    HRESULT_FROM_WIN32(ERROR_NOT_READY));
            }
            drainReads(reqs);
            stop_flag_.store(true, std::memory_order_release);
            reader_mmcss_.store(false, std::memory_order_release);
            reader_thread_id_.store(0, std::memory_order_release);
        }

        void drainReads(std::vector<std::unique_ptr<ReadRequest>>& reqs) {
            bool any = false;
            for (const auto& r : reqs) any = any || r->inFlight;
            if (!any) return;
            io_->cancelAll(pin_.get());
            const ULONGLONG deadline = GetTickCount64() + kCancelGraceMs;
            size_t leaked = 0;
            for (auto& r : reqs) {
                if (!r->inFlight) continue;
                const ULONGLONG now = GetTickCount64();
                const DWORD wait = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
                DWORD br = 0;
                if (WaitForSingleObject(r->event.get(), wait) == WAIT_OBJECT_0 ||
                    HasOverlappedIoCompleted(&r->overlapped)) {
                    GetOverlappedResult(pin_.get(), &r->overlapped, &br, FALSE);
                    r->inFlight = false;
                    continue;
                }
                r.release();  // still owned by the driver: leak it on purpose
                ++leaked;
            }
            if (leaked) {
                reportError(std::to_string(leaked) +
                    " cancelled MIDI read request(s) were not completed by the driver; "
                    "their buffers were leaked to avoid a use-after-free.",
                    HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            }
        }

        bool queueRead(ReadRequest& req) {
            ResetEvent(req.event.get());
            std::memset(req.data.data(), 0, req.data.size());
            req.header.DataUsed = 0;
            req.header.PresentationTime = { 0, 1, 1 };
            if (!io_->submitRead(pin_.get(), req.header, req.overlapped)) {
                const DWORD err = GetLastError();
                if (err != ERROR_IO_PENDING) {
                    reportError("Fatal stream read error.", HRESULT_FROM_WIN32(err));
                    return false;
                }
            }
            req.inFlight = true;
            return true;
        }

        void reportError(const std::string& what, HRESULT code) {
            errorQueue_.try_push(KsMidiError(what, code));
            SetEvent(dispatch_event_.get());
        }

        template <class OnEvent>
        static void walkEvents(const KSSTREAM_HEADER& header, size_t alignment, OnEvent&& onEvent) {
            const BYTE* p = static_cast<const BYTE*>(header.Data);
            const BYTE* const end = p + header.DataUsed;
            while (p < end) {
                if (static_cast<size_t>(end - p) < sizeof(KSMUSICFORMAT)) break;
                KSMUSICFORMAT fmt;
                std::memcpy(&fmt, p, sizeof(fmt));
                const BYTE* const payload = p + sizeof(KSMUSICFORMAT);
                if (fmt.ByteCount == 0 || fmt.ByteCount > static_cast<size_t>(end - payload)) break;
                onEvent(fmt, payload);
                const size_t recordSize =
                    (sizeof(KSMUSICFORMAT) + size_t{ fmt.ByteCount } + alignment - 1) & ~(alignment - 1);
                if (recordSize > static_cast<size_t>(end - p)) break;
                p += recordSize;
            }
        }

        double eventTimestamp(const KSMUSICFORMAT& fmt, double buffer_qpc_ts) noexcept {
            if constexpr (TMode == TimestampMode::QPC) {
                return buffer_qpc_ts - timestamp_baseline_;
            }
            else if constexpr (TMode == TimestampMode::None) {
                (void)fmt;
                (void)buffer_qpc_ts;
                return 0.0;
            }
            else {  // Driver
                (void)buffer_qpc_ts;
                return fmt.TimeDeltaMs * 0.001;
            }
        }

        void signalConsumers() noexcept {
            SetEvent(dispatch_event_.get());
            ++signalCount_;
        }

        void processData(const KSSTREAM_HEADER& header) noexcept {
            double buffer_qpc_ts = 0.0;
            if constexpr (TMode == TimestampMode::QPC) {
                LARGE_INTEGER n;
                QueryPerformanceCounter(&n);
                buffer_qpc_ts = static_cast<double>(n.QuadPart) * perf_freq_reciprocal_;
                if (!baseline_is_set_.load(std::memory_order_relaxed)) {
                    timestamp_baseline_ = buffer_qpc_ts;
                    baseline_is_set_.store(true, std::memory_order_relaxed);
                }
            }
            direct_in_use_.store(true, std::memory_order_seq_cst);
            const DirectMessageCallback cb = direct_callback_.load(std::memory_order_seq_cst);
            if (cb) {
                void* const user_data = direct_callback_user_data_.load(std::memory_order_acquire);
                walkEvents(header, settings_.eventAlignment, [&](const KSMUSICFORMAT& fmt, const BYTE* payload) {
                    const double ts = eventTimestamp(fmt, buffer_qpc_ts);
                    try {
                        cb(payload, fmt.ByteCount, ts, user_data);
                    }
                    catch (...) {  // must not escape the reader thread
                    }
                });
                direct_in_use_.store(false, std::memory_order_release);
                return;
            }
            direct_in_use_.store(false, std::memory_order_release);

            bool pushed = false;
            if (!isUmpStream_) {
                walkEvents(header, settings_.eventAlignment, [&](const KSMUSICFORMAT& fmt, const BYTE* payload) {
                    if (parser_.process(payload, fmt.ByteCount, messageQueue_, info_.name,
                        eventTimestamp(fmt, buffer_qpc_ts)))
                        pushed = true;
                });
            }
            else {
                walkEvents(header, settings_.eventAlignment, [&](const KSMUSICFORMAT& fmt, const BYTE* payload) {
                    if (ump_parser_.process(payload, fmt.ByteCount, umpMessageQueue_, info_.name,
                        eventTimestamp(fmt, buffer_qpc_ts)))
                        pushed = true;
                });
            }
            if (pushed) signalConsumers();
        }
        void setPinState(KSSTATE state) {
            if (io_->setState(pin_.get(), state))
                pinState_ = state;
            else if (state != KSSTATE_STOP)
                throw KsMidiError("Failed to set pin state",
                    HRESULT_FROM_WIN32(GetLastError()));
        }

        internal::PinIo* io_ = &internal::ksPinIo();
        mutable std::mutex mutex_;
        LockFreeSPSCQueue<MidiMessage> messageQueue_;
        LockFreeSPSCQueue<ump::UmpMessage> umpMessageQueue_;
        LockFreeSPSCQueue<KsMidiError> errorQueue_;
        MessageCallback message_callback_;
        UmpCallback ump_callback_;
        ErrorCallback error_callback_;
        std::atomic<bool> message_cb_active_{ false };
        std::atomic<bool> ump_cb_active_{ false };
        std::atomic<bool> error_cb_active_{ false };
        std::thread dispatcher_thread_;
        std::atomic<bool> stop_dispatch_{ true };
        std::atomic<DirectMessageCallback> direct_callback_{ nullptr };
        std::atomic<void*> direct_callback_user_data_{ nullptr };
        std::atomic<bool> direct_in_use_{ false };
        std::atomic<DWORD> reader_thread_id_{ 0 };
        std::atomic<bool> reader_mmcss_{ false };
        std::atomic<bool> dispatcher_mmcss_{ false };
        std::atomic<DWORD> mmcss_error_{ 0 };
        std::atomic<bool> stop_flag_{ true };
        internal::UniqueHandle filter_, pin_;
        KSSTATE pinState_ = KSSTATE_STOP;
        internal::UniqueHandle dispatch_event_;
        internal::UniqueHandle stop_event_;
        DeviceInfo info_;
        Settings settings_;
        std::thread reader_thread_;
        internal::MidiParser parser_;
        internal::UmpParser ump_parser_;
        bool isUmpStream_ = false;
        double perf_freq_reciprocal_{ 0.0 };  
        long long spinTicks_ = 0;
        double timestamp_baseline_{ 0.0 };
        std::atomic<bool> baseline_is_set_{ false };
        size_t signalCount_ = 0;
       
    };

    // --- MidiIn through WinMM ---
    class MidiIn::MidiInSharedImpl final : public MidiIn::MidiInImplBase {
        friend struct ::ksmidi::detail::TestAccess;

    public:
        explicit MidiInSharedImpl(const MidiIn::Settings& settings)
            : settings_(settings),
            messageQueue_(settings.messageQueueSize),
            umpMessageQueue_(settings.umpMessageQueueSize),
            errorQueue_(settings.errorQueueSize) {
            if (settings.bufferCount < 1 || settings.bufferCount > 64 || settings.bufferSize < 16)
                throw KsMidiError("SysEx buffers: bufferCount 1..64, bufferSize 16 bytes or more.",
                    E_INVALIDARG);
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            perf_freq_reciprocal_ = 1.0 / static_cast<double>(freq.QuadPart);
            dispatch_event_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
            if (!dispatch_event_)
                throw KsMidiError("CreateEvent failed.", HRESULT_FROM_WIN32(GetLastError()));
        }
        ~MidiInSharedImpl() noexcept override { closePort(); }

        void openPort(unsigned int portNumber) override {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
            const auto devices = internal::listPorts(true, false);
            if (portNumber >= devices.size())
                throw KsMidiError("Invalid input port number specified.", E_INVALIDARG);
            openDevice(devices[portNumber], internal::winmmIndexFor(devices, portNumber, true));
        }
        void openDevice(const DeviceInfo& info, UINT winmmIndex) {
            if (winmmIndex == UINT_MAX)
                throw KsMidiError("Input port '" + info.name +
                    "' is not listed by Windows, so it cannot be opened shared.",
                    HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
            info_ = info;
            info_.supportsMidi2 = false;
            parser_ = internal::MidiParser{};
            parser_.config = { settings_.ignoreSysex, settings_.ignoreTime,
                              settings_.ignoreSense, settings_.sysexChunkSize };
            baseline_is_set_ = false;
            closing_.store(false, std::memory_order_release);
            HMIDIIN handle = nullptr;
            MMRESULT r = midiInOpen(&handle, winmmIndex,
                reinterpret_cast<DWORD_PTR>(&MidiInSharedImpl::winmmCallback),
                reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
            if (r != MMSYSERR_NOERROR)
                throw internal::winmmError("Failed to open input port '" + info.name +
                    "' through Windows", r);
            handle_ = handle;
            try {
                for (unsigned int i = 0; i < settings_.bufferCount; ++i) {
                    auto buffer = std::make_unique<SysexBuffer>(settings_.bufferSize);
                    r = midiInPrepareHeader(handle_, &buffer->header, sizeof(MIDIHDR));
                    if (r == MMSYSERR_NOERROR) r = midiInAddBuffer(handle_, &buffer->header, sizeof(MIDIHDR));
                    sysexBuffers_.push_back(std::move(buffer));
                    if (r != MMSYSERR_NOERROR)
                        throw internal::winmmError("Failed to queue a SysEx buffer", r);
                }
                r = midiInStart(handle_);
                if (r != MMSYSERR_NOERROR) throw internal::winmmError("Failed to start input", r);
                open_.store(true, std::memory_order_release);
            }
            catch (...) {
                closePortImpl();
                throw;
            }
        }
        void closePort() override {
            std::lock_guard<std::mutex> lock(mutex_);
            closePortImpl();
        }
        bool isPortOpen() const noexcept override { return open_.load(std::memory_order_acquire); }
        bool isUmpStream() const noexcept override { return false; }
        Transport transport() const noexcept override { return Transport::Shared; }
        MmcssStatus getMmcssStatus() const noexcept override {
            MmcssStatus st;
            st.requested = settings_.useMmcss;
            st.callbackRegistered = dispatcher_mmcss_.load(std::memory_order_acquire);
            st.lastError = mmcss_error_.load(std::memory_order_acquire);
            return st;
        }
        bool try_pop_message(MidiMessage& m) noexcept override {
            if (message_cb_active_.load(std::memory_order_acquire)) return false;
            return messageQueue_.try_pop(m);
        }
        std::optional<MidiMessage> pop_message() noexcept override {
            if (message_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return messageQueue_.pop();
        }
        void setCallback(MessageCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            cancelDirectCallbackImpl();
            ump_callback_ = nullptr;
            message_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }
        void cancelCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            message_callback_ = nullptr;
            startDispatcherIfNeeded();
        }
        bool try_pop_ump_message(ump::UmpMessage& m) noexcept override {
            if (ump_cb_active_.load(std::memory_order_acquire)) return false;
            return umpMessageQueue_.try_pop(m);
        }
        std::optional<ump::UmpMessage> pop_ump_message() noexcept override {
            if (ump_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return umpMessageQueue_.pop();
        }
        void setUmpCallback(UmpCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            cancelDirectCallbackImpl();
            message_callback_ = nullptr;
            ump_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }
        void cancelUmpCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            ump_callback_ = nullptr;
            startDispatcherIfNeeded();
        }
        bool try_pop_error(KsMidiError& e) noexcept override {
            if (error_cb_active_.load(std::memory_order_acquire)) return false;
            return errorQueue_.try_pop(e);
        }
        std::optional<KsMidiError> pop_error() noexcept override {
            if (error_cb_active_.load(std::memory_order_acquire)) return std::nullopt;
            return errorQueue_.pop();
        }
        void setDirectCallback(DirectMessageCallback cb, void* ud) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            message_callback_ = nullptr;
            ump_callback_ = nullptr;
            startDispatcherIfNeeded();
            cancelDirectCallbackImpl();
            direct_callback_user_data_.store(ud, std::memory_order_release);
            direct_callback_.store(cb, std::memory_order_release);
        }
        void cancelDirectCallback() override {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelDirectCallbackImpl();
        }
        void setErrorCallback(ErrorCallback cb) override {
            std::lock_guard<std::mutex> lock(mutex_);
            stopDispatcher();
            error_callback_ = std::move(cb);
            startDispatcherIfNeeded();
        }

    private:
        struct SysexBuffer {
            std::vector<char> data;
            MIDIHDR header{};
            explicit SysexBuffer(DWORD size) : data(size) {
                header.lpData = data.data();
                header.dwBufferLength = size;
            }
        };

        static void CALLBACK winmmCallback(HMIDIIN, UINT msg, DWORD_PTR instance,
            DWORD_PTR param1, DWORD_PTR param2) {
            auto* self = reinterpret_cast<MidiInSharedImpl*>(instance);
            self->callback_thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
            switch (msg) {
            case MIM_DATA:
                self->onShortMessage(static_cast<DWORD>(param1), static_cast<DWORD>(param2));
                break;
            case MIM_LONGDATA:
                self->onLongMessage(reinterpret_cast<MIDIHDR*>(param1), static_cast<DWORD>(param2));
                break;
            case MIM_ERROR:
            case MIM_LONGERROR:
                self->reportError("Windows delivered an invalid MIDI message.", E_FAIL);
                break;
            default:
                break;
            }
        }

        double timestampFor(DWORD winmmMs) noexcept {
            if (settings_.timestampMode == TimestampMode::None) return 0.0;
            if (settings_.timestampMode == TimestampMode::Driver) return winmmMs * 0.001;
            LARGE_INTEGER n;
            QueryPerformanceCounter(&n);
            const double now = static_cast<double>(n.QuadPart) * perf_freq_reciprocal_;
            if (!baseline_is_set_) {
                timestamp_baseline_ = now;
                baseline_is_set_ = true;
            }
            return now - timestamp_baseline_;
        }

        void onShortMessage(DWORD packed, DWORD winmmMs) noexcept {
            const BYTE bytes[3] = { static_cast<BYTE>(packed), static_cast<BYTE>(packed >> 8),
                                    static_cast<BYTE>(packed >> 16) };
            const size_t size = bytes[0] >= 0xF8 ? 1 : 1 + size_t{ internal::kBytesNeeded[bytes[0]] };
            deliver(bytes, size, timestampFor(winmmMs));
        }

        void onLongMessage(MIDIHDR* header, DWORD winmmMs) noexcept {
            if (closing_.load(std::memory_order_acquire)) return;
            if (header->dwBytesRecorded > 0) {
                deliver(reinterpret_cast<const BYTE*>(header->lpData), header->dwBytesRecorded,
                    timestampFor(winmmMs));
            }
            midiInAddBuffer(handle_, header, sizeof(MIDIHDR));
        }

        void deliver(const BYTE* data, size_t size, double timestamp) noexcept {
            direct_in_use_.store(true, std::memory_order_seq_cst);
            const DirectMessageCallback cb = direct_callback_.load(std::memory_order_seq_cst);
            if (cb) {
                try {
                    cb(data, size, timestamp, direct_callback_user_data_.load(std::memory_order_acquire));
                }
                catch (...) {
                }
                direct_in_use_.store(false, std::memory_order_release);
                return;
            }
            direct_in_use_.store(false, std::memory_order_release);
            if (parser_.process(data, static_cast<DWORD>(size), messageQueue_, info_.name, timestamp))
                SetEvent(dispatch_event_.get());
        }

        void reportError(const std::string& what, HRESULT code) {
            errorQueue_.try_push(KsMidiError(what, code));
            SetEvent(dispatch_event_.get());
        }

        void closePortImpl() {
            open_.store(false, std::memory_order_release);
            if (handle_) {
                closing_.store(true, std::memory_order_release);
                midiInStop(handle_);
                midiInReset(handle_);
                for (auto& b : sysexBuffers_) midiInUnprepareHeader(handle_, &b->header, sizeof(MIDIHDR));
                midiInClose(handle_);
                handle_ = nullptr;
                if (parser_.flush(messageQueue_, info_.name, 0.0, nullptr))
                    SetEvent(dispatch_event_.get());
            }
            sysexBuffers_.clear();
            cancelDirectCallbackImpl();
            info_ = {};
            stopDispatcher();
            message_callback_ = nullptr;
            ump_callback_ = nullptr;
            error_callback_ = nullptr;
        }

        void cancelDirectCallbackImpl() {
            direct_callback_.store(nullptr, std::memory_order_seq_cst);
            if (GetCurrentThreadId() != callback_thread_id_.load(std::memory_order_acquire)) {
                while (direct_in_use_.load(std::memory_order_seq_cst)) SwitchToThread();
            }
            direct_callback_user_data_.store(nullptr, std::memory_order_release);
        }

        void stopDispatcher() {
            if (dispatcher_thread_.joinable()) {
                stop_dispatch_.store(true, std::memory_order_release);
                SetEvent(dispatch_event_.get());
                dispatcher_thread_.join();
            }
            message_cb_active_.store(false, std::memory_order_release);
            ump_cb_active_.store(false, std::memory_order_release);
            error_cb_active_.store(false, std::memory_order_release);
        }
        void startDispatcherIfNeeded() {
            message_cb_active_.store(static_cast<bool>(message_callback_), std::memory_order_release);
            ump_cb_active_.store(static_cast<bool>(ump_callback_), std::memory_order_release);
            error_cb_active_.store(static_cast<bool>(error_callback_), std::memory_order_release);
            if (!message_callback_ && !ump_callback_ && !error_callback_) return;
            stop_dispatch_.store(false, std::memory_order_release);
            dispatcher_thread_ = std::thread(&MidiInSharedImpl::dispatchLoop, this);
        }
        void dispatchLoop() {
            internal::MmcssScope mmcss(settings_.useMmcss, settings_.mmcssTaskName);
            dispatcher_mmcss_.store(mmcss.active(), std::memory_order_release);
            if (mmcss.error()) mmcss_error_.store(mmcss.error(), std::memory_order_release);
            for (;;) {
                deliverPending();
                if (stop_dispatch_.load(std::memory_order_acquire)) break;
                WaitForSingleObject(dispatch_event_.get(), INFINITE);
            }
            dispatcher_mmcss_.store(false, std::memory_order_release);
        }
        void deliverPending() noexcept {
            if (message_callback_) {
                MidiMessage m;
                while (messageQueue_.try_pop(m)) {
                    try { message_callback_(m); }
                    catch (...) {}
                }
            }
            if (ump_callback_) {
                ump::UmpMessage m;
                while (umpMessageQueue_.try_pop(m)) {
                    try { ump_callback_(m); }
                    catch (...) {}
                }
            }
            if (error_callback_) {
                KsMidiError e;
                while (errorQueue_.try_pop(e)) {
                    try { error_callback_(e); }
                    catch (...) {}
                }
            }
        }

        Settings settings_;
        mutable std::mutex mutex_;
        LockFreeSPSCQueue<MidiMessage> messageQueue_;
        LockFreeSPSCQueue<ump::UmpMessage> umpMessageQueue_;
        LockFreeSPSCQueue<KsMidiError> errorQueue_;
        MessageCallback message_callback_;
        UmpCallback ump_callback_;
        ErrorCallback error_callback_;
        std::atomic<bool> message_cb_active_{ false };
        std::atomic<bool> ump_cb_active_{ false };
        std::atomic<bool> error_cb_active_{ false };
        std::thread dispatcher_thread_;
        std::atomic<bool> stop_dispatch_{ true };
        std::atomic<bool> dispatcher_mmcss_{ false };
        std::atomic<DWORD> mmcss_error_{ 0 };
        std::atomic<DirectMessageCallback> direct_callback_{ nullptr };
        std::atomic<void*> direct_callback_user_data_{ nullptr };
        std::atomic<bool> direct_in_use_{ false };
        std::atomic<DWORD> callback_thread_id_{ 0 };
        internal::UniqueHandle dispatch_event_;
        HMIDIIN handle_ = nullptr;
        std::vector<std::unique_ptr<SysexBuffer>> sysexBuffers_;
        std::atomic<bool> open_{ false };
        std::atomic<bool> closing_{ false };
        DeviceInfo info_;
        internal::MidiParser parser_;
        double perf_freq_reciprocal_ = 0.0;
        double timestamp_baseline_ = 0.0;
        bool baseline_is_set_ = false;
    };

    MidiIn::MidiIn() = default;
    MidiIn::~MidiIn() noexcept {
        if (pimpl_) {
            try {
                pimpl_->closePort();
            }
            catch (...) {
            }
        }
    }
    MidiIn::MidiIn(MidiIn&&) noexcept = default;
    MidiIn& MidiIn::operator=(MidiIn&&) noexcept = default;

    void MidiIn::openPort(unsigned int portNumber) {
        openPort(portNumber, settings_);
    }

    void MidiIn::openPort(unsigned int portNumber, const Settings& settings) {
        if (settings.transport == Transport::Shared) {
            openShared(portNumber, settings);
            return;
        }
        try {
            openPortWith(settings,
                [portNumber](MidiInImplBase& impl) { impl.openPort(portNumber); });
        }
        catch (const KsMidiError& e) {
            if (settings.transport != Transport::Auto || e.code() == E_INVALIDARG) throw;
            try {
                openShared(portNumber, settings);
            }
            catch (const KsMidiError&) {
                throw e;
            }
        }
    }

    void MidiIn::openShared(unsigned int portNumber, const Settings& settings) {
        if (pimpl_) {
            pimpl_->closePort();
        }
        settings_ = settings;
        auto is_pow2 = [](size_t n) { return n != 0 && (n & (n - 1)) == 0; };
        if (!is_pow2(settings_.messageQueueSize) ||
            !is_pow2(settings_.umpMessageQueueSize) ||
            !is_pow2(settings_.errorQueueSize))
            throw KsMidiError("Queue sizes must be a power of two.", E_INVALIDARG);
        pimpl_ = std::make_unique<MidiInSharedImpl>(settings_);
        pimpl_->openPort(portNumber);
        applyCallbacks();
    }

    Transport MidiIn::activeTransport() const noexcept {
        return pimpl_ ? pimpl_->transport() : Transport::Direct;
    }

    const MidiIn::Settings& MidiIn::getSettings() const noexcept { return settings_; }

    void MidiIn::openPortWith(const Settings& settings,
        const std::function<void(MidiInImplBase&)>& open) {
        if (pimpl_) {
            pimpl_->closePort();
        }
        settings_ = settings;
        auto is_pow2 = [](size_t n) { return n != 0 && (n & (n - 1)) == 0; };
        if (!is_pow2(settings_.messageQueueSize) ||
            !is_pow2(settings_.umpMessageQueueSize) ||
            !is_pow2(settings_.errorQueueSize))
            throw KsMidiError("Queue sizes must be a power of two.", E_INVALIDARG);

        switch (settings_.timestampMode) {
        case TimestampMode::None:
            pimpl_ = std::make_unique<MidiInImpl<TimestampMode::None>>(settings_);
            break;
        case TimestampMode::Driver:
            pimpl_ = std::make_unique<MidiInImpl<TimestampMode::Driver>>(settings_);
            break;
        default:
            pimpl_ = std::make_unique<MidiInImpl<TimestampMode::QPC>>(settings_);
            break;
        }
        open(*pimpl_);
        applyCallbacks();
    }

    void MidiIn::applyCallbacks() {
        if (!pimpl_) return;
        if (errorCallback_) {
            auto cb = errorCallback_;
            pimpl_->setErrorCallback([cb](const KsMidiError& e) { (*cb)(e); });
        }
        if (callback_) {
            auto cb = callback_;
            pimpl_->setCallback([cb](const MidiMessage& m) { (*cb)(m); });
        }
        else if (umpCallback_) {
            auto cb = umpCallback_;
            pimpl_->setUmpCallback([cb](const ump::UmpMessage& m) { (*cb)(m); });
        }
        else if (directCallback_) {
            pimpl_->setDirectCallback(directCallback_, directUserData_);
        }
    }
    void MidiIn::closePort() {
        if (pimpl_) pimpl_->closePort();
    }
    bool MidiIn::isPortOpen() const noexcept {
        return pimpl_ ? pimpl_->isPortOpen() : false;
    }
    bool MidiIn::isUmpStream() const noexcept {
        return pimpl_ ? pimpl_->isUmpStream() : false;
    }
    MidiIn::MmcssStatus MidiIn::getMmcssStatus() const noexcept {
        return pimpl_ ? pimpl_->getMmcssStatus() : MmcssStatus{};
    }
    bool MidiIn::try_pop_message(MidiMessage& m) noexcept {
        return pimpl_ ? pimpl_->try_pop_message(m) : false;
    }
    std::optional<MidiMessage> MidiIn::pop_message() noexcept {
        return pimpl_ ? pimpl_->pop_message() : std::nullopt;
    }
    void MidiIn::setCallback(MessageCallback cb) {
        callback_ = cb ? std::make_shared<MessageCallback>(std::move(cb)) : nullptr;
        umpCallback_ = nullptr;
        directCallback_ = nullptr;
        directUserData_ = nullptr;
        if (!pimpl_) return;
        if (callback_) {
            auto stored = callback_;
            pimpl_->setCallback([stored](const MidiMessage& m) { (*stored)(m); });
        }
        else {
            pimpl_->setCallback(nullptr);
        }
    }
    void MidiIn::cancelCallback() {
        callback_ = nullptr;
        if (pimpl_) pimpl_->cancelCallback();
    }
    bool MidiIn::try_pop_ump_message(ump::UmpMessage& m) noexcept {
        return pimpl_ ? pimpl_->try_pop_ump_message(m) : false;
    }
    std::optional<ump::UmpMessage> MidiIn::pop_ump_message() noexcept {
        return pimpl_ ? pimpl_->pop_ump_message() : std::nullopt;
    }
    void MidiIn::setUmpCallback(UmpCallback cb) {
        umpCallback_ = cb ? std::make_shared<UmpCallback>(std::move(cb)) : nullptr;
        callback_ = nullptr;
        directCallback_ = nullptr;
        directUserData_ = nullptr;
        if (!pimpl_) return;
        if (umpCallback_) {
            auto stored = umpCallback_;
            pimpl_->setUmpCallback([stored](const ump::UmpMessage& m) { (*stored)(m); });
        }
        else {
            pimpl_->setUmpCallback(nullptr);
        }
    }
    void MidiIn::cancelUmpCallback() {
        umpCallback_ = nullptr;
        if (pimpl_) pimpl_->cancelUmpCallback();
    }
    bool MidiIn::try_pop_error(KsMidiError& e) noexcept {
        return pimpl_ ? pimpl_->try_pop_error(e) : false;
    }
    std::optional<KsMidiError> MidiIn::pop_error() noexcept {
        return pimpl_ ? pimpl_->pop_error() : std::nullopt;
    }
    void MidiIn::setErrorCallback(ErrorCallback cb) {
        errorCallback_ = cb ? std::make_shared<ErrorCallback>(std::move(cb)) : nullptr;
        if (!pimpl_) return;
        if (errorCallback_) {
            auto stored = errorCallback_;
            pimpl_->setErrorCallback([stored](const KsMidiError& e) { (*stored)(e); });
        }
        else {
            pimpl_->setErrorCallback(nullptr);
        }
    }
    void MidiIn::setDirectCallback(DirectMessageCallback cb, void* ud) {
        directCallback_ = cb;
        directUserData_ = cb ? ud : nullptr;
        callback_ = nullptr;
        umpCallback_ = nullptr;
        if (pimpl_) pimpl_->setDirectCallback(cb, directUserData_);
    }
    void MidiIn::cancelDirectCallback() {
        directCallback_ = nullptr;
        directUserData_ = nullptr;
        if (pimpl_) pimpl_->cancelDirectCallback();
    }
    void MidiIn::ignoreTypes(bool s, bool t, bool n) {
        if (pimpl_ && pimpl_->isPortOpen()) {
            throw KsMidiError("Cannot change ignore settings while port is open.",
                E_ACCESSDENIED);
        }
        settings_.ignoreSysex = s;
        settings_.ignoreTime = t;
        settings_.ignoreSense = n;
    }

}  // namespace ksmidi
