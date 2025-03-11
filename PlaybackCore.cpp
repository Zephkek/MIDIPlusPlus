// PlaybackSystem.cpp
#include "PlaybackSystem.hpp"
#include "InputHeader.h"

#include <immintrin.h>
#include <intrin.h>
#include <nmmintrin.h>
#include <mmsystem.h>
#include <cmath>

#pragma comment(lib, "avrt.lib")

// ------------------------------
// Global definitions
// ------------------------------
double g_totalSongSeconds = 0.0;
int currentTransposition = 0; // did they read the instructions? if not, fuck 'em

typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

static std::mutex s_inputMutex;

struct KeySequence {
    std::vector<INPUT> events_press;
    std::vector<INPUT> events_release;
};
static std::unordered_map<std::string, KeySequence> g_keyCache;

// ====================
// SCAN_TABLE_AUTO definition
// ====================
const std::array<WORD, 256> VirtualPianoPlayer::SCAN_TABLE_AUTO = []() {
    std::array<WORD, 256> table{};
    table.fill(0);
    // Numbers
    table[static_cast<unsigned char>('1')] = 0x02;
    table[static_cast<unsigned char>('2')] = 0x03;
    table[static_cast<unsigned char>('3')] = 0x04;
    table[static_cast<unsigned char>('4')] = 0x05;
    table[static_cast<unsigned char>('5')] = 0x06;
    table[static_cast<unsigned char>('6')] = 0x07;
    table[static_cast<unsigned char>('7')] = 0x08;
    table[static_cast<unsigned char>('8')] = 0x09;
    table[static_cast<unsigned char>('9')] = 0x0A;
    table[static_cast<unsigned char>('0')] = 0x0B;
    // Lowercase letters
    table[static_cast<unsigned char>('q')] = 0x10;
    table[static_cast<unsigned char>('w')] = 0x11;
    table[static_cast<unsigned char>('e')] = 0x12;
    table[static_cast<unsigned char>('r')] = 0x13;
    table[static_cast<unsigned char>('t')] = 0x14;
    table[static_cast<unsigned char>('y')] = 0x15;
    table[static_cast<unsigned char>('u')] = 0x16;
    table[static_cast<unsigned char>('i')] = 0x17;
    table[static_cast<unsigned char>('o')] = 0x18;
    table[static_cast<unsigned char>('p')] = 0x19;
    table[static_cast<unsigned char>('a')] = 0x1E;
    table[static_cast<unsigned char>('s')] = 0x1F;
    table[static_cast<unsigned char>('d')] = 0x20;
    table[static_cast<unsigned char>('f')] = 0x21;
    table[static_cast<unsigned char>('g')] = 0x22;
    table[static_cast<unsigned char>('h')] = 0x23;
    table[static_cast<unsigned char>('j')] = 0x24;
    table[static_cast<unsigned char>('k')] = 0x25;
    table[static_cast<unsigned char>('l')] = 0x26;
    table[static_cast<unsigned char>('z')] = 0x2C;
    table[static_cast<unsigned char>('x')] = 0x2D;
    table[static_cast<unsigned char>('c')] = 0x2E;
    table[static_cast<unsigned char>('v')] = 0x2F;
    table[static_cast<unsigned char>('b')] = 0x30;
    table[static_cast<unsigned char>('n')] = 0x31;
    table[static_cast<unsigned char>('m')] = 0x32;
    // Punctuation (shifted numbers)
    table[static_cast<unsigned char>('!')] = 0x02;
    table[static_cast<unsigned char>('@')] = 0x03;
    table[static_cast<unsigned char>('#')] = 0x04;
    table[static_cast<unsigned char>('$')] = 0x05;
    table[static_cast<unsigned char>('%')] = 0x06;
    table[static_cast<unsigned char>('^')] = 0x07;
    table[static_cast<unsigned char>('&')] = 0x08;
    table[static_cast<unsigned char>('*')] = 0x09;
    table[static_cast<unsigned char>('(')] = 0x0A;
    table[static_cast<unsigned char>(')')] = 0x0B;
    // Uppercase letters (map same as lowercase)
    table[static_cast<unsigned char>('Q')] = 0x10;
    table[static_cast<unsigned char>('W')] = 0x11;
    table[static_cast<unsigned char>('E')] = 0x12;
    table[static_cast<unsigned char>('R')] = 0x13;
    table[static_cast<unsigned char>('T')] = 0x14;
    table[static_cast<unsigned char>('Y')] = 0x15;
    table[static_cast<unsigned char>('U')] = 0x16;
    table[static_cast<unsigned char>('I')] = 0x17;
    table[static_cast<unsigned char>('O')] = 0x18;
    table[static_cast<unsigned char>('P')] = 0x19;
    table[static_cast<unsigned char>('A')] = 0x1E;
    table[static_cast<unsigned char>('S')] = 0x1F;
    table[static_cast<unsigned char>('D')] = 0x20;
    table[static_cast<unsigned char>('F')] = 0x21;
    table[static_cast<unsigned char>('G')] = 0x22;
    table[static_cast<unsigned char>('H')] = 0x23;
    table[static_cast<unsigned char>('J')] = 0x24;
    table[static_cast<unsigned char>('K')] = 0x25;
    table[static_cast<unsigned char>('L')] = 0x26;
    table[static_cast<unsigned char>('Z')] = 0x2C;
    table[static_cast<unsigned char>('X')] = 0x2D;
    table[static_cast<unsigned char>('C')] = 0x2E;
    table[static_cast<unsigned char>('V')] = 0x2F;
    table[static_cast<unsigned char>('B')] = 0x30;
    table[static_cast<unsigned char>('N')] = 0x31;
    table[static_cast<unsigned char>('M')] = 0x32;
    return table;
    }();

// ------------------------------
// Helper function: define key mappings from config
// ------------------------------
std::pair<std::map<std::string, std::string>, std::map<std::string, std::string>>
VirtualPianoPlayer::define_key_mappings() {
    auto& cfg = midi::Config::getInstance();
    return { cfg.key_mappings["LIMITED"], cfg.key_mappings["FULL"] };
}

// ------------------------------
// KeySequence precomputation helper
// ------------------------------
static KeySequence computeKeySequence(const std::string& key) {
    KeySequence seq;
    // Detect modifiers
    bool hasAlt = (key.find("alt+") != std::string::npos);
    bool hasCtrl = (key.find("ctrl+") != std::string::npos);
    char lastChar = key.empty() ? '\0' : key.back();
    bool shifted = std::isupper(static_cast<unsigned char>(lastChar)) || std::ispunct(static_cast<unsigned char>(lastChar));
    char lookupChar = shifted ? static_cast<char>(std::tolower(lastChar)) : lastChar;
    WORD mainScan = VirtualPianoPlayer::SCAN_TABLE_AUTO[static_cast<unsigned char>(lookupChar)];
    constexpr WORD SHIFT_SCAN = 0x2A;
    constexpr WORD CTRL_SCAN = 0x1D;
    constexpr WORD ALT_SCAN = 0x38;

    // Build press sequence
    std::vector<INPUT> pressSeq;
    if (shifted && mainScan != 0) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = mainScan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        pressSeq.push_back(input);
    }
    if (hasAlt) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ALT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        pressSeq.push_back(input);
    }
    if (hasCtrl) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = CTRL_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        pressSeq.push_back(input);
    }
    if (shifted) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = SHIFT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        pressSeq.push_back(input);
    }
    if (mainScan != 0) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = mainScan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        pressSeq.push_back(input);
    }
    if (shifted) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = SHIFT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        pressSeq.push_back(input);
    }
    if (hasCtrl) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = CTRL_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        pressSeq.push_back(input);
    }
    if (hasAlt) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ALT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        pressSeq.push_back(input);
    }

    // Build release sequence (simulate full tap)
    std::vector<INPUT> releaseSeq;
    if (hasAlt) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ALT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        releaseSeq.push_back(input);
    }
    if (hasCtrl) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = CTRL_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        releaseSeq.push_back(input);
    }
    if (shifted) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = SHIFT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        releaseSeq.push_back(input);
    }
    if (mainScan != 0) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = mainScan;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        releaseSeq.push_back(input);
    }
    if (shifted) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = SHIFT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        releaseSeq.push_back(input);
    }
    if (hasCtrl) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = CTRL_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        releaseSeq.push_back(input);
    }
    if (hasAlt) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ALT_SCAN;
        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        releaseSeq.push_back(input);
    }
    seq.events_press = std::move(pressSeq);
    seq.events_release = std::move(releaseSeq);
    return seq;
}

// ------------------------------
// OS Version Check Helper (because for some fucking reason ver helper doesn't fucking work)
// ------------------------------
bool IsWin7OrWin8_Real() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll)
        return false;
    auto pRtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hNtdll, "RtlGetVersion"));
    if (!pRtlGetVersion)
        return false;
    RTL_OSVERSIONINFOW info = { 0 };
    info.dwOSVersionInfoSize = sizeof(info);
    if (pRtlGetVersion(&info) != 0)
        return false;
    return (info.dwMajorVersion == 6 && (info.dwMinorVersion == 1 || info.dwMinorVersion == 2));
}

// ====================================================================
// NoteEvent Implementation
// ====================================================================
NoteEvent::NoteEvent() noexcept
    : time(std::chrono::nanoseconds::zero()),
    note(""),
    isPress(false),
    velocity(0),
    isSustain(false),
    sustainValue(0),
    trackIndex(-1)
{}

NoteEvent::NoteEvent(std::chrono::nanoseconds t,
    std::string_view n,
    bool p,
    int v,
    bool s,
    int sv,
    int trackIdx) noexcept
    : time(t),
    note(n),
    isPress(p),
    velocity(v),
    isSustain(s),
    sustainValue(sv),
    trackIndex(trackIdx)
{}

bool NoteEvent::operator>(const NoteEvent& other) const noexcept {
    return time > other.time;
}

// ====================================================================
// NoteEventPool Implementation
// ====================================================================
NoteEventPool::NoteEventPool() : head(nullptr), current(nullptr), end(nullptr), allocated_count(0) {
    allocateNewBlock();
}

NoteEventPool::~NoteEventPool() {
    while (head) {
        Block* next = head->next;
        _aligned_free(head);
        head = next;
    }
}

void NoteEventPool::allocateNewBlock() {
    Block* newBlock = static_cast<Block*>(_aligned_malloc(sizeof(Block), CACHE_LINE_SIZE));
    if (!newBlock) {
        throw std::bad_alloc();
    }
    newBlock->next = head;
    head = newBlock;
    current = newBlock->data;
    end = current + BLOCK_SIZE;
}

void NoteEventPool::reset() {
    current = head->data;
    allocated_count.store(0, std::memory_order_relaxed);
}

size_t NoteEventPool::getAllocatedCount() const {
    return allocated_count.load(std::memory_order_relaxed);
}

// ====================================================================
// PlaybackControl Implementation
// ====================================================================
void PlaybackControl::requestSkip(std::chrono::seconds amount) {
    std::lock_guard<std::mutex> lock(mutex);
    pending_command = Command::SKIP;
    command_amount = amount;
    command_processed.store(false, std::memory_order_release);
}

void PlaybackControl::requestRewind(std::chrono::seconds amount) {
    std::lock_guard<std::mutex> lock(mutex);
    pending_command = Command::REWIND;
    command_amount = amount;
    command_processed.store(false, std::memory_order_release);
}

bool PlaybackControl::hasCommand() const {
    return (pending_command != Command::NONE) && !command_processed.load(std::memory_order_acquire);
}

PlaybackControl::State PlaybackControl::processCommand(const State& current_state, double speed, size_t) {
    std::lock_guard<std::mutex> lock(mutex);
    if (command_processed.load(std::memory_order_acquire))
        return current_state;

    State new_state = current_state;
    auto scaled_amount = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(command_amount.count() * speed));

    switch (pending_command) {
    case Command::SKIP:
        new_state.position += scaled_amount;
        new_state.needs_reset = true;
        break;
    case Command::REWIND:
        new_state.position = (scaled_amount > new_state.position) ? std::chrono::nanoseconds(0)
            : new_state.position - scaled_amount;
        new_state.needs_reset = true;
        break;
    case Command::RESTART:
        new_state.position = std::chrono::nanoseconds(0);
        new_state.event_index = 0;
        new_state.needs_reset = true;
        break;
    default:
        break;
    }
    command_processed.store(true, std::memory_order_release);
    pending_command = Command::NONE;
    return new_state;
}

// ------------------------------
// VirtualPianoPlayer Key Cache Initialization
// ------------------------------
void VirtualPianoPlayer::initializeKeyCache() {
    for (const auto& [note, key] : limited_key_mappings) {
        if (!key.empty()) {
            g_keyCache.emplace(key, computeKeySequence(key));
        }
    }
    for (const auto& [note, key] : full_key_mappings) {
        if (!key.empty()) {
            g_keyCache.emplace(key, computeKeySequence(key));
        }
    }
}

// ====================================================================
// VirtualPianoPlayer Implementation
// ====================================================================

HANDLE VirtualPianoPlayer::command_event = nullptr;

// Global event pool and note buffer
static NoteEventPool event_pool;
static std::vector<NoteEvent*> note_buffer;

VirtualPianoPlayer::VirtualPianoPlayer() noexcept(false)
    : processing_pool(std::thread::hardware_concurrency())
{
    // 1) Load configuration
    try {
        auto& config = midi::Config::getInstance();
        config.loadFromFile("config.json");
    }
    catch (const midi::ConfigException& e) {
        std::cerr << "Configuration error: " << e.what() << "\nLoading default settings...\n";
        midi::Config::getInstance().setDefaults();
        try {
            midi::Config::getInstance().saveToFile("config.json");
        }
        catch (const midi::ConfigException& e2) {
            std::cerr << "Failed to save default config: " << e2.what() << "\n";
        }
    }
    if (IsWin7OrWin8_Real()) {
        MessageBoxA(nullptr,
            "Incompatible OS detected.\nThis software requires Windows 8.1 or later.",
            "Incompatibility Warning",
            MB_ICONERROR | MB_OK);
        throw std::runtime_error("Incompatible OS (Windows 7/8) detected");
    }
    // 3) Key mappings
    auto [limited, full] = define_key_mappings();
    limited_key_mappings = std::move(limited);
    full_key_mappings = std::move(full);
    note_buffer.reserve(1 << 20);
    // 5) Create command_event
    command_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!command_event) {
        CloseHandle(command_event);
        throw std::runtime_error("Failed to create command_event");
    }
    // 6) Hotkeys mapping
    try {
        sustain_key_code = stringToVK(midi::Config::getInstance().hotkeys.SUSTAIN_KEY);
        volume_up_key_code = vkToScanCode(stringToVK(midi::Config::getInstance().hotkeys.VOLUME_UP_KEY));
        volume_down_key_code = vkToScanCode(stringToVK(midi::Config::getInstance().hotkeys.VOLUME_DOWN_KEY));
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Error mapping hotkeys: ") + e.what());
    }
    if (sustain_key_code == 0 || volume_up_key_code == 0 || volume_down_key_code == 0) {
        throw std::runtime_error("Invalid hotkey configuration. Check your config file.");
    }
    SyscallNumber = GetNtUserSendInputSyscallNumber();
    initializeKeyCache();
}

VirtualPianoPlayer::~VirtualPianoPlayer() {
    if (isSustainPressed) {
        releaseKey(sustain_key_code);
        isSustainPressed = false;
    }
    should_stop.store(true, std::memory_order_release);
    if (playback_thread && playback_thread->joinable()) {
        playback_thread->join();
    }
    if (command_event) {
        CloseHandle(command_event);
    }
}

void VirtualPianoPlayer::prepare_event_queue() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    note_buffer.clear();
    event_pool.reset();
    for (auto& e : note_events) {
        std::chrono::nanoseconds time_ns = e.time;
        std::string_view n = e.note_or_control;
        bool isPress = (e.action == "press");
        int velocity = e.velocity;
        int trackIndex = e.trackIndex;
        bool isSustain = (n == "sustain");
        int sValue = isSustain ? (velocity & 0xFF) : 0;
        int realVel = isSustain ? 0 : velocity;
        note_buffer.push_back(
            event_pool.allocate(time_ns, n, isPress, realVel, isSustain, sValue, trackIndex)
        );
    }
    std::stable_sort(note_buffer.begin(), note_buffer.end(),
        [](const NoteEvent* a, const NoteEvent* b) {
            return a->time < b->time;
        });
}

// TODO: you're an absolute retard, you should fucking rewrite this 

void VirtualPianoPlayer::play_notes() {
    prepare_event_queue();

    if (midi::Config::getInstance().auto_transpose.ENABLED) {
        int suggestedTransposition = toggle_transpose_adjustment();
        int diff = suggestedTransposition - currentTransposition;

        std::cout << "Suggested Transposition: " << suggestedTransposition << "\n";

        if (diff != 0) {
            std::cout << "[Transpose] Adjusting by " << diff << " steps.\n";

            std::string upKeyName = midi::Config::getInstance().auto_transpose.TRANSPOSE_UP_KEY;
            std::string downKeyName = midi::Config::getInstance().auto_transpose.TRANSPOSE_DOWN_KEY;

            int upKey = stringToVK(upKeyName);
            int downKey = stringToVK(downKeyName);

            WORD scanCode = MapVirtualKey((diff > 0) ? upKey : downKey, MAPVK_VK_TO_VSC);
            bool isExtended = true;

            for (int i = 0; i < std::abs(diff); ++i) {
                arrowsend(scanCode, isExtended);  // took me an embarrassingly long time to figure this out
                Sleep(50);
            }
            currentTransposition = suggestedTransposition;
        }
    }

    // FUCKING THREAD AFFINITY BECAUSE WINDOWS IS A DRUNK TODDLER THAT CAN'T WALK STRAIGHT
    DWORD_PTR affinity_mask = 1;
    if (!SetThreadAffinityMask(GetCurrentThread(), affinity_mask)) {
        // WE'RE COMPLETELY FUCKED IF THIS FAILS BUT WHO CARES LMAO
    }

    // STOP WINDOWS FROM HAVING A NAP WHILE WE'RE TRYING TO MAKE MUSIC FFS
    EXECUTION_STATE prev_state = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    // nobody actually tested this but idk wtf im doing anymore now so fuck it
    DWORD taskIndex = 0;
    HANDLE mmcss_handle = AvSetMmThreadCharacteristics(L"Pro Audio", &taskIndex);
    if (mmcss_handle) {
        AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_CRITICAL);
    }

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double ticks_per_ns = static_cast<double>(frequency.QuadPart) / 1'000'000'000.0;
    LARGE_INTEGER last_qpc_time;
    QueryPerformanceCounter(&last_qpc_time);

    if (!playback_started.load(std::memory_order_acquire)) {
        playback_started.store(true, std::memory_order_release);
        playback_start_time = std::chrono::steady_clock::now();
        last_resume_time = playback_start_time;
    }
    const size_t buffer_size = note_buffer.size();
    size_t current_index = buffer_index.load(std::memory_order_acquire);

    std::vector<NoteEvent*> batch_events;
    batch_events.reserve(32);  

    while (!should_stop.load(std::memory_order_acquire)) {
        while (paused.load(std::memory_order_acquire) && !should_stop.load(std::memory_order_acquire)) {
            if (WaitForSingleObject(command_event, INFINITE) == WAIT_OBJECT_0) {
                while (playback_control.hasCommand()) {
                    auto current_adjusted = get_adjusted_time();
                    auto new_state = playback_control.processCommand({ current_adjusted, current_index, false },
                        current_speed, buffer_size);
                    if (new_state.needs_reset) {
                        current_index = find_next_event_index(new_state.position);
                        buffer_index.store(current_index, std::memory_order_release);
                        total_adjusted_time = new_state.position;
                        last_resume_time = std::chrono::steady_clock::now();
                        QueryPerformanceCounter(&last_qpc_time);
                    }
                }
                if (!paused.load(std::memory_order_acquire)) {
                    last_resume_time = std::chrono::steady_clock::now();
                    QueryPerformanceCounter(&last_qpc_time);
                    break;
                }
                ResetEvent(command_event);
            }
        }
        if (should_stop.load(std::memory_order_acquire))
            break; 
        if (current_index >= buffer_size)
            break;

        // COMMAND PROCESSING PROBABLY BUGGY, DON'T CARE ANYMORE
        if (playback_control.hasCommand()) {
            while (playback_control.hasCommand()) {
                auto new_state = playback_control.processCommand({ get_adjusted_time(), current_index, false },
                    current_speed, buffer_size);
                if (new_state.needs_reset) {
                    current_index = find_next_event_index(new_state.position);
                    buffer_index.store(current_index, std::memory_order_release);
                    total_adjusted_time = new_state.position;
                    last_resume_time = std::chrono::steady_clock::now();
                    QueryPerformanceCounter(&last_qpc_time);
                }
            }
            ResetEvent(command_event);
        }

        // TIMING HELL I SPENT 3 DAYS GETTING THIS SHIT RIGHT AND I STILL HATE IT
        auto now_adjusted = get_adjusted_time();
        auto event_time = note_buffer[current_index]->time;
        if (event_time > now_adjusted) {
            auto remain_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(event_time - now_adjusted).count();
            LONGLONG target_ticks = static_cast<LONGLONG>(remain_ns * ticks_per_ns);
            LARGE_INTEGER current_qpc;
            if (target_ticks > frequency.QuadPart / 1000) { // > 1ms
                DWORD sleep_ms = static_cast<DWORD>((target_ticks * 1000) / frequency.QuadPart) - 1;
                if (sleep_ms > 0) {
                    Sleep(sleep_ms); // SLEEP IS FOR THE WEAK BUT SO IS THIS CPU
                }
            }

            // SPINLOCK FROM HELL GRABBED FROM STACKEXCHANGE DON'T ASK HOW IT WORKS
            int yield_count = 0;
            do {
                QueryPerformanceCounter(&current_qpc);
                LONGLONG elapsed_ticks = current_qpc.QuadPart - last_qpc_time.QuadPart;

                if (playback_control.hasCommand()) {
                    break; // FORGET TIMING, USER WANTS SOMETHING, PROBABLY STUPID
                }
                if (elapsed_ticks >= target_ticks - static_cast<LONGLONG>(0.05 * frequency.QuadPart / 1000000)) {
                    while (elapsed_ticks < target_ticks) {
                        _mm_pause(); // INTEL MAGIC DON'T YOU DARE REMOVE THIS
                        QueryPerformanceCounter(&current_qpc);
                        elapsed_ticks = current_qpc.QuadPart - last_qpc_time.QuadPart;
                    }
                    break;
                }
                if (++yield_count % 64 == 0) {
                    Sleep(0); 
                }
                else if (yield_count % 16 == 0) {
                    SwitchToThread(); // maybe IDK?
                }
                else {
                    _mm_pause();
                }
            } while (current_qpc.QuadPart - last_qpc_time.QuadPart < target_ticks);

            last_qpc_time = current_qpc;
        }

        auto batch_time = note_buffer[current_index]->time;
        batch_events.clear();

        while (current_index < buffer_size && note_buffer[current_index]->time == batch_time) {
            batch_events.push_back(note_buffer[current_index]);
            current_index++;
        }
        buffer_index.store(current_index, std::memory_order_release);

        
        auto fut = processing_pool.enqueue([this, batch = std::move(batch_events)] {
            for (auto* e : batch) {
                if (!e->isPress && !e->isSustain)
                    execute_note_event(*e);
            }

            for (auto* e : batch) {
                if (e->isSustain)
                    execute_note_event(*e);
            }

            for (auto* e : batch) {
                if (e->isPress && !e->isSustain)
                    execute_note_event(*e);
            }

            return batch.size(); // RETURN VALUE NOBODY EVER CHECKS
            });

        // WAIT BUT NOT TOO LONG BECAUSE THAT WOULD BE STUPID
        if (fut.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
            fut.get(); // LOL WHO CARES IF THIS BLOCKS FOREVER, NOT MY PROBLEM
        }
    }

    if (mmcss_handle) {
        AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    SetThreadAffinityMask(GetCurrentThread(), 0); 
    SetThreadExecutionState(prev_state);
}
void VirtualPianoPlayer::execute_note_event(const NoteEvent& event) noexcept {
    if (!isTrackEnabled(event.trackIndex))
        return;
    if (!event.isSustain) {
        if (event.isPress) {
            if (enable_volume_adjustment.load(std::memory_order_relaxed)) {
                AdjustVolumeBasedOnVelocity(event.velocity);
            }
            if (enable_velocity_keypress.load(std::memory_order_relaxed) && event.velocity != 0) {
                std::string velocityKey = "alt+" + getVelocityKey(event.velocity);
                if (velocityKey != lastPressedKey) {
                    KeyPress(velocityKey, true);
                    KeyPress(velocityKey, false);
                    lastPressedKey = velocityKey;
                }
            }
            press_key(event.note);
        }
        else {
            release_key(event.note);
        }
    }
    else {
        handle_sustain_event(event);
    }
}

bool VirtualPianoPlayer::isTrackEnabled(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(trackMuted.size()) ||
        trackIndex >= static_cast<int>(trackSoloed.size()))
        return false;
    bool anySolo = false;
    for (const auto& s : trackSoloed) {
        if (s && s->load(std::memory_order_relaxed)) {
            anySolo = true;
            break;
        }
    }
    if (!anySolo)
        return (trackMuted[trackIndex] && !trackMuted[trackIndex]->load(std::memory_order_relaxed));
    else
        return (trackSoloed[trackIndex] && trackSoloed[trackIndex]->load(std::memory_order_relaxed));
}

void VirtualPianoPlayer::handle_sustain_event(const NoteEvent& event) {
    bool pedal_on = (event.sustainValue >= g_sustainCutoff);
    switch (currentSustainMode) {
    case SustainMode::IG:
        return;
    case SustainMode::SPACE_DOWN:
        if (pedal_on && !isSustainPressed) {
            pressKey(sustain_key_code);
            isSustainPressed = true;
        }
        else if (!pedal_on && isSustainPressed) {
            releaseKey(sustain_key_code);
            isSustainPressed = false;
        }
        break;
    case SustainMode::SPACE_UP:
        if (!pedal_on && isSustainPressed) {
            releaseKey(sustain_key_code);
            isSustainPressed = false;
        }
        else if (pedal_on && !isSustainPressed) {
            pressKey(sustain_key_code);
            isSustainPressed = true;
        }
        break;
    }
}

void VirtualPianoPlayer::set_track_mute(size_t trackIndex, bool mute) {
    if (trackIndex < trackMuted.size() && trackMuted[trackIndex]) {
        trackMuted[trackIndex]->store(mute, std::memory_order_relaxed);
        std::cout << "[TRACK] Track " << trackIndex+1 << (mute ? " muted.\n" : " unmuted.\n");
    }
    else {
        std::cout << "Invalid track index: " << trackIndex+1 << "\n";
    }
}

void VirtualPianoPlayer::set_track_solo(size_t trackIndex, bool solo) {
    if (trackIndex < trackSoloed.size() && trackSoloed[trackIndex]) {
        trackSoloed[trackIndex]->store(solo, std::memory_order_relaxed);
        std::cout << "[TRACK] Track " << trackIndex+1 << (solo ? " soloed.\n" : " unsoloed.\n");
    }
    else {
        std::cout << "Invalid track index: " << trackIndex+1 << "\n";
    }
}

void VirtualPianoPlayer::toggle_play_pause() {
    bool was_paused = paused.exchange(!paused, std::memory_order_acq_rel);
    double totalSec = note_buffer.empty() ? 0.0 : static_cast<double>(note_buffer.back()->time.count()) / 1e9;
    release_all_keys();

    if (!was_paused) {
        auto now = std::chrono::steady_clock::now();
        total_adjusted_time += std::chrono::duration_cast<std::chrono::nanoseconds>((now - last_resume_time) * current_speed);
        last_resume_time = now;
        double pausedTime = std::max(0.0, total_adjusted_time.count() / 1e9);
        std::cout << std::fixed << std::setprecision(2)
            << "[PAUSE] speed x" << current_speed << " at " << pausedTime << "s / " << totalSec << "s total\n";
    }
    else {
        if (!playback_started.load(std::memory_order_acquire)) {
            playback_started.store(true, std::memory_order_release);
            playback_start_time = std::chrono::steady_clock::now();
            last_resume_time = playback_start_time;
            buffer_index.store(0, std::memory_order_release);
            if (!playback_thread || !playback_thread->joinable()) {
                playback_thread = std::make_unique<std::jthread>(&VirtualPianoPlayer::play_notes, this);
            }
        }
        else {
            last_resume_time = std::chrono::steady_clock::now();
        }
        double resumeTime = std::max(0.0, total_adjusted_time.count() / 1e9);
        std::cout << std::fixed << std::setprecision(2)
            << "[RESUME] speed x" << current_speed << " at " << resumeTime << "s / " << totalSec << "s total\n";
    }
    SetEvent(command_event);
}

void VirtualPianoPlayer::skip(std::chrono::seconds duration) {
    if (!playback_started.load(std::memory_order_acquire)) {
        playback_start_time += duration;
        last_resume_time = playback_start_time;
        constexpr auto initialBuffer = std::chrono::milliseconds(50);
        total_adjusted_time = -initialBuffer;
        std::cout << "[SKIP] Not started; adjusting start time with buffer.\n";
        return;
    }
    double cur = get_adjusted_time().count() / 1e9;
    double tot = note_buffer.empty() ? 0.0 : static_cast<double>(note_buffer.back()->time.count()) / 1e9;
    std::cout << "[SKIP] forward " << duration.count() << "s from " << cur << " / " << tot << "\n";
    playback_control.requestSkip(duration);
    SetEvent(command_event);
}

void VirtualPianoPlayer::rewind(std::chrono::seconds duration) {
    if (!playback_started.load(std::memory_order_acquire)) {
        constexpr auto initialBuffer = std::chrono::milliseconds(50);
        playback_start_time -= duration;
        last_resume_time = playback_start_time;
        total_adjusted_time = -initialBuffer;
        std::cout << "[REWIND] Not started; adjusting start time with buffer.\n";
        return;
    }
    double cur = get_adjusted_time().count() / 1e9;
    double tot = note_buffer.empty() ? 0.0 : static_cast<double>(note_buffer.back()->time.count()) / 1e9;
    std::cout << "[REWIND] back " << duration.count() << "s from " << cur << " / " << tot << "\n";
    playback_control.requestRewind(duration);
    SetEvent(command_event);
}

size_t VirtualPianoPlayer::find_next_event_index(const std::chrono::nanoseconds& target_time) {
    const size_t sz = note_buffer.size();
    if (sz == 0) return 0;
    if (sz <= 16) {
        for (size_t i = 0; i < sz; ++i) {
            if (note_buffer[i]->time >= target_time)
                return i;
        }
        return sz;
    }
    const int64_t tgt = target_time.count();
    size_t lo = 0, hi = sz;
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        int64_t mid_time = note_buffer[mid]->time.count();
        if (mid_time < tgt)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void VirtualPianoPlayer::toggleSustainMode() {
    switch (currentSustainMode) {
    case SustainMode::IG:
        currentSustainMode = SustainMode::SPACE_DOWN;
        std::cout << "[SUSTAIN] DOWN cutoff=" << g_sustainCutoff << "\n";
        break;
    case SustainMode::SPACE_DOWN:
        currentSustainMode = SustainMode::SPACE_UP;
        std::cout << "[SUSTAIN] UP cutoff=" << g_sustainCutoff << "\n";
        break;
    case SustainMode::SPACE_UP:
        currentSustainMode = SustainMode::IG;
        std::cout << "[SUSTAIN] IGNORE\n";
        break;
    }
}

void VirtualPianoPlayer::release_all_keys() {
    if (isSustainPressed) {
        releaseKey(sustain_key_code);
        isSustainPressed = false;
    }
    const auto& mappings = (eightyEightKeyModeActive ? full_key_mappings : limited_key_mappings);
    for (auto& [note, key] : mappings) {
        KeyPress(key, false);
    }
    for (auto& [n, pressed] : pressed_keys) {
        pressed.store(false, std::memory_order_relaxed);
    }
    releaseKey(VK_MENU);
    releaseKey(VK_CONTROL);
}

void VirtualPianoPlayer::reset_volume() {
    int diff = midi::Config::getInstance().volume.INITIAL_VOLUME - current_volume.load(std::memory_order_relaxed);
    int steps = std::abs(diff) / midi::Config::getInstance().volume.VOLUME_STEP;
    WORD sc = (diff > 0) ? volume_up_key_code : volume_down_key_code;
    for (int i = 0; i < steps; ++i) {
        arrowsend(sc, true);
    }
    current_volume.store(midi::Config::getInstance().volume.INITIAL_VOLUME, std::memory_order_relaxed);
}

void VirtualPianoPlayer::restart_song() {
    try {
        if (isSustainPressed) {
            releaseKey(sustain_key_code);
            isSustainPressed = false;
        }
        should_stop.store(true, std::memory_order_release);
        SetEvent(command_event);
        if (playback_thread && playback_thread->joinable()) {
            auto future = std::async(std::launch::async, [this] { playback_thread->join(); });
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                std::cerr << "[RESTART] Warning: Playback thread did not join in time. Detaching.\n";
                playback_thread->detach();
            }
        }
        processing_pool.clear_tasks();
        playback_started.store(false, std::memory_order_release);
        paused.store(false, std::memory_order_release);
        constexpr auto initialBuffer = std::chrono::milliseconds(50);
        total_adjusted_time = -initialBuffer;
        current_speed = 1.0;
        buffer_index.store(0, std::memory_order_release);
        release_all_keys();
        auto now = std::chrono::steady_clock::now();
        playback_start_time = now;
        last_resume_time = now;
        should_stop.store(false, std::memory_order_release);
        playback_thread = std::make_unique<std::jthread>(&VirtualPianoPlayer::play_notes, this);
        std::cout << "[RESTART] Done.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[RESTART] Error: " << e.what() << "\n";
    }
}

void VirtualPianoPlayer::KeyPress(std::string_view key, bool press) {
    std::string keyStr(key);
    auto it = g_keyCache.find(keyStr);
    if (it == g_keyCache.end()) {
        KeySequence seq = computeKeySequence(keyStr);
        it = g_keyCache.emplace(std::move(keyStr), std::move(seq)).first;
    }
    const auto& seq = it->second;
    const auto& events = press ? seq.events_press : seq.events_release;
    if (!events.empty()) {
        NtUserSendInputCall(static_cast<UINT>(events.size()),
            const_cast<INPUT*>(events.data()),
            sizeof(INPUT));
    }
}

int VirtualPianoPlayer::stringToVK(std::string_view keyName) {
    static const std::unordered_map<std::string, int> keyMap = {
        {"ENTER",VK_RETURN},{"ESC",VK_ESCAPE},{"SPACE",VK_SPACE},{"UP",VK_UP},
        {"DOWN",VK_DOWN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},{"CAPS",VK_CAPITAL},
        {"SHIFT",VK_SHIFT},{"CTRL",VK_CONTROL},{"ALT",VK_MENU},{"TAB",VK_TAB},
        {"F1",VK_F1},{"F2",VK_F2},{"F3",VK_F3},{"F4",VK_F4},{"F5",VK_F5},
        {"F6",VK_F6},{"F7",VK_F7},{"F8",VK_F8},{"F9",VK_F9},{"F10",VK_F10},
        {"F11",VK_F11},{"F12",VK_F12},{"PAUSE",VK_PAUSE},{"BACK",VK_BACK},
        {"DELETE",VK_DELETE},{"HOME",VK_HOME},{"END",VK_END},{"INSERT",VK_INSERT},
        {"PGUP",VK_PRIOR},{"PGDN",VK_NEXT},{"NUMLOCK",VK_NUMLOCK},{"SCROLL",VK_SCROLL},
        {"PRTSC",VK_SNAPSHOT},{"APPS",VK_APPS},
        {"VOLUME_MUTE",VK_VOLUME_MUTE},{"VOLUME_DOWN",VK_VOLUME_DOWN},
        {"VOLUME_UP",VK_VOLUME_UP},{"MEDIA_NEXT",VK_MEDIA_NEXT_TRACK},
        {"MEDIA_PREV",VK_MEDIA_PREV_TRACK},{"MEDIA_PLAY",VK_MEDIA_PLAY_PAUSE},
    };
    std::string upper;
    upper.reserve(keyName.size());
    for (char c : keyName) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (upper.rfind("VK_", 0) == 0) {
        upper.erase(0, 3);
    }
    else if (upper.rfind("VK", 0) == 0) {
        upper.erase(0, 2);
    }
    auto it = keyMap.find(upper);
    if (it != keyMap.end())
        return it->second;
    if (upper.size() == 1) {
        char c = upper[0];
        if (std::isalnum(static_cast<unsigned char>(c))) {
            SHORT vk = VkKeyScanA(c);
            if (vk != -1) return LOBYTE(vk);
        }
    }
    try {
        int vkCode = std::stoi(upper);
        if (vkCode >= 0 && vkCode <= 255) return vkCode;
    }
    catch (...) {}
    throw std::runtime_error("Cannot map key '" + std::string(keyName) + "' to a VK code");
}

WORD VirtualPianoPlayer::vkToScanCode(int vk) {
    return static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
}

void VirtualPianoPlayer::sendVirtualKey(WORD vk, bool press) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
    if (!press)
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    NtUserSendInputCall(1, &in, sizeof(INPUT));
}

void VirtualPianoPlayer::pressKey(WORD vk) {
    sendVirtualKey(vk, true);
}

void VirtualPianoPlayer::releaseKey(WORD vk) {
    sendVirtualKey(vk, false);
}

void VirtualPianoPlayer::press_key(std::string_view note) noexcept {
    std::string actual = ENABLE_OUT_OF_RANGE_TRANSPOSE ? transpose_note(note) : std::string(note);
    const std::string& key = (eightyEightKeyModeActive ? full_key_mappings[actual] : limited_key_mappings[actual]);
    if (!key.empty()) {
        if (!pressed_keys[actual].exchange(true, std::memory_order_relaxed)) {
            KeyPress(key, true);
        }
        else {
            KeyPress(key, false);
            KeyPress(key, true);
        }
    }
}

void VirtualPianoPlayer::release_key(std::string_view note) noexcept {
    const std::string& key = (eightyEightKeyModeActive ? full_key_mappings[std::string(note)]
        : limited_key_mappings[std::string(note)]);
    if (!key.empty() && pressed_keys[std::string(note)].exchange(false, std::memory_order_relaxed)) {
        KeyPress(key, false);
    }
}

std::string VirtualPianoPlayer::transpose_note(std::string_view note) {
    int midi_n = note_name_to_midi(note);
    int transposed = (midi_n < 36) ? 36 + (midi_n % 12)
        : (midi_n > 96) ? 96 - (11 - (midi_n % 12))
        : midi_n;
    if (transposed > midi_n + 24)
        transposed = midi_n + 24;
    else if (transposed < midi_n - 24)
        transposed = midi_n - 24;
    return get_note_name(transposed);
}

int VirtualPianoPlayer::note_name_to_midi(std::string_view note_name) {
    static constexpr const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                               "F#", "G", "G#", "A", "A#", "B" };
    if (note_name.size() < 2) return 60;
    int octave = note_name.back() - '0';
    std::string_view pitch = note_name.substr(0, note_name.size() - 1);
    int idx = 0;
    for (int i = 0; i < 12; ++i) {
        if (pitch == NAMES[i]) { idx = i; break; }
    }
    return (octave + 1) * 12 + idx;
}

std::string VirtualPianoPlayer::get_note_name(int midi_note) {
    static constexpr const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                               "F#", "G", "G#", "A", "A#", "B" };
    int octave = (midi_note / 12) - 1;
    int pitch = midi_note % 12;
    if (pitch < 0 || pitch > 11) return "Unknown";
    return std::string(NAMES[pitch]) + std::to_string(octave);
}

std::string VirtualPianoPlayer::getVelocityCurveName(midi::VelocityCurveType curveType) {
    using VT = midi::VelocityCurveType;
    switch (curveType) {
    case VT::LinearCoarse:       return "Linear Coarse";
    case VT::LinearFine:         return "Linear Fine";
    case VT::ImprovedLowVolume:  return "Improved Low Volume";
    case VT::Logarithmic:        return "Logarithmic";
    case VT::Exponential:        return "Exponential";
    default:                     return "Unknown";
    }
}

void VirtualPianoPlayer::setVelocityCurveIndex(size_t index) {
    auto& cfg = midi::Config::getInstance();
    size_t mx = 5 + cfg.playback.customVelocityCurves.size();
    currentVelocityCurveIndex = std::min(index, mx);
}

std::string VirtualPianoPlayer::getVelocityKey(int targetVelocity) {
    static constexpr std::array<int, 32> builtinCurves[5] = {
        {4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,127},
        {2,6,10,14,18,22,26,30,34,38,42,46,50,54,58,62,66,70,74,78,82,86,90,94,98,102,106,110,114,118,122,127},
        {1,3,5,7,10,13,16,20,24,29,34,40,46,53,60,68,76,85,94,104,114,120,123,127,127,127,127,127,127,127,127,127},
        {1,2,3,5,7,10,14,19,25,32,40,49,60,72,85,99,115,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127},
        {1,2,4,8,16,24,32,40,48,56,64,72,80,88,96,104,110,115,120,124,126,127,127,127,127,127,127,127,127,127,127,127}
    };
    static constexpr char velocityKeys[] = "1234567890qwertyuiopasdfghjklzxc";
    const auto& config = midi::Config::getInstance();
    const auto& velocityTable = (currentVelocityCurveIndex < 5)
        ? builtinCurves[currentVelocityCurveIndex]
        : config.playback.customVelocityCurves[currentVelocityCurveIndex - 5].velocityValues;
    int idx = 0;
    while (idx < 32 && velocityTable[idx] < targetVelocity) {
        ++idx;
    }
    return std::string(1, velocityKeys[(idx < 32) ? idx : 31]);
}

void VirtualPianoPlayer::toggle_88_key_mode() {
    eightyEightKeyModeActive = !eightyEightKeyModeActive;
    std::cout << "[88-KEY MODE] " << (eightyEightKeyModeActive ? "Enabled" : "Disabled") << "\n";
}

void VirtualPianoPlayer::toggle_volume_adjustment() {
    bool newVal = !enable_volume_adjustment.load(std::memory_order_relaxed);
    enable_volume_adjustment.store(newVal, std::memory_order_relaxed);
    if (newVal) {
        max_volume = midi::Config::getInstance().volume.MAX_VOLUME;
        precompute_volume_adjustments();
        calibrate_volume();
        std::cout << "[AUTOVOL] On. Initial=" << midi::Config::getInstance().volume.INITIAL_VOLUME
            << "% Step=" << midi::Config::getInstance().volume.VOLUME_STEP
            << "% Max=" << midi::Config::getInstance().volume.MAX_VOLUME << "%\n";
    }
    else {
        std::cout << "[AUTOVOL] Off.\n";
    }
}

void VirtualPianoPlayer::toggle_velocity_keypress() {
    bool newVal = !enable_velocity_keypress.load(std::memory_order_relaxed);
    enable_velocity_keypress.store(newVal, std::memory_order_relaxed);
    std::cout << "[VELOCITY KEY] " << (newVal ? "Enabled" : "Disabled") << "\n";
    if (newVal) {
        std::cout << "[WARNING] This uses ALT+key combos. Make sure environment is correct.\n";
        std::cout << "[WARNING] Overlays or software that hook into ALT inputs may conflict; disabling them is recommended.\n";
    }
}

void VirtualPianoPlayer::precompute_volume_adjustments() {
    int currMaxVol = max_volume.load(std::memory_order_relaxed);
    for (int v = 0; v < 128; ++v) {
        double ratio = static_cast<double>(v) / 127.0;
        int tv = static_cast<int>(ratio * currMaxVol);
        int mn = std::min(midi::Config::getInstance().volume.MIN_VOLUME, currMaxVol);
        int mx = std::max(midi::Config::getInstance().volume.MIN_VOLUME, currMaxVol);
        tv = std::clamp(tv, mn, mx);
        tv = ((tv + 5) / 10) * 10;
        volume_lookup[v] = tv;
    }
}

void VirtualPianoPlayer::calibrate_volume() {
    int totalSteps = (midi::Config::getInstance().volume.MAX_VOLUME - midi::Config::getInstance().volume.MIN_VOLUME)
        / midi::Config::getInstance().volume.VOLUME_STEP;
    for (int i = 0; i < totalSteps; ++i) {
        arrowsend(volume_down_key_code, true);
    }
    int stepsToInit = (midi::Config::getInstance().volume.INITIAL_VOLUME - midi::Config::getInstance().volume.MIN_VOLUME)
        / midi::Config::getInstance().volume.VOLUME_STEP;
    for (int i = 0; i < stepsToInit; ++i) {
        arrowsend(volume_up_key_code, true);
    }
    current_volume.store(midi::Config::getInstance().volume.INITIAL_VOLUME, std::memory_order_relaxed);
}

void VirtualPianoPlayer::AdjustVolumeBasedOnVelocity(int velocity) noexcept {
    if (velocity < 0 || velocity >= static_cast<int>(volume_lookup.size()))
        return;
    int target_vol = volume_lookup[velocity];
    int current_vol = current_volume.load(std::memory_order_relaxed);
    int diff = target_vol - current_vol;
    int step_size = midi::Config::getInstance().volume.VOLUME_STEP;
    if (std::abs(diff) >= step_size) {
        WORD sc = (diff > 0) ? volume_up_key_code : volume_down_key_code;
        int steps = std::abs(diff) / step_size;
        for (int i = 0; i < steps; ++i) {
            arrowsend(sc, true);
        }
        current_volume.store(target_vol, std::memory_order_relaxed);
    }
}

void VirtualPianoPlayer::toggle_out_of_range_transpose() {
    ENABLE_OUT_OF_RANGE_TRANSPOSE = !ENABLE_OUT_OF_RANGE_TRANSPOSE;
    std::cout << "[61-KEY TRANSPOSE] " << (ENABLE_OUT_OF_RANGE_TRANSPOSE ? "Enabled" : "Disabled") << "\n";
}

int VirtualPianoPlayer::toggle_transpose_adjustment() {
    auto [notes, durations] = transposeEngine.extractNotesAndDurations(midi_file);
    if (notes.empty()) {
        std::cout << "[TRANSPOSE] No notes.\n";
        return 0;
    }
    std::string key = transposeEngine.estimateKey(notes, durations);
    std::string genre = transposeEngine.detectGenre(midi_file);
    std::cout << "Detected key: " << key << "\nDetected genre: " << genre << "\n";
    int best = transposeEngine.findBestTranspose(notes, durations, key, genre);
    return best;
}

std::string getTrackName(const MidiTrack& track) {
    if (!track.name.empty())
        return track.name;
    for (const auto& evt : track.events) {
        if (evt.status == 0xFF && evt.data1 == 0x03 && !evt.metaData.empty())
            return std::string(evt.metaData.begin(), evt.metaData.end());
    }
    return "(no name)";
}

double computeDrumConfidence(const MidiTrack& track) {
    double confidence = 0.0;
    std::string trackName = track.name;
    if (trackName.empty()) {
        for (const auto& evt : track.events) {
            if (evt.status == 0xFF && evt.data1 == 0x03 && !evt.metaData.empty()) {
                trackName = std::string(evt.metaData.begin(), evt.metaData.end());
                break;
            }
        }
    }
    if (!trackName.empty()) {
        std::string lowerName = trackName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        const std::vector<std::string> drumKeywords = {
            "drum", "drums", "ezdrummer", "addictive drums",
            "superior drummer", "bfd", "drum kit", "drumkit", "percussion"
        };
        for (const auto& keyword : drumKeywords) {
            if (lowerName.find(keyword) != std::string::npos) {
                confidence += 1.0;
                break;
            }
        }
    }
    int totalNotes = 0, minPitch = INT_MAX, maxPitch = 0, sumPitch = 0;
    std::unordered_map<int, int> pitchHistogram;
    std::set<int> channels;
    int noteOnCount = 0, noteOffCount = 0;
    uint32_t firstTick = UINT_MAX, lastTick = 0;
    for (const auto& evt : track.events) {
        uint8_t status = evt.status;
        if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80) {
            totalNotes++;
            if ((status & 0xF0) == 0x90)
                noteOnCount++;
            else
                noteOffCount++;
            int pitch = evt.data1;
            sumPitch += pitch;
            pitchHistogram[pitch]++;
            firstTick = std::min(firstTick, evt.absoluteTick);
            lastTick = std::max(lastTick, evt.absoluteTick);
            channels.insert(status & 0x0F);
            minPitch = std::min(minPitch, pitch);
            maxPitch = std::max(maxPitch, pitch);
        }
    }
    if (totalNotes == 0)
        return confidence;
    double noteOffRatio = static_cast<double>(noteOffCount) / totalNotes;
    confidence += (noteOffRatio < 0.1) ? 0.1 : (noteOffRatio > 0.3 ? -0.1 : 0.0);
    int range = maxPitch - minPitch;
    confidence += (range < 20) ? 0.3 : (range < 30 ? 0.2 : 0.0);
    int maxCount = 0;
    for (const auto& p : pitchHistogram)
        maxCount = std::max(maxCount, p.second);
    double dominantRatio = static_cast<double>(maxCount) / totalNotes;
    confidence += (dominantRatio > 0.7) ? 0.2 : (dominantRatio > 0.5 ? 0.1 : 0.0);
    double avgPitch = static_cast<double>(sumPitch) / totalNotes;
    if (avgPitch >= 35 && avgPitch <= 81)
        confidence += 0.1;
    if (channels.size() == 1 && *channels.begin() == 9)
        confidence += 0.5;
    if (lastTick > firstTick) {
        double density = static_cast<double>(totalNotes) / (lastTick - firstTick + 1);
        if (density > 0.03) confidence += 0.05;
        if (density > 0.05) confidence += 0.05;
        if (density > 0.1)  confidence += 0.05;
    }
    std::vector<int> velocities;
    for (const auto& evt : track.events) {
        if (((evt.status & 0xF0) == 0x90) && evt.data2 > 0)
            velocities.push_back(evt.data2);
    }
    if (!velocities.empty()) {
        double sumVel = 0.0;
        for (int v : velocities)
            sumVel += v;
        double meanVel = sumVel / velocities.size();
        double variance = 0.0;
        for (int v : velocities) {
            double diff = v - meanVel;
            variance += diff * diff;
        }
        variance /= velocities.size();
        double stdev = std::sqrt(variance);
        if (stdev < 10)
            confidence += 0.05;
    }
    return confidence;
}

void VirtualPianoPlayer::process_tracks(const MidiFile& mid) {
    note_events.clear();
    tempo_changes.clear();
    timeSignatures.clear();
    string_storage.clear();

    bool smpte = (mid.division & 0x8000) != 0;
    struct TempoPoint { uint64_t tick; uint64_t tempo; };
    std::vector<TempoPoint> tempo_points;
    if (!smpte) {
        tempo_points.push_back({ 0, 500000ULL });
    }
    bool filterDrums = midi::Config::getInstance().midi.FILTER_DRUMS;
    if (filterDrums) {
        drum_flags.clear();
        drum_flags.resize(mid.tracks.size(), false);
        for (size_t i = 0; i < mid.tracks.size(); ++i) {
            double conf = computeDrumConfidence(mid.tracks[i]);
            double clampedConf = (conf > 1.0) ? 1.0 : conf;
            double threshold = (!getTrackName(mid.tracks[i]).empty()) ? 0.8 : 0.9;
            if (clampedConf >= threshold) {
                drum_flags[i] = true;
                std::string trackName = getTrackName(mid.tracks[i]);
                std::cout << "[DRUMS] Track #" << i << " \"" << trackName
                    << "\" flagged as drums (confidence: " << std::fixed << std::setprecision(1)
                    << clampedConf * 100 << "%, raw: " << conf << ")\n";
            }
        }
    }
    std::chrono::nanoseconds current_time_ns(0);
    std::vector<std::tuple<uint64_t, int, MidiEvent>> all_events;
    all_events.reserve(100000);
    for (int trackIndex = 0; trackIndex < static_cast<int>(mid.tracks.size()); ++trackIndex) {
        for (auto& evt : mid.tracks[trackIndex].events) {
            all_events.push_back({ evt.absoluteTick, trackIndex, evt });
        }
    }
    std::sort(all_events.begin(), all_events.end(),
        [](auto& a, auto& b) { return std::get<0>(a) < std::get<0>(b); });
    size_t totalEvents = all_events.size();
    string_storage.reserve(totalEvents * 3);
    std::unordered_map<int, std::unordered_map<int, std::vector<std::chrono::nanoseconds>>> active_notes;

    auto tick2ns = [&](uint64_t st, uint64_t en) {
        if (smpte) {
            int8_t fps_val = static_cast<int8_t>(mid.division >> 8);
            int fps = -fps_val;
            uint8_t tpf = static_cast<uint8_t>(mid.division & 0xFF);
            uint64_t nspt = 1000000000ULL / (fps * tpf);
            return std::chrono::nanoseconds((en - st) * nspt);
        }
        else {
            std::chrono::nanoseconds total_ns(0);
            size_t tempo_idx = 0;
            while (tempo_idx < tempo_points.size() - 1 && tempo_points[tempo_idx + 1].tick <= st) {
                ++tempo_idx;
            }
            uint64_t current_tick = st;
            while (current_tick < en) {
                uint64_t nxt = (tempo_idx < tempo_points.size() - 1) ? tempo_points[tempo_idx + 1].tick : en;
                uint64_t seg = std::min(en - current_tick, nxt - current_tick);
                uint64_t nspt = (tempo_points[tempo_idx].tempo * 1000ULL) / mid.division;
                total_ns += std::chrono::nanoseconds(seg * nspt);
                current_tick += seg;
                if (current_tick >= nxt && tempo_idx < tempo_points.size() - 1)
                    ++tempo_idx;
            }
            return total_ns;
        }
        };

    auto close_active_notes = [&](std::chrono::nanoseconds ctime) {
        using NH = midi::NoteHandlingMode;
        if (midi::Config::getInstance().playback.noteHandlingMode == NH::NoHandling) {
            active_notes.clear();
            return;
        }
        for (auto& [ch, notemap] : active_notes) {
            for (auto& [note, starts] : notemap) {
                while (!starts.empty()) {
                    add_note_event(ctime, "C0", "release", 0, -1);
                    if (midi::Config::getInstance().playback.noteHandlingMode == NH::LIFO)
                        starts.pop_back();
                    else
                        starts.erase(starts.begin());
                }
            }
        }
        active_notes.clear();
        };

    uint64_t current_tick = 0;
    for (auto& [tick, trackIdx, evt] : all_events) {
        auto delta_ns = tick2ns(current_tick, tick);
        current_time_ns += delta_ns;
        current_tick = tick;
        if (evt.status == 0xFF && evt.data1 == 0x51 && evt.metaData.size() == 3) {
            uint32_t tempo_val = ((uint8_t)evt.metaData[0] << 16) |
                ((uint8_t)evt.metaData[1] << 8) |
                ((uint8_t)evt.metaData[2]);
            if (!smpte)
                tempo_points.push_back({ tick, tempo_val });
            tempo_changes.push_back({ static_cast<double>(tick), static_cast<double>(tempo_val) });
        }
        else if (evt.status == 0xFF && evt.data1 == 0x58 && evt.metaData.size() == 4) {
            TimeSignature ts;
            ts.tick = tick;
            ts.numerator = evt.metaData[0];
            ts.denominator = static_cast<uint8_t>(1 << evt.metaData[1]);
            ts.clocksPerClick = evt.metaData[2];
            ts.thirtySecondNotesPerQuarter = evt.metaData[3];
            timeSignatures.push_back(ts);
        }
        else if ((evt.status & 0xF0) == 0xB0 && evt.data1 == 64) {
            add_sustain_event(current_time_ns, evt.status & 0x0F, evt.data2, trackIdx);
        }
        else if ((evt.status & 0xF0) == 0x90 || (evt.status & 0xF0) == 0x80) {
            int note = evt.data1;
            int channel = evt.status & 0x0F;
            int velocity = evt.data2;
            if ((evt.status & 0xF0) == 0x90 && velocity > 0)
                handle_note_on(current_time_ns, channel, note, velocity, trackIdx, active_notes);
            else
                handle_note_off(current_time_ns, channel, note, velocity, trackIdx, active_notes);
        }
    }
    close_active_notes(current_time_ns);
    std::stable_sort(note_events.begin(), note_events.end(),
        [](auto& a, auto& b) { return a.time < b.time; });
}

void VirtualPianoPlayer::handle_note_off(std::chrono::nanoseconds ctime,
    int ch, int note, int vel, int trackIndex,
    std::unordered_map<int, std::unordered_map<int, std::vector<std::chrono::nanoseconds>>>& active_notes)
{
    std::string nn = get_note_name(note);
    using NH = midi::NoteHandlingMode;
    auto mode = midi::Config::getInstance().playback.noteHandlingMode;
    if (mode == NH::NoHandling) {
        string_storage.push_back(nn);
        string_storage.push_back("release");
        note_events.push_back({ ctime,
                               std::string_view(string_storage[string_storage.size() - 2]),
                               std::string_view(string_storage[string_storage.size() - 1]),
                               vel, trackIndex });
        return;
    }
    auto itCh = active_notes.find(ch);
    if (itCh != active_notes.end()) {
        auto& noteMap = itCh->second;
        auto itN = noteMap.find(note);
        if (itN != noteMap.end() && !itN->second.empty()) {
            string_storage.push_back(nn);
            string_storage.push_back("release");
            note_events.push_back({ ctime,
                                   std::string_view(string_storage[string_storage.size() - 2]),
                                   std::string_view(string_storage[string_storage.size() - 1]),
                                   vel, trackIndex });
            if (mode == NH::LIFO)
                itN->second.pop_back();
            else
                itN->second.erase(itN->second.begin());
        }
    }
}

void VirtualPianoPlayer::handle_note_on(std::chrono::nanoseconds ctime,
    int ch, int note, int vel, int trackIndex,
    std::unordered_map<int, std::unordered_map<int, std::vector<std::chrono::nanoseconds>>>& active_notes)
{
    std::string nm = get_note_name(note);
    active_notes[ch][note].push_back(ctime);
    string_storage.push_back(nm);
    string_storage.push_back("press");
    note_events.push_back({ ctime,
                           std::string_view(string_storage[string_storage.size() - 2]),
                           std::string_view(string_storage[string_storage.size() - 1]),
                           vel, trackIndex });
}

void VirtualPianoPlayer::add_sustain_event(std::chrono::nanoseconds time, int channel, int sustainValue, int trackIndex) {
    string_storage.push_back("sustain");
    string_storage.push_back(sustainValue >= g_sustainCutoff ? "press" : "release");
    note_events.push_back({ time,
                           std::string_view(string_storage[string_storage.size() - 2]),
                           std::string_view(string_storage[string_storage.size() - 1]),
                           (sustainValue | (channel << 8)), trackIndex });
}

void VirtualPianoPlayer::add_note_event(std::chrono::nanoseconds time,
    std::string_view note,
    std::string_view action,
    int velocity,
    int trackIndex)
{
    string_storage.push_back(std::string(note));
    string_storage.push_back(std::string(action));
    note_events.push_back({ time,
                           std::string_view(string_storage[string_storage.size() - 2]),
                           std::string_view(string_storage[string_storage.size() - 1]),
                           velocity, trackIndex });
}

void VirtualPianoPlayer::speed_up() {
    adjust_playback_speed(1.1);
}

void VirtualPianoPlayer::slow_down() {
    adjust_playback_speed(1.0 / 1.1);
}

void VirtualPianoPlayer::adjust_playback_speed(double factor) {
    auto now = std::chrono::steady_clock::now();
    if (!playback_started.load(std::memory_order_relaxed)) {
        playback_start_time = now;
        last_resume_time = now;
    }
    else {
        total_adjusted_time += std::chrono::duration_cast<std::chrono::nanoseconds>((now - last_resume_time) * current_speed);
        last_resume_time = now;
    }
    current_speed *= factor;
    current_speed = std::clamp(current_speed, 0.25, 2.0);
    if (std::fabs(current_speed - 1.0) < 0.05)
        current_speed = 1.0;
    std::cout << "[SPEED] x" << current_speed << "\n";
}

void VirtualPianoPlayer::arrowsend(WORD sc, bool extended) {
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = sc;
    in[0].ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wScan = sc;
    in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    NtUserSendInputCall(2, in, sizeof(INPUT));
}
