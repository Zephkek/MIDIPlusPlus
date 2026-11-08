#include "Player.hpp"
#include "LiveMidi.hpp"
#include "LegitWindow.hpp"
#include "SplashScreen.h"
#include "TrackControl.hpp"
#include "Transpose.h"
#include "VelocityCurveEditor.hpp"
#include "Widgets.hpp"
#include "resource.h"

#include <CommCtrl.h>
#include <GdiPlus.h>
#include <dbt.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

namespace {

    constexpr wchar_t kClass[] = L"MIDI++";
    constexpr wchar_t kTitle[] = L"MIDI++ v1.0.6";
    constexpr UINT WM_APP_LOG = WM_APP + 101;
    constexpr UINT WM_APP_PLAYER = WM_APP + 102;
    constexpr UINT WM_APP_HOTKEY = WM_APP + 103;
    constexpr UINT WM_APP_LOADED = WM_APP + 104;
    constexpr UINT_PTR IDT_FRAME = 1;
    constexpr UINT_PTR IDT_STATS = 2;
    constexpr UINT_PTR IDT_DEVICES = 3;

    enum : int {
        ID_SEARCH = 101, ID_CB_SORT, ID_BTN_REFRESH, ID_LB_MIDI, ID_BTN_PREV, ID_BTN_NEXT, ID_CHK_SHUFFLE,
        ID_SEEK, ID_BTN_LOAD, ID_BTN_PLAY, ID_BTN_RESTART, ID_BTN_REW, ID_BTN_SKIP, ID_BTN_SPEEDDN, ID_BTN_SPEEDUP,
        ID_BTN_OUTPUT, ID_BTN_88KEY, ID_BTN_TRANSPOSEOUT, ID_BTN_VELOCITY, ID_CB_VELOCITY_CURVE, ID_BTN_VLCURVE,
        ID_BTN_SUSTAIN, ID_SLIDER_SUSTAIN, ID_BTN_VOLADJ, ID_CB_BATCH, ID_BTN_LEGIT, ID_BTN_TRANSPOSE,
        ID_CB_MIDIDEV, ID_CB_MIDICH, ID_BTN_MIDI2QWERTY, ID_BTN_MIDICONNECT, ID_BTN_REFRESH_MIDI,
        ID_CHK_GAME, ID_CHK_TOP, ID_CHK_DARK, ID_SLIDER_OPACITY, ID_BTN_FOLDER, ID_BTN_STOP,
        ID_EDIT_LOG, ID_BTN_CLEARLOG, ID_BTN_KEYDN, ID_BTN_KEYUP,
    };

    enum Hotkey : WPARAM { HK_PLAY, HK_REWIND, HK_SKIP, HK_STOP };

    constexpr int kBatchMs[] = { -1, 0, 1, 2, 4, 8, 16 };

    struct Item {
        std::wstring name;
        fs::path path;
        bool folder = false;
        bool favorite = false;
        std::time_t written = 0;
    };

    struct Loaded {
        uint64_t seq = 0;
        fs::path path;
        std::shared_ptr<Song> song;
        std::string error;
        double ms = 0.0;
        bool autoplay = false;
        int64_t keep = -1;
    };

    struct Panel {
        RECT r{};
        const wchar_t* title = L"";
    };

    struct App {
        HINSTANCE inst = nullptr;
        HWND wnd = nullptr;
        int dpi = 96;
        HBRUSH brBg = nullptr, brPanel = nullptr, brField = nullptr, brLog = nullptr;
        HWND search = nullptr, sortCombo = nullptr, list = nullptr;
        HWND speed = nullptr, tempoLabel = nullptr, keyLabel = nullptr, keyValue = nullptr;
        HWND curveCombo = nullptr, batchCombo = nullptr, cutoffValue = nullptr;
        HWND devCombo = nullptr, chanCombo = nullptr, latency = nullptr, opacityLabel = nullptr, log = nullptr;
        ui::Slider seek, cutoff, opacity;
        ui::PianoView piano;
        ui::Visualizer visual;
        ui::Display lcd;
        ui::StatusBar status;
        TrackControl tracks;
        std::vector<Panel> panels;
        std::vector<RECT> frames;
        std::vector<RECT> screens;
        std::vector<TrackControl::TrackInfo> songRows;
        bool liveRows = false;
        bool awayNoted = false;
        int key = 0;
        int songNo = 0, songTotal = 0;
        Player player;
        std::shared_ptr<const Song> song;
        std::thread loader;
        std::atomic<bool> cancelLoad{ false };
        std::atomic<int> loadProgress{ 0 };
        uint64_t loadSeq = 0;
        bool loading = false;
        fs::path loadingPath;
        fs::path dir = L"midi";
        std::vector<Item> items;
        std::vector<int> shown;
        std::vector<live::Device> devices;
        HWND lastExternal = nullptr;
        HWINEVENTHOOK hook = nullptr;
        std::thread hotkeys;
        std::atomic<bool> quit{ false };
        bool shuffle = false;
        bool connectBegun = false;
        bool freshSong = false;
        bool wasPlaying = false;
        int gameTranspose = 0;
        int shownSecond = -1;
        int away = 0;
        uint64_t lastSent = 0, lastNotes = 0;
        std::chrono::steady_clock::time_point lastStats = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
        double notesPerSec = 0.0, keysPerSec = 0.0;
        Player::Stats timing;
        live::Latency live;
    };

    App g;
    std::mutex g_logMutex;
    std::wstring g_logPending;
    std::atomic<bool> g_logPosted{ false };

    midi::Config& cfg() { return midi::Config::getInstance(); }
    int S(int v) { return MulDiv(v, g.dpi, 96); }
    void start_playing(bool restart);

    std::wstring widen(const std::string& s) {
        if (s.empty())
            return {};
        UINT cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) ? CP_UTF8 : CP_ACP;
        int n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::string narrow(const std::wstring& w) {
        if (w.empty())
            return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring thousands(uint64_t v) {
        std::wstring s = std::to_wstring(v);
        for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
            s.insert(static_cast<size_t>(i), L",");
        return s;
    }

    void say(const char* fmt, ...) {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ts[16];
        swprintf_s(ts, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        std::wstring line = ts + widen(buf) + L"\r\n";
        {
            std::lock_guard l(g_logMutex);
            g_logPending += line;
        }
        if (g.wnd && !g_logPosted.exchange(true))
            PostMessageW(g.wnd, WM_APP_LOG, 0, 0);
    }

    void flush_log() {
        g_logPosted = false;
        std::wstring text;
        {
            std::lock_guard l(g_logMutex);
            text.swap(g_logPending);
        }
        if (text.empty() || !g.log)
            return;
        int len = GetWindowTextLengthW(g.log);
        if (len + static_cast<int>(text.size()) > 200000) {
            SendMessageW(g.log, EM_SETSEL, 0, len / 2);
            SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
            len = GetWindowTextLengthW(g.log);
        }
        SendMessageW(g.log, EM_SETSEL, len, len);
        SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(g.log, EM_SCROLLCARET, 0, 0);
    }

    void save_config() {
        try {
            cfg().saveToFile("config.json");
        }
        catch (const std::exception& e) {
            say("[Config] %s", e.what());
        }
    }

    std::string window_title(HWND h) {
        wchar_t t[256] = {};
        GetWindowTextW(h, t, 256);
        std::string s = narrow(t);
        return s.empty() ? "the game window" : s;
    }

    bool ours(HWND h) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        return pid == GetCurrentProcessId();
    }

    HWND root(HWND h) {
        HWND r = h ? GetAncestor(h, GA_ROOTOWNER) : nullptr;
        return r ? r : h;
    }

    HWND find_roblox() {
        HWND h = FindWindowW(nullptr, L"Roblox");
        return h && IsWindowVisible(h) ? h : nullptr;
    }

    HWND pick_target() {
        HWND fg = root(GetForegroundWindow());
        if (fg && !ours(fg) && IsWindowVisible(fg))
            return fg;
        if (HWND rb = find_roblox())
            return rb;
        if (g.lastExternal && IsWindow(g.lastExternal) && IsWindowVisible(g.lastExternal))
            return g.lastExternal;
        return nullptr;
    }

    DWORD token_integrity(HANDLE token, bool& ok) {
        ok = false;
        DWORD n = 0;
        GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &n);
        std::vector<BYTE> buf(n);
        if (!n || !GetTokenInformation(token, TokenIntegrityLevel, buf.data(), n, &n))
            return 0;
        auto* tml = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf.data());
        ok = true;
        return *GetSidSubAuthority(tml->Label.Sid, *GetSidSubAuthorityCount(tml->Label.Sid) - 1);
    }

    void check_elevation(HWND target) {
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        HANDLE self = nullptr;
        bool selfOk = false, otherOk = false, denied = false;
        DWORD mine = 0, theirs = 0;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &self)) {
            mine = token_integrity(self, selfOk);
            CloseHandle(self);
        }
        if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            HANDLE tok = nullptr;
            if (OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
                theirs = token_integrity(tok, otherOk);
                CloseHandle(tok);
            }
            else {
                denied = GetLastError() == ERROR_ACCESS_DENIED;
            }
            CloseHandle(proc);
        }
        if (!selfOk || mine >= SECURITY_MANDATORY_HIGH_RID)
            return;
        if ((otherOk && theirs > mine) || (!otherOk && denied))
            say("[Keys] %s is running as administrator, so Windows blocks the keys MIDI++ sends to it. "
                "Start MIDI++ as administrator too.", window_title(target).c_str());
    }

    void aim(const char* what) {
        HWND t = cfg().playback.gameWindowOnly ? pick_target() : nullptr;
        input::set_guard(cfg().playback.gameWindowOnly);
        input::set_target(t);
        if (!t) {
            if (cfg().playback.gameWindowOnly)
                say("[Keys] No game window found. Click into the game and press %s.",
                    input::key_label(cfg().hotkeys.PLAY_PAUSE_KEY).c_str());
            return;
        }
        out::set_layout(GetKeyboardLayout(GetWindowThreadProcessId(t, nullptr)));
        if (IsIconic(t))
            ShowWindow(t, SW_RESTORE);
        if (root(GetForegroundWindow()) != t)
            SetForegroundWindow(t);
        say("[Keys] %s into: %s", what, window_title(t).c_str());
        check_elevation(t);
    }

    std::wstring fmt_time(int64_t ns) {
        int s = static_cast<int>(std::max<int64_t>(0, ns) / 1'000'000'000);
        wchar_t buf[32];
        if (s >= 3600)
            swprintf_s(buf, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
        else
            swprintf_s(buf, L"%d:%02d", s / 60, s % 60);
        return buf;
    }

    bool playing() { return g.player.playing(); }

    bool system_dark() {
        DWORD light = 1, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        return light == 0;
    }

    const wchar_t* output_label() {
        const auto& p = cfg().playback;
        return p.output == midi::OutputMode::MidiConnect ? L"Out: MidiConnect" : p.typedKeys ? L"Out: Typed" : L"Out: Keys";
    }

    bool is_on(int id) {
        const auto& p = cfg().playback;
        switch (id) {
        case ID_BTN_88KEY: return p.keys88;
        case ID_BTN_VOLADJ: return p.autoVolume;
        case ID_BTN_VELOCITY: return p.velocityKeys;
        case ID_BTN_SUSTAIN: return p.sustain != midi::SustainMode::Ignore;
        case ID_BTN_TRANSPOSEOUT: return p.outOfRange;
        case ID_BTN_LEGIT: return cfg().legit.ENABLED;
        case ID_BTN_MIDI2QWERTY: return live::active() && live::mode() == out::Mode::Keys;
        case ID_BTN_MIDICONNECT: return live::active() && live::mode() == out::Mode::MidiConnect;
        case ID_BTN_PLAY: return playing();
        case ID_CHK_SHUFFLE: return g.shuffle;
        case ID_CHK_GAME: return p.gameWindowOnly;
        case ID_CHK_TOP: return cfg().ui.alwaysOnTop;
        case ID_CHK_DARK: return ui::dark();
        default: return false;
        }
    }

    void redraw(int id) {
        InvalidateRect(GetDlgItem(g.wnd, id), nullptr, TRUE);
    }

    void update_focusability() {
        bool noActivate = live::active() || playing();
        bool now = playing();
        if (now != g.wasPlaying) {
            g.wasPlaying = now;
            SetDlgItemTextW(g.wnd, ID_BTN_PLAY, now ? L"Pause" : L"Play");
            redraw(ID_BTN_PLAY);
        }
        LONG ex = GetWindowLongW(g.wnd, GWL_EXSTYLE);
        if (((ex & WS_EX_NOACTIVATE) != 0) == noActivate)
            return;
        ex = noActivate ? (ex | WS_EX_NOACTIVATE) : (ex & ~WS_EX_NOACTIVATE);
        SetWindowLongW(g.wnd, GWL_EXSTYLE, ex);
        SetWindowPos(g.wnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    void draw_star(HDC hdc, int cx, int cy, int r, COLORREF c) {
        POINT pts[10];
        for (int i = 0; i < 10; ++i) {
            double a = -1.5707963 + i * 0.6283185;
            double rr = (i & 1) ? r * 0.45 : r;
            pts[i] = { cx + static_cast<LONG>(std::lround(std::cos(a) * rr)), cy + static_cast<LONG>(std::lround(std::sin(a) * rr)) };
        }
        HBRUSH b = CreateSolidBrush(c);
        HPEN pen = CreatePen(PS_SOLID, 1, c);
        HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, pen);
        Polygon(hdc, pts, 10);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(b);
        DeleteObject(pen);
    }

    void draw_folder(HDC hdc, int x, int y, int w, int h, COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        RECT tab{ x, y, x + w / 2, y + h / 4 + 1 };
        RECT body{ x, y + h / 5, x + w, y + h };
        FillRect(hdc, &tab, b);
        FillRect(hdc, &body, b);
        DeleteObject(b);
    }

    void draw_list_item(const DRAWITEMSTRUCT* dis) {
        const ui::Palette& p = ui::pal();
        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        const bool sel = (dis->itemState & ODS_SELECTED) != 0;
        SetDCBrushColor(hdc, sel ? p.select : p.field);
        FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (dis->itemID == static_cast<UINT>(-1) || dis->itemID >= g.shown.size())
            return;
        const Item& it = g.items[g.shown[dis->itemID]];
        const int icon = S(12), cy = (rc.top + rc.bottom) / 2, ix = rc.left + S(6);
        if (it.folder)
            draw_folder(hdc, ix, cy - icon / 2, icon, icon - S(1), p.folder);
        else if (it.favorite)
            draw_star(hdc, ix + icon / 2, cy, icon / 2 + S(1), p.star);
        RECT t = rc;
        t.left = ix + icon + S(6);
        t.right -= S(4);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, sel ? p.selectText : p.fieldText);
        HGDIOBJ old = SelectObject(hdc, ui::font(g.dpi, 12, FW_NORMAL));
        std::wstring name = it.name == L".." ? std::wstring(L"Back") : it.name;
        DrawTextW(hdc, name.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, old);
    }

    HWND control(const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0, DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font(g.dpi, 12, FW_NORMAL)), FALSE);
        return c;
    }

    HWND button(const wchar_t* text, int id) {
        return control(L"button", text, BS_OWNERDRAW, id);
    }

    void apply_fonts() {
        HFONT f = ui::font(g.dpi, 12, FW_NORMAL);
        for (HWND c = GetWindow(g.wnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), FALSE);
        SendMessageW(g.latency, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font(g.dpi, 11, FW_NORMAL)), FALSE);
        SendMessageW(g.log, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font(g.dpi, 12, FW_NORMAL, L"Consolas")), FALSE);
        SendMessageW(g.list, LB_SETITEMHEIGHT, 0, S(20));
        SendMessageW(g.search, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(4), S(4)));
        SendMessageW(g.log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(4), S(4)));
        for (ui::Slider* s : { &g.seek, &g.cutoff, &g.opacity })
            s->set_dpi(g.dpi);
        g.piano.set_dpi(g.dpi);
        g.visual.set_dpi(g.dpi);
        g.lcd.set_dpi(g.dpi);
        g.status.set_dpi(g.dpi);
        g.tracks.SetDpi(g.dpi);
    }

    void apply_theme() {
        int mode = cfg().ui.darkMode;
        ui::set_dark(mode < 0 ? system_dark() : mode == 1);
        const ui::Palette& p = ui::pal();
        for (HBRUSH* b : { &g.brBg, &g.brPanel, &g.brField, &g.brLog })
            if (*b)
                DeleteObject(*b);
        g.brBg = CreateSolidBrush(p.bg);
        g.brPanel = CreateSolidBrush(p.panel);
        g.brField = CreateSolidBrush(p.field);
        g.brLog = CreateSolidBrush(p.logBg);
        ui::theme_window(g.wnd);
        ui::theme_control(g.list, L"DarkMode_Explorer");
        ui::theme_control(g.log, L"DarkMode_Explorer");
        ui::theme_control(g.tracks.GetWindowHandle(), L"DarkMode_Explorer");
        ui::theme_control(g.search, L"DarkMode_CFD");
        for (HWND c : { g.sortCombo, g.curveCombo, g.batchCombo, g.devCombo, g.chanCombo })
            ui::theme_control(c, L"DarkMode_CFD");
        g.tracks.Restyle();
        RedrawWindow(g.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM owner) -> BOOL {
            if (GetWindow(w, GW_OWNER) == reinterpret_cast<HWND>(owner))
                SendMessageW(w, WM_THEMECHANGED, 0, 0);
            return TRUE;
        }, reinterpret_cast<LPARAM>(g.wnd));
    }

    void layout() {
        RECT rc;
        GetClientRect(g.wnd, &rc);
        const int W = rc.right, H = rc.bottom;
        if (W <= 0 || H <= 0)
            return;
        HDWP dw = BeginDeferWindowPos(64);
        auto put = [&](HWND h, int x, int y, int w, int hh) {
            if (h && dw)
                dw = DeferWindowPos(dw, h, nullptr, x, y, std::max(0, w), std::max(0, hh), SWP_NOZORDER | SWP_NOACTIVATE);
        };
        auto at = [&](int id, int x, int y, int w, int hh) { put(GetDlgItem(g.wnd, id), x, y, w, hh); };
        g.panels.clear();
        g.frames.clear();
        auto panel = [&](const wchar_t* title, int x, int y, int w, int hh) { g.panels.push_back({ { x, y, x + w, y + hh }, title }); };
        auto frame = [&](HWND h, int x, int y, int w, int hh) {
            put(h, x, y, w, hh);
            g.frames.push_back({ x - 1, y - 1, x + w + 1, y + hh + 1 });
        };

        const int m = S(8), gap = S(4), bh = S(24);
        const int top = S(334);
        const int lw = S(232);
        panel(L"Library", m, S(6), lw, top - S(6));
        const int lx = m + S(8), liw = lw - S(16);
        frame(g.search, lx, S(28), liw, S(22));
        put(g.sortCombo, lx, S(56), S(136), S(300));
        at(ID_BTN_REFRESH, lx + S(140), S(55), liw - S(140), bh);
        frame(g.list, lx, S(85), liw, S(207));
        at(ID_BTN_PREV, lx, S(300), S(50), bh);
        at(ID_BTN_NEXT, lx + S(54), S(300), S(50), bh);
        at(ID_CHK_SHUFFLE, lx + S(108), S(300), liw - S(108), bh);

        const int x0 = m + lw + S(8), rw = W - x0 - m;
        panel(L"Now Playing", x0, S(6), rw, S(158));
        const int nx = x0 + S(10), nw = rw - S(20);
        const int vw = S(196);
        g.screens.clear();
        g.screens.push_back({ nx, S(26), nx + nw, S(98) });
        put(g.lcd.hwnd(), nx + S(1), S(27), nw - vw - S(9), S(70));
        put(g.visual.hwnd(), nx + nw - vw - S(8), S(33), vw, S(58));
        const struct { int id; HWND h; int w; int after; } transport[] = {
            { ID_BTN_LOAD, nullptr, 46, 0 }, { ID_BTN_PLAY, nullptr, 54, 0 }, { ID_BTN_RESTART, nullptr, 54, 0 },
            { ID_BTN_REW, nullptr, 40, 0 }, { ID_BTN_SKIP, nullptr, 40, 10 },
            { 0, g.tempoLabel, 44, 0 }, { ID_BTN_SPEEDDN, nullptr, 24, 0 }, { 0, g.speed, 42, 0 }, { ID_BTN_SPEEDUP, nullptr, 24, 10 },
            { 0, g.keyLabel, 28, 0 }, { ID_BTN_KEYDN, nullptr, 24, 0 }, { 0, g.keyValue, 34, 0 }, { ID_BTN_KEYUP, nullptr, 24, 0 },
        };
        int bx = nx;
        for (const auto& t : transport) {
            if (t.id)
                at(t.id, bx, S(104), S(t.w), S(26));
            else
                put(t.h, bx, S(104), S(t.w), S(26));
            bx += S(t.w) + gap + S(t.after);
        }
        put(g.seek.hwnd(), nx - S(6), S(136), nw + S(12), S(24));

        const int gy = S(170), gh = top - gy;
        const int wOut = S(228), wLive = (rw - wOut - 2 * S(6)) / 2, wWin = rw - wOut - wLive - 2 * S(6);
        const int ox = x0, lvx = ox + wOut + S(6), wx = lvx + wLive + S(6);
        panel(L"Output", ox, gy, wOut, gh);
        panel(L"Live MIDI", lvx, gy, wLive, gh);
        panel(L"Window", wx, gy, wWin, gh);
        const int r1 = gy + S(21), r2 = gy + S(49), r3 = gy + S(77), r4 = gy + S(105), r5 = gy + S(133);
        {
            const int x = ox + S(8), w = wOut - S(16);
            const int a = S(90), b = (w - a - 2 * gap) / 2;
            at(ID_BTN_OUTPUT, x, r1, a, bh);
            at(ID_BTN_88KEY, x + a + gap, r1, b, bh);
            at(ID_BTN_TRANSPOSEOUT, x + a + b + 2 * gap, r1, w - a - b - 2 * gap, bh);
            at(ID_BTN_VELOCITY, x, r2, S(64), bh);
            put(g.curveCombo, x + S(64) + gap, r2, w - S(64) - S(40) - 2 * gap, S(300));
            at(ID_BTN_VLCURVE, x + w - S(40), r2, S(40), bh);
            at(ID_BTN_SUSTAIN, x, r3, S(84), bh);
            put(g.cutoff.hwnd(), x + S(84) + gap, r3, w - S(84) - S(28) - 2 * gap, bh);
            put(g.cutoffValue, x + w - S(28), r3, S(28), bh);
            at(ID_BTN_VOLADJ, x, r4, S(64), bh);
            put(g.batchCombo, x + S(64) + gap, r4, w - S(64) - gap, S(300));
            at(ID_BTN_LEGIT, x, r5, (w - gap) / 2, bh);
            at(ID_BTN_TRANSPOSE, x + (w - gap) / 2 + gap, r5, w - (w - gap) / 2 - gap, bh);
        }
        {
            const int x = lvx + S(8), w = wLive - S(16), half = (w - gap) / 2;
            put(g.devCombo, x, r1, w, S(300));
            put(g.chanCombo, x, r2, w, S(300));
            at(ID_BTN_MIDI2QWERTY, x, r3, half, bh);
            at(ID_BTN_MIDICONNECT, x + half + gap, r3, w - half - gap, bh);
            at(ID_BTN_REFRESH_MIDI, x, r4, w, bh);
            put(g.latency, x, r5, w, bh);
        }
        {
            const int x = wx + S(8), w = wWin - S(16), half = (w - gap) / 2;
            at(ID_CHK_GAME, x, r1, w, bh);
            at(ID_CHK_TOP, x, r2, half, bh);
            at(ID_CHK_DARK, x + half + gap, r2, w - half - gap, bh);
            put(g.opacityLabel, x, r3, S(48), bh);
            put(g.opacity.hwnd(), x + S(48), r3, w - S(48), bh);
            at(ID_BTN_FOLDER, x, r4, w, bh);
            at(ID_BTN_STOP, x, r5, w, bh);
        }

        const int statusH = S(22);
        const int logY = S(532);
        const int logH = H - statusH - S(6) - logY;
        frame(g.tracks.GetWindowHandle(), m + 1, top + S(7), W - 2 * m - 2, S(122));
        frame(g.piano.hwnd(), m + 1, top + S(136), W - 2 * m - 2, S(54));
        panel(L"Log", m, logY, W - 2 * m, logH);
        frame(g.log, m + S(8), logY + S(22), W - 2 * m - S(16) - S(78), logH - S(30));
        at(ID_BTN_CLEARLOG, W - m - S(8) - S(72), logY + S(22), S(72), bh);
        put(g.status.hwnd(), 0, H - statusH, W, statusH);
        if (dw)
            EndDeferWindowPos(dw);
        InvalidateRect(g.wnd, nullptr, TRUE);
    }

    void paint(HWND wnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(wnd, &ps);
        RECT rc;
        GetClientRect(wnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        const ui::Palette& p = ui::pal();
        FillRect(mem, &rc, g.brBg);
        SetBkMode(mem, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(mem, ui::font(g.dpi, 11, FW_SEMIBOLD));
        for (const Panel& pn : g.panels) {
            FillRect(mem, &pn.r, g.brPanel);
            SetDCBrushColor(mem, p.border);
            FrameRect(mem, &pn.r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            RECT t{ pn.r.left + S(8), pn.r.top + S(3), pn.r.right - S(8), pn.r.top + S(20) };
            SetTextColor(mem, p.subtext);
            DrawTextW(mem, pn.title, -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }
        for (const RECT& f : g.frames) {
            SetDCBrushColor(mem, p.border);
            FrameRect(mem, &f, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        for (const RECT& s : g.screens) {
            HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(DC_BRUSH));
            HGDIOBJ oldPen = SelectObject(mem, GetStockObject(DC_PEN));
            SetDCBrushColor(mem, ui::lcd().bg);
            SetDCPenColor(mem, ui::lcd().bezel);
            RoundRect(mem, s.left, s.top, s.right, s.bottom, S(8), S(8));
            SelectObject(mem, oldPen);
            SelectObject(mem, oldBrush);
        }
        SelectObject(mem, oldFont);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(wnd, &ps);
    }

    bool favorite(const Item& item) {
        if (item.folder)
            return false;
        if (item.path.parent_path().filename() == L"favorite")
            return true;
        std::error_code ec;
        return fs::exists(fs::path(L"midi") / L"favorite" / item.path.filename(), ec);
    }

    void scan_folder() {
        g.items.clear();
        std::error_code ec;
        if (!fs::is_directory(g.dir, ec)) {
            say("[Scan] '%s' not found.", narrow(g.dir.wstring()).c_str());
            return;
        }
        if (!fs::equivalent(g.dir, L"midi", ec))
            g.items.push_back({ L"..", g.dir.parent_path(), true, false, 0 });
        for (const auto& entry : fs::directory_iterator(g.dir, ec)) {
            Item item;
            item.name = entry.path().filename().wstring();
            item.path = entry.path();
            item.folder = entry.is_directory(ec);
            if (!item.folder) {
                std::wstring ext = entry.path().extension().wstring();
                for (wchar_t& c : ext)
                    c = static_cast<wchar_t>(towlower(c));
                if (ext != L".mid" && ext != L".midi" && ext != L".rmi" && ext != L".kar")
                    continue;
                auto ft = fs::last_write_time(entry.path(), ec);
                auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                item.written = std::chrono::system_clock::to_time_t(sys);
                item.favorite = favorite(item);
            }
            g.items.push_back(std::move(item));
        }
        const int mode = cfg().ui.sortMode;
        auto byName = [&](const Item& a, const Item& b) {
            int c = _wcsicmp(a.name.c_str(), b.name.c_str());
            return mode == 1 ? c > 0 : c < 0;
        };
        std::stable_sort(g.items.begin(), g.items.end(), [&](const Item& a, const Item& b) {
            int ra = a.name == L".." ? 0 : a.folder ? 1 : 2, rb = b.name == L".." ? 0 : b.folder ? 1 : 2;
            if (ra != rb)
                return ra < rb;
            if (ra == 1)
                return byName(a, b);
            if (a.favorite != b.favorite)
                return a.favorite;
            if (mode == 2)
                return a.written < b.written;
            if (mode == 3)
                return a.written > b.written;
            return byName(a, b);
        });
    }

    void fill_list() {
        wchar_t query[256] = {};
        GetWindowTextW(g.search, query, 256);
        std::wstring q = query;
        for (wchar_t& c : q)
            c = static_cast<wchar_t>(towlower(c));
        g.shown.clear();
        g.songNo = g.songTotal = 0;
        SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g.list, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < static_cast<int>(g.items.size()); ++i) {
            const Item& item = g.items[i];
            if (!q.empty() && !item.folder) {
                std::wstring n = item.name;
                for (wchar_t& c : n)
                    c = static_cast<wchar_t>(towlower(c));
                if (n.find(q) == std::wstring::npos)
                    continue;
            }
            SendMessageW(g.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name.c_str()));
            g.shown.push_back(i);
            if (!item.folder)
                ++g.songTotal;
            if (!item.folder && g.song && item.path == g.song->path)
                g.songNo = g.songTotal;
        }
        if (!g.shown.empty())
            SendMessageW(g.list, LB_SETCURSEL, 0, 0);
        SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g.list, nullptr, TRUE);
    }

    void refresh_list() {
        scan_folder();
        fill_list();
    }

    int selected() {
        int sel = static_cast<int>(SendMessageW(g.list, LB_GETCURSEL, 0, 0));
        return sel >= 0 && sel < static_cast<int>(g.shown.size()) ? g.shown[sel] : -1;
    }

    void select_item(int item) {
        auto it = std::find(g.shown.begin(), g.shown.end(), item);
        if (it != g.shown.end())
            SendMessageW(g.list, LB_SETCURSEL, it - g.shown.begin(), 0);
    }

    void toggle_favorite(int row) {
        if (row < 0 || row >= static_cast<int>(g.shown.size()))
            return;
        Item& item = g.items[g.shown[row]];
        if (item.folder)
            return;
        std::error_code ec;
        fs::path favDir = fs::path(L"midi") / L"favorite";
        if (item.path.parent_path().filename() == L"favorite") {
            fs::remove(item.path, ec);
            if (!ec)
                item.path = fs::path(L"midi") / item.path.filename();
        }
        else {
            fs::path dest = favDir / item.path.filename();
            if (fs::exists(dest, ec)) {
                fs::remove(dest, ec);
            }
            else {
                fs::create_directories(favDir, ec);
                fs::copy_file(item.path, dest, fs::copy_options::overwrite_existing, ec);
            }
        }
        item.favorite = favorite(item);
        RECT r{};
        SendMessageW(g.list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&r));
        InvalidateRect(g.list, &r, TRUE);
        SendMessageW(g.list, LB_SETCURSEL, row, 0);
    }

    LRESULT CALLBACK list_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
        if (msg == WM_RBUTTONDOWN) {
            LRESULT r = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
            if (!HIWORD(r))
                toggle_favorite(LOWORD(r));
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    void fill_devices() {
        g.devices = live::devices();
        SendMessageW(g.devCombo, CB_RESETCONTENT, 0, 0);
        int sel = 0;
        for (size_t i = 0; i < g.devices.size(); ++i) {
            SendMessageW(g.devCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g.devices[i].name.c_str()));
            if (narrow(g.devices[i].name) == cfg().ui.midiDevice)
                sel = static_cast<int>(i);
        }
        if (g.devices.empty())
            SendMessageW(g.devCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No MIDI input devices"));
        SendMessageW(g.devCombo, CB_SETCURSEL, sel, 0);
    }

    const live::Device* chosen_device() {
        int sel = static_cast<int>(SendMessageW(g.devCombo, CB_GETCURSEL, 0, 0));
        return sel >= 0 && sel < static_cast<int>(g.devices.size()) ? &g.devices[sel] : nullptr;
    }

    void fill_curves() {
        SendMessageW(g.curveCombo, CB_RESETCONTENT, 0, 0);
        for (const wchar_t* n : { L"Linear Coarse", L"Linear Fine", L"Improved Low Volume", L"Logarithmic", L"Exponential" })
            SendMessageW(g.curveCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
        for (const auto& c : cfg().playback.customVelocityCurves)
            SendMessageW(g.curveCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(c.name).c_str()));
        int count = static_cast<int>(SendMessageW(g.curveCombo, CB_GETCOUNT, 0, 0));
        SendMessageW(g.curveCombo, CB_SETCURSEL, std::clamp(cfg().playback.velocityCurveIndex, 0, count - 1), 0);
    }

    void refresh_piano_keys() {
        std::array<std::wstring, 128> labels;
        bool connect = live::active() ? live::mode() == out::Mode::MidiConnect : cfg().playback.output == midi::OutputMode::MidiConnect;
        if (connect)
            labels.fill(L" ");
        else
            labels = out::labels();
        g.piano.set_keys(labels);
    }

    void show_live_rows(bool on);

    void stop_live(bool quiet = false) {
        if (!live::active())
            return;
        out::Mode m = live::mode();
        live::close();
        show_live_rows(false);
        if (m == out::Mode::MidiConnect)
            out::sink(m).end();
        if (!quiet)
            say(m == out::Mode::MidiConnect ? "[MidiConnect] DISABLED" : "[MIDI->QWERTY] DISABLED");
        redraw(ID_BTN_MIDI2QWERTY);
        redraw(ID_BTN_MIDICONNECT);
        refresh_piano_keys();
        update_focusability();
    }

    void start_live(out::Mode m) {
        const live::Device* dev = chosen_device();
        if (!dev) {
            MessageBoxW(g.wnd, L"No MIDI input device found.\nPlug in your keyboard or start loopMIDI / LoopBe, then press Refresh MIDI.",
                L"MIDI++", MB_OK | MB_ICONWARNING);
            return;
        }
        g.player.pause();
        aim("Live MIDI");
        if (m == out::Mode::MidiConnect)
            out::sink(m).begin();
        std::wstring err;
        if (!live::open(*dev, m, err)) {
            if (m == out::Mode::MidiConnect)
                out::sink(m).end();
            say("[MIDI] Could not open %s: %s", narrow(dev->name).c_str(), narrow(err).c_str());
            std::wstring text = L"Could not open " + dev->name + L":\n" + err;
            MessageBoxW(g.wnd, text.c_str(), L"MIDI++", MB_OK | MB_ICONERROR);
            return;
        }
        live::set_channel(cfg().ui.midiChannel);
        live::set_key(g.key - g.gameTranspose);
        if (m == out::Mode::Keys && cfg().playback.autoVolume)
            out::calibrate_volume();
        show_live_rows(true);
        if (m == out::Mode::MidiConnect)
            say("[MidiConnect] ENABLED on %s through %s (NumLock is kept on while it runs)", narrow(dev->name).c_str(), narrow(live::path()).c_str());
        else
            say("[MIDI->QWERTY] ENABLED on %s through %s", narrow(dev->name).c_str(), narrow(live::path()).c_str());
        redraw(ID_BTN_MIDI2QWERTY);
        redraw(ID_BTN_MIDICONNECT);
        refresh_piano_keys();
        update_focusability();
    }

    void toggle_live(out::Mode m) {
        bool was = live::active() && live::mode() == m;
        stop_live();
        if (!was)
            start_live(m);
    }

    void apply_rows(const std::vector<TrackControl::TrackInfo>& t, bool channels) {
        bool solo = std::any_of(t.begin(), t.end(), [](const TrackControl::TrackInfo& i) { return i.isSoloed; });
        for (size_t i = 0; i < t.size(); ++i) {
            const bool on = solo ? t[i].isSoloed : !t[i].isMuted;
            if (channels)
                live::set_channel_enabled(static_cast<int>(i), on);
            else
                g.player.set_track_enabled(i, on);
        }
    }

    void apply_tracks() {
        apply_rows(g.tracks.Tracks(), g.liveRows);
    }

    void show_live_rows(bool on) {
        if (on == g.liveRows)
            return;
        if (on) {
            g.songRows = g.tracks.Tracks();
            std::vector<TrackControl::TrackInfo> rows;
            for (int ch = 0; ch < 16; ++ch) {
                TrackControl::TrackInfo info;
                info.trackName = "Channel " + std::to_string(ch + 1);
                info.instrumentName = ch == 9 ? "Live input, drum channel" : "Live input";
                info.noteCount = -1;
                info.channel = ch;
                rows.push_back(std::move(info));
            }
            g.liveRows = true;
            g.tracks.SetTracks(rows);
            apply_tracks();
            return;
        }
        g.liveRows = false;
        g.tracks.SetTracks(g.songRows);
        for (int ch = 0; ch < 16; ++ch)
            live::set_channel_enabled(ch, true);
        apply_tracks();
    }

    void show_tracks(bool keepStates) {
        std::vector<TrackControl::TrackInfo> old = keepStates ? (g.liveRows ? g.songRows : g.tracks.Tracks()) : std::vector<TrackControl::TrackInfo>{};
        std::vector<TrackControl::TrackInfo> list;
        if (g.song) {
            list.reserve(g.song->tracks.size());
            for (size_t i = 0; i < g.song->tracks.size(); ++i) {
                const SongTrack& s = g.song->tracks[i];
                TrackControl::TrackInfo info;
                info.trackName = s.name;
                info.instrumentName = s.instrument;
                info.noteCount = static_cast<int>(std::min<uint64_t>(s.notes, INT_MAX));
                info.channel = s.channel;
                info.programNumber = s.program;
                info.isDrums = s.drums;
                info.isMuted = s.drums;
                if (i < old.size()) {
                    info.isMuted = old[i].isMuted;
                    info.isSoloed = old[i].isSoloed;
                }
                list.push_back(std::move(info));
            }
        }
        if (g.liveRows) {
            g.songRows = list;
            apply_rows(list, false);
            return;
        }
        g.tracks.SetTracks(list);
        apply_tracks();
    }

    void show_song() {
        if (!g.song) {
            g.lcd.set_text(g.loading ? L"Loading " + g.loadingPath.filename().wstring() + L"..." : std::wstring(L"No song loaded"),
                L"Pick a MIDI file on the left and press Load, or double click it.");
            return;
        }
        const Song& s = *g.song;
        g.songNo = 0;
        for (int i = 0, n = 0; i < static_cast<int>(g.shown.size()); ++i) {
            const Item& item = g.items[g.shown[i]];
            if (item.folder)
                continue;
            ++n;
            if (item.path == s.path)
                g.songNo = n;
        }
        int active = 0;
        for (const auto& t : s.tracks)
            active += t.notes > 0;
        std::wstring info = thousands(s.notes) + L" notes, " + std::to_wstring(active) + (active == 1 ? L" track, " : L" tracks, ");
        wchar_t bpm[32];
        swprintf_s(bpm, L"%.0f BPM", s.bpm);
        info += bpm;
        info += L", " + std::to_wstring(s.tsNum) + L"/" + std::to_wstring(s.tsDen) + L", " + fmt_time(s.length);
        if (s.peakNotesPerSec >= 2000)
            info += L", black MIDI";
        if (cfg().legit.ENABLED)
            info += L", legit";
        std::wstring title;
        for (wchar_t ch : s.path.stem().wstring()) {
            if (ch == L'_')
                ch = L' ';
            if (ch != L' ' || (!title.empty() && title.back() != L' '))
                title += ch;
        }
        g.lcd.set_text(title, info);
    }

    void show_time(bool force = false) {
        if (!g.song) {
            if (force || g.shownSecond != -2) {
                g.seek.set(0);
                g.shownSecond = -2;
            }
            g.lcd.set_counters(g.songNo, g.songTotal, 0, 0, 0, false);
            return;
        }
        const int64_t len = std::max<int64_t>(1, g.song->length);
        int64_t pos = g.seek.dragging() ? static_cast<int64_t>(g.seek.value() / 10000.0 * len)
            : std::clamp<int64_t>(g.player.position(), 0, len);
        if (!g.seek.dragging())
            g.seek.set(static_cast<int>(static_cast<double>(pos) / len * 10000.0));
        g.shownSecond = static_cast<int>(pos / 1'000'000'000);
        g.lcd.set_counters(g.songNo, g.songTotal, pos, g.song->length,
            static_cast<int>(std::lround(g.song->bpm_at(pos) * g.player.speed())), playing());
    }

    void show_key() {
        std::wstring k = g.key > 0 ? L"+" + std::to_wstring(g.key) : std::to_wstring(g.key);
        SetWindowTextW(g.keyValue, k.c_str());
    }

    void show_speed() {
        wchar_t buf[32];
        swprintf_s(buf, L"%.2fx", g.player.speed());
        SetWindowTextW(g.speed, buf);
    }

    void show_status() {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - g.lastStats).count();
        if (secs >= 0.25) {
            uint64_t sent = input::sent_events(), notes = g.player.notes_played();
            g.keysPerSec = static_cast<double>(sent - g.lastSent) / secs;
            g.notesPerSec = static_cast<double>(notes - g.lastNotes) / secs;
            g.lastSent = sent;
            g.lastNotes = notes;
            g.lastStats = now;
            Player::Stats st = g.player.stats();
            if (st.batches)
                g.timing = st;
            live::Latency l = live::latency();
            if (l.messages)
                g.live = l;
        }
        HWND t = input::target();
        std::wstring left = L"Target: " + (t && IsWindow(t) ? widen(window_title(t)) : std::wstring(L"none"));
        const auto& p = cfg().playback;
        left += p.output == midi::OutputMode::MidiConnect ? L"  |  MidiConnect" : (p.keys88 ? L"  |  88 keys" : L"  |  61 keys");
        if (p.output != midi::OutputMode::MidiConnect && p.typedKeys)
            left += L", typed";
        int batch = g.player.batching();
        left += L"  |  batching " + (batch ? std::to_wstring(batch) + L" ms" : std::wstring(L"off"));
        if (p.batchMs < 0)
            left += L" (auto)";
        wchar_t right[256];
        swprintf_s(right, L"%s notes/s  |  %s keys/s  |  timing %.1f us avg, %.0f us worst",
            thousands(static_cast<uint64_t>(g.notesPerSec)).c_str(), thousands(static_cast<uint64_t>(g.keysPerSec)).c_str(),
            g.timing.avgLateUs, g.timing.maxLateUs);
        g.status.set(left, right);

        wchar_t lat[96];
        if (live::active() && g.live.messages)
            swprintf_s(lat, L"%.1f us avg, %.0f us worst", g.live.avgUs, g.live.maxUs);
        else
            swprintf_s(lat, live::active() ? L"Waiting for notes" : L"Live input off");
        SetWindowTextW(g.latency, lat);
    }

    TransposePlan plan_transpose() {
        TransposeOptions o;
        o.layout = cfg().playback.keys88 ? "FULL" : "LIMITED";
        o.fold = cfg().playback.outOfRange;
        o.batchMs = cfg().playback.batchMs;
        o.key = g.key;
        o.range = 12;
        const std::vector<TrackControl::TrackInfo> rows = g.liveRows ? g.songRows : g.tracks.Tracks();
        const bool solo = std::any_of(rows.begin(), rows.end(), [](const TrackControl::TrackInfo& i) { return i.isSoloed; });
        for (const TrackControl::TrackInfo& r : rows)
            o.tracks.push_back(solo ? r.isSoloed : !r.isMuted);
        return TransposeEngine().plan(*g.song, o);
    }

    int suggest_transpose() {
        if (!g.song)
            return 0;
        const TransposePlan p = plan_transpose();
        if (!p.total) {
            say("[TRANSPOSE] No notes.");
            return 0;
        }
        say("[TRANSPOSE] %s (%.0f%%), game %+d: out of range %d -> %d, shift/ctrl %d -> %d, same key %d -> %d",
            p.key.c_str(), p.keyConfidence * 100.0, p.semitones, p.unshifted.outOfRange, p.outOfRange,
            p.unshifted.needsModifier, p.needsModifier, p.unshifted.sameKeyCollisions, p.sameKeyCollisions);
        return p.semitones;
    }

    void apply_key() {
        g.player.set_key(g.key - g.gameTranspose);
        live::set_key(g.key - g.gameTranspose);
    }

    LoadOptions options() {
        LoadOptions o;
        o.pairing = cfg().playback.noteHandlingMode;
        o.legit = cfg().legit;
        o.detectDrums = cfg().midi.DETECT_DRUMS;
        return o;
    }

    void open_song(const fs::path& path, bool autoplay, int64_t keep = -1) {
        g.cancelLoad = true;
        if (g.loader.joinable())
            g.loader.join();
        g.cancelLoad = false;
        g.loadProgress = 0;
        g.loading = true;
        g.loadingPath = path;
        uint64_t seq = ++g.loadSeq;
        if (keep < 0)
            g.player.pause();
        LoadOptions opt = options();
        HWND wnd = g.wnd;
        g.loader = std::thread([path, opt, seq, autoplay, keep, wnd] {
            auto* r = new Loaded{ seq, path, nullptr, {}, 0.0, autoplay, keep };
            auto t0 = std::chrono::steady_clock::now();
            try {
                r->song = load_song(path, opt, &g.loadProgress, &g.cancelLoad);
            }
            catch (const std::exception& e) {
                r->error = e.what();
            }
            r->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!PostMessageW(wnd, WM_APP_LOADED, 0, reinterpret_cast<LPARAM>(r)))
                delete r;
        });
        if (keep < 0)
            show_song();
    }

    void on_loaded(Loaded* raw) {
        std::unique_ptr<Loaded> r(raw);
        if (r->seq != g.loadSeq)
            return;
        g.loading = false;
        if (!r->error.empty() || !r->song) {
            if (!r->error.empty())
                say("[Load] Error in %s: %s", narrow(r->path.filename().wstring()).c_str(), r->error.c_str());
            show_song();
            return;
        }
        const bool keep = r->keep >= 0;
        const bool was = keep && playing();
        g.song = r->song;
        g.player.load(g.song, keep ? static_cast<int64_t>(static_cast<double>(r->keep) / 1e9 * g.song->length) : 0);
        show_tracks(keep);
        if (was)
            g.player.play();
        g.freshSong = !keep;
        show_song();
        show_time(true);
        if (keep) {
            say("[LEGIT] Song rebuilt in %.1f ms", r->ms);
            return;
        }
        for (size_t i = 0; i < g.song->tracks.size(); ++i)
            if (g.song->tracks[i].drums)
                say("[DRUMS] Track #%zu \"%s\" looks like drums, muted it", i + 1, g.song->tracks[i].name.c_str());
        say("[Load] Loaded: %s (%s notes, %s) in %.1f ms", narrow(r->path.filename().wstring()).c_str(),
            narrow(thousands(g.song->notes)).c_str(), narrow(fmt_time(g.song->length)).c_str(), r->ms);
        if (g.song->peakNotesPerSec >= 2000 && cfg().playback.batchMs < 0)
            say("[Load] Black MIDI: %s notes/s at its peak, keys go out in %d ms batches so the game keeps up.",
                narrow(thousands(static_cast<uint64_t>(g.song->peakNotesPerSec))).c_str(), g.player.batching());
        if (cfg().playback.autoVolume)
            g.player.recalibrate();
        if (r->autoplay && !live::active())
            start_playing(false);
    }

    void load_selected(bool autoplay = false) {
        int sel = selected();
        if (sel < 0 || g.items[sel].folder) {
            say("[Load] No MIDI file selected.");
            return;
        }
        open_song(g.items[sel].path, autoplay);
    }

    bool live_blocks() {
        if (!live::active())
            return false;
        MessageBoxW(g.wnd, L"Auto controls are disabled while MIDI input is active.", L"Info", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    bool need_song() {
        if (g.song)
            return true;
        MessageBoxW(g.wnd, g.loading ? L"The song is still loading." : L"Please load a MIDI file first.", L"MIDI++", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    void begin_output() {
        if (cfg().playback.output == out::Mode::MidiConnect && !g.connectBegun) {
            out::sink(out::Mode::MidiConnect).begin();
            g.connectBegun = true;
        }
        else if (cfg().playback.output != out::Mode::MidiConnect && g.connectBegun) {
            out::sink(out::Mode::MidiConnect).end();
            g.connectBegun = false;
        }
        g.player.set_output(cfg().playback.output);
    }

    void start_playing(bool restart) {
        aim("Playing");
        begin_output();
        if ((g.freshSong || restart) && cfg().auto_transpose.ENABLED && cfg().playback.output == midi::OutputMode::Keys) {
            int want = suggest_transpose();
            int diff = want - g.gameTranspose;
            if (diff) {
                say("Suggested Transposition: %d, adjusting by %d steps.", want, diff);
                g.player.transpose(diff);
                g.gameTranspose = want;
            }
            apply_key();
        }
        g.freshSong = false;
        if (restart)
            g.player.restart();
        else
            g.player.play();
        update_focusability();
    }

    void play_pause() {
        if (live_blocks() || !need_song())
            return;
        if (playing()) {
            g.player.pause();
            say("[PLAYBACK] Paused");
            update_focusability();
            return;
        }
        start_playing(false);
        say("[PLAYBACK] Playing");
    }

    void emergency_stop() {
        g.player.pause();
        stop_live(true);
        {
            input::Batch b(true);
            out::sink(out::Mode::Keys).reset(b);
            out::sink(out::Mode::MidiConnect).reset(b);
        }
        say("[EMERGENCY] Stopped playback and live input, released every key.");
        update_focusability();
    }

    void step_song(int dir, bool random) {
        std::vector<int> files;
        for (int i : g.shown)
            if (!g.items[i].folder)
                files.push_back(i);
        if (files.empty())
            return;
        int cur = selected();
        int next = files.front();
        if (random) {
            static std::mt19937 rng(std::random_device{}());
            if (files.size() > 1)
                do next = files[std::uniform_int_distribution<size_t>(0, files.size() - 1)(rng)]; while (next == cur);
        }
        else {
            auto it = std::find(files.begin(), files.end(), cur);
            if (it == files.end())
                next = dir > 0 ? files.front() : files.back();
            else if (dir > 0)
                next = (it + 1 == files.end()) ? files.front() : *(it + 1);
            else
                next = it == files.begin() ? files.back() : *(it - 1);
        }
        select_item(next);
        open_song(g.items[next].path, true);
    }

    void set_toggle(int id) {
        auto& p = cfg().playback;
        switch (id) {
        case ID_BTN_88KEY:
            p.keys88 = !p.keys88;
            say("[88-KEY MODE] %s", p.keys88 ? "Enabled (notes outside the 61 keys use ctrl+key, only for games that support it)" : "Disabled");
            break;
        case ID_BTN_VOLADJ:
            p.autoVolume = !p.autoVolume;
            if (p.autoVolume)
                say("[AUTOVOL] On. Initial=%d%% Step=%d%% Max=%d%%", cfg().volume.INITIAL_VOLUME, cfg().volume.VOLUME_STEP, cfg().volume.MAX_VOLUME);
            else
                say("[AUTOVOL] Off.");
            break;
        case ID_BTN_VELOCITY:
            p.velocityKeys = !p.velocityKeys;
            say("[VELOCITY KEY] %s", p.velocityKeys ? "Enabled (ALT+key combos in use)" : "Disabled");
            break;
        case ID_BTN_SUSTAIN:
            p.sustain = p.sustain == midi::SustainMode::Ignore ? midi::SustainMode::Down
                : p.sustain == midi::SustainMode::Down ? midi::SustainMode::Up : midi::SustainMode::Ignore;
            SetDlgItemTextW(g.wnd, ID_BTN_SUSTAIN, p.sustain == midi::SustainMode::Down ? L"Sustain: Down"
                : p.sustain == midi::SustainMode::Up ? L"Sustain: Up" : L"Sustain: Off");
            if (p.sustain == midi::SustainMode::Down) say("[SUSTAIN] DOWN cutoff=%d", p.sustainCutoff);
            else if (p.sustain == midi::SustainMode::Up) say("[SUSTAIN] UP (inverted) cutoff=%d", p.sustainCutoff);
            else say("[SUSTAIN] IGNORE");
            break;
        case ID_BTN_TRANSPOSEOUT:
            p.outOfRange = !p.outOfRange;
            say("[TRANSPOSE] Out of range notes %s", p.outOfRange ? "move into range" : "are dropped");
            break;
        case ID_BTN_OUTPUT:
            if (p.output == midi::OutputMode::Keys && !p.typedKeys) {
                p.typedKeys = true;
            }
            else if (p.output == midi::OutputMode::Keys) {
                p.output = midi::OutputMode::MidiConnect;
                p.typedKeys = false;
            }
            else {
                p.output = midi::OutputMode::Keys;
            }
            SetDlgItemTextW(g.wnd, ID_BTN_OUTPUT, output_label());
            say("[OUTPUT] %s", p.output == midi::OutputMode::MidiConnect ? "Autoplayer sends MidiConnect numpad codes"
                : p.typedKeys ? "Keys follow the characters of the game window's keyboard layout (for apps that read text)"
                : "Keys are sent by position like a US keyboard (Roblox)");
            if (playing())
                begin_output();
            else
                g.player.set_output(p.output);
            break;
        default:
            return;
        }
        out::reload();
        if (id == ID_BTN_VOLADJ && p.autoVolume) {
            if (live::active()) {
                aim("Volume calibration");
                out::calibrate_volume();
            }
            else if (g.song)
                g.player.recalibrate();
        }
        refresh_piano_keys();
        redraw(id);
        save_config();
    }

    void on_legit_apply() {
        const auto& l = cfg().legit;
        if (l.ENABLED)
            say("[LEGIT] On: timing %.0f%%, skip %.0f%%, pauses %.0f%% (%.0f-%.0f ms)", l.TIMING_VARIATION * 100, l.NOTE_SKIP_CHANCE * 100,
                l.EXTRA_DELAY_CHANCE * 100, l.EXTRA_DELAY_MIN * 1000, l.EXTRA_DELAY_MAX * 1000);
        else
            say("[LEGIT] Off");
        save_config();
        redraw(ID_BTN_LEGIT);
        if (g.song) {
            int64_t pos = std::max<int64_t>(0, g.player.position());
            double frac = std::clamp(static_cast<double>(pos) / std::max<int64_t>(1, g.song->length), 0.0, 1.0);
            open_song(g.song->path, false, static_cast<int64_t>(frac * 1e9));
        }
    }

    void target_left() {
        HWND t = input::target();
        if (!t || !input::guard() || input::target_in_front())
            return;
        input::focus_lost();
        if (!IsWindow(t)) {
            input::set_target(nullptr);
            say("[Keys] The game window closed.");
            if (playing())
                g.player.pause();
            update_focusability();
            return;
        }
        if (ours(root(GetForegroundWindow()))) {
            if (!g.awayNoted && (playing() || live::active()))
                say("[Keys] MIDI++ is in front, the song keeps going and keys resume when %s is back in front.", window_title(t).c_str());
            g.awayNoted = true;
            return;
        }
        if (playing()) {
            g.player.pause();
            say("[PLAYBACK] Paused, %s is not in front. Press %s in the game to go on.", window_title(t).c_str(),
                input::key_label(cfg().hotkeys.PLAY_PAUSE_KEY).c_str());
            update_focusability();
        }
    }

    void CALLBACK on_foreground(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
        if (idObject != OBJID_WINDOW || !hwnd)
            return;
        HWND r = root(hwnd);
        if (!ours(r))
            g.lastExternal = r;
        target_left();
    }

    void hotkey_loop() {
        const auto& h = cfg().hotkeys;
        const int vks[4] = { input::parse_vk(h.PLAY_PAUSE_KEY), input::parse_vk(h.REWIND_KEY), input::parse_vk(h.SKIP_KEY),
            input::parse_vk(h.EMERGENCY_EXIT_KEY) };
        bool was[4] = {};
        while (!g.quit.load()) {
            for (int i = 0; i < 4; ++i) {
                bool down = vks[i] && (GetAsyncKeyState(vks[i]) & 0x8000);
                if (down && !was[i])
                    PostMessageW(g.wnd, WM_APP_HOTKEY, i, 0);
                was[i] = down;
            }
            Sleep(10);
        }
    }

    void create_controls(HWND wnd) {
        g.wnd = wnd;
        INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icex);
        const auto& c = cfg();

        g.search = control(L"edit", L"", ES_AUTOHSCROLL | WS_TABSTOP, ID_SEARCH);
        SendMessageW(g.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search songs"));
        g.sortCombo = control(L"combobox", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, ID_CB_SORT);
        for (const wchar_t* s : { L"Name (A-Z)", L"Name (Z-A)", L"Date (Old-New)", L"Date (New-Old)" })
            SendMessageW(g.sortCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(g.sortCombo, CB_SETCURSEL, c.ui.sortMode, 0);
        button(L"Refresh", ID_BTN_REFRESH);
        g.list = control(L"listbox", nullptr, LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, ID_LB_MIDI);
        SetWindowSubclass(g.list, list_proc, 0, 0);
        button(L"Prev", ID_BTN_PREV);
        button(L"Next", ID_BTN_NEXT);
        button(L"Shuffle", ID_CHK_SHUFFLE);

        g.lcd.create(wnd);
        button(L"Load", ID_BTN_LOAD);
        button(L"Play", ID_BTN_PLAY);
        button(L"Restart", ID_BTN_RESTART);
        button(L"-10s", ID_BTN_REW);
        button(L"+10s", ID_BTN_SKIP);
        g.tempoLabel = control(L"static", L"Tempo", SS_RIGHT | SS_CENTERIMAGE);
        button(L"-", ID_BTN_SPEEDDN);
        g.speed = control(L"static", L"1.00x", SS_CENTER | SS_CENTERIMAGE);
        button(L"+", ID_BTN_SPEEDUP);
        g.keyLabel = control(L"static", L"Key", SS_RIGHT | SS_CENTERIMAGE);
        button(L"-", ID_BTN_KEYDN);
        g.keyValue = control(L"static", L"0", SS_CENTER | SS_CENTERIMAGE);
        button(L"+", ID_BTN_KEYUP);
        g.visual.create(wnd);
        g.seek.create(wnd, ID_SEEK, 0, 10000, 0);

        button(output_label(), ID_BTN_OUTPUT);
        button(L"88-Key", ID_BTN_88KEY);
        button(L"OutRange", ID_BTN_TRANSPOSEOUT);
        button(L"Velocity", ID_BTN_VELOCITY);
        g.curveCombo = control(L"combobox", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, ID_CB_VELOCITY_CURVE);
        button(L"Edit", ID_BTN_VLCURVE);
        button(c.playback.sustain == midi::SustainMode::Down ? L"Sustain: Down"
            : c.playback.sustain == midi::SustainMode::Up ? L"Sustain: Up" : L"Sustain: Off", ID_BTN_SUSTAIN);
        g.cutoff.create(wnd, ID_SLIDER_SUSTAIN, 0, 127, c.playback.sustainCutoff);
        g.cutoffValue = control(L"static", std::to_wstring(c.playback.sustainCutoff).c_str(), SS_CENTER | SS_CENTERIMAGE);
        button(L"AutoVol", ID_BTN_VOLADJ);
        g.batchCombo = control(L"combobox", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, ID_CB_BATCH);
        for (const wchar_t* s : { L"Key batching: auto", L"Key batching: off", L"Key batching: 1 ms", L"Key batching: 2 ms",
            L"Key batching: 4 ms", L"Key batching: 8 ms", L"Key batching: 16 ms" })
            SendMessageW(g.batchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        int batchSel = 0;
        for (int i = 0; i < 7; ++i)
            if (kBatchMs[i] == c.playback.batchMs)
                batchSel = i;
        SendMessageW(g.batchCombo, CB_SETCURSEL, batchSel, 0);
        button(L"Legit...", ID_BTN_LEGIT);
        button(L"Transpose", ID_BTN_TRANSPOSE);

        g.devCombo = control(L"combobox", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, ID_CB_MIDIDEV);
        g.chanCombo = control(L"combobox", nullptr, CBS_DROPDOWNLIST | WS_VSCROLL, ID_CB_MIDICH);
        SendMessageW(g.chanCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Channels"));
        for (int ch = 1; ch <= 16; ++ch) {
            std::wstring s = L"Channel " + std::to_wstring(ch);
            SendMessageW(g.chanCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
        }
        SendMessageW(g.chanCombo, CB_SETCURSEL, c.ui.midiChannel + 1, 0);
        button(L"Midi2Key", ID_BTN_MIDI2QWERTY);
        button(L"MidiConnect", ID_BTN_MIDICONNECT);
        button(L"Refresh MIDI", ID_BTN_REFRESH_MIDI);
        g.latency = control(L"static", L"Live input off", SS_CENTER | SS_CENTERIMAGE);

        button(L"Game window only", ID_CHK_GAME);
        button(L"On top", ID_CHK_TOP);
        button(L"Dark mode", ID_CHK_DARK);
        g.opacityLabel = control(L"static", L"Opacity", SS_LEFT | SS_CENTERIMAGE);
        g.opacity.create(wnd, ID_SLIDER_OPACITY, 100, 255, c.ui.opacity);
        button(L"Open MIDI folder", ID_BTN_FOLDER);
        button(L"Stop all keys (F4)", ID_BTN_STOP);

        g.tracks.Create(wnd, 0, 0, 100, 100);
        g.tracks.SetOnChange(apply_tracks);
        g.piano.create(wnd);
        g.log = control(L"edit", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_EDIT_LOG);
        SendMessageW(g.log, EM_SETLIMITTEXT, 0, 0);
        button(L"Clear", ID_BTN_CLEARLOG);
        g.status.create(wnd);

        apply_fonts();
        apply_theme();
        fill_curves();
        fill_devices();
        refresh_list();
        refresh_piano_keys();
        show_song();
        show_speed();
        SetWindowPos(wnd, c.ui.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(wnd, 0, static_cast<BYTE>(c.ui.opacity), LWA_ALPHA);
        SetTimer(wnd, IDT_FRAME, 16, nullptr);
        SetTimer(wnd, IDT_STATS, 500, nullptr);
    }

    void on_command(HWND wnd, int id, int code) {
        auto& c = cfg();
        switch (id) {
        case ID_SEARCH:
            if (code == EN_CHANGE)
                fill_list();
            break;
        case ID_CB_SORT:
            if (code == CBN_SELCHANGE) {
                c.ui.sortMode = static_cast<int>(SendMessageW(g.sortCombo, CB_GETCURSEL, 0, 0));
                refresh_list();
                save_config();
            }
            break;
        case ID_BTN_REFRESH:
            refresh_list();
            break;
        case ID_LB_MIDI:
            if (code == LBN_DBLCLK) {
                int sel = selected();
                if (sel < 0)
                    break;
                if (g.items[sel].folder) {
                    g.dir = g.items[sel].path;
                    SetWindowTextW(g.search, L"");
                    refresh_list();
                }
                else {
                    open_song(g.items[sel].path, false);
                }
            }
            break;
        case ID_BTN_PREV:
            step_song(-1, false);
            break;
        case ID_BTN_NEXT:
            step_song(1, false);
            break;
        case ID_CHK_SHUFFLE:
            g.shuffle = !g.shuffle;
            redraw(id);
            break;
        case ID_SEEK:
            if (code == ui::kSliderCommit && g.song) {
                g.player.seek(static_cast<int64_t>(g.seek.value() / 10000.0 * g.song->length));
                show_time(true);
            }
            else if (code == ui::kSliderChange) {
                show_time(true);
            }
            break;
        case ID_BTN_LOAD:
            load_selected();
            break;
        case ID_BTN_PLAY:
            play_pause();
            break;
        case ID_BTN_RESTART:
            if (live_blocks() || !need_song())
                break;
            start_playing(true);
            say("[RESTART] Done.");
            break;
        case ID_BTN_SKIP:
        case ID_BTN_REW:
            if (live_blocks() || !need_song())
                break;
            g.player.skip(id == ID_BTN_SKIP ? 10'000'000'000 : -10'000'000'000);
            show_time(true);
            break;
        case ID_BTN_KEYUP:
        case ID_BTN_KEYDN: {
            g.key = std::clamp(g.key + (id == ID_BTN_KEYUP ? 1 : -1), -24, 24);
            apply_key();
            show_key();
            say("[KEY] %+d semitones", g.key);
            break;
        }
        case ID_BTN_SPEEDUP:
        case ID_BTN_SPEEDDN: {
            if (live_blocks() || !need_song())
                break;
            double s = g.player.speed() * (id == ID_BTN_SPEEDUP ? 1.1 : 1.0 / 1.1);
            s = std::clamp(s, 0.25, 2.0);
            if (std::fabs(s - 1.0) < 0.05)
                s = 1.0;
            g.player.set_speed(s);
            show_speed();
            break;
        }
        case ID_BTN_OUTPUT:
        case ID_BTN_88KEY:
        case ID_BTN_TRANSPOSEOUT:
        case ID_BTN_VELOCITY:
        case ID_BTN_SUSTAIN:
        case ID_BTN_VOLADJ:
            set_toggle(id);
            break;
        case ID_CB_VELOCITY_CURVE:
            if (code == CBN_DROPDOWN) {
                fill_curves();
            }
            else if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessageW(g.curveCombo, CB_GETCURSEL, 0, 0));
                c.playback.velocityCurveIndex = std::max(0, sel);
                c.playback.velocityCurve = sel < 5 ? static_cast<midi::VelocityCurveType>(sel) : midi::VelocityCurveType::Custom;
                out::reload(false);
                save_config();
            }
            break;
        case ID_BTN_VLCURVE: {
            auto* editor = new VelocityCurveEditor();
            if (!editor->Create(wnd, c.playback.velocityCurveIndex, [](int index) {
                    auto& p = cfg().playback;
                    p.velocityCurveIndex = index;
                    p.velocityCurve = midi::VelocityCurveType::Custom;
                    fill_curves();
                    out::reload(false);
                    save_config();
                    say("[VELOCITY] Saved curve \"%s\", playing with it now", p.customVelocityCurves[static_cast<size_t>(index - 5)].name.c_str());
                }))
                delete editor;
            break;
        }
        case ID_CB_BATCH:
            if (code == CBN_SELCHANGE) {
                int sel = std::clamp(static_cast<int>(SendMessageW(g.batchCombo, CB_GETCURSEL, 0, 0)), 0, 6);
                c.playback.batchMs = kBatchMs[sel];
                g.player.set_batching(c.playback.batchMs);
                save_config();
                show_status();
            }
            break;
        case ID_BTN_LEGIT:
            ShowLegitWindow(wnd, nullptr, on_legit_apply);
            break;
        case ID_BTN_TRANSPOSE:
            if (!need_song())
                break;
            {
                const TransposePlan p = plan_transpose();
                wchar_t text[320];
                swprintf_s(text, L"Key: %hs (%.0f%%)\nSuggested transpose: [%d]\nOut of range: %d -> %d\nShift/Ctrl notes: %d -> %d\nSame-key clashes: %d -> %d",
                    p.key.c_str(), p.keyConfidence * 100.0, p.semitones, p.unshifted.outOfRange, p.outOfRange,
                    p.unshifted.needsModifier, p.needsModifier, p.unshifted.sameKeyCollisions, p.sameKeyCollisions);
                MessageBoxW(wnd, text, L"Transpose Suggestion", MB_OK | MB_ICONINFORMATION);
            }
            break;
        case ID_CB_MIDIDEV:
            if (code == CBN_DROPDOWN) {
                fill_devices();
            }
            else if (code == CBN_SELCHANGE) {
                if (const live::Device* d = chosen_device()) {
                    c.ui.midiDevice = narrow(d->name);
                    save_config();
                    if (live::active())
                        start_live(live::mode());
                }
            }
            break;
        case ID_CB_MIDICH:
            if (code == CBN_SELCHANGE) {
                c.ui.midiChannel = static_cast<int>(SendMessageW(g.chanCombo, CB_GETCURSEL, 0, 0)) - 1;
                live::set_channel(c.ui.midiChannel);
                save_config();
            }
            break;
        case ID_BTN_MIDI2QWERTY:
            toggle_live(out::Mode::Keys);
            break;
        case ID_BTN_MIDICONNECT:
            toggle_live(out::Mode::MidiConnect);
            break;
        case ID_BTN_REFRESH_MIDI:
            fill_devices();
            say("[MIDI Devices] %zu input device%s", g.devices.size(), g.devices.size() == 1 ? "" : "s");
            break;
        case ID_CHK_GAME:
            c.playback.gameWindowOnly = !c.playback.gameWindowOnly;
            input::set_guard(c.playback.gameWindowOnly);
            redraw(id);
            save_config();
            say("[Keys] %s", c.playback.gameWindowOnly ? "Keys only go to the game window" : "Keys go to whatever window is in front");
            break;
        case ID_CHK_TOP:
            c.ui.alwaysOnTop = !c.ui.alwaysOnTop;
            SetWindowPos(wnd, c.ui.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            redraw(id);
            save_config();
            break;
        case ID_CHK_DARK:
            c.ui.darkMode = ui::dark() ? 0 : 1;
            apply_theme();
            save_config();
            break;
        case ID_SLIDER_SUSTAIN:
            c.playback.sustainCutoff = g.cutoff.value();
            SetWindowTextW(g.cutoffValue, std::to_wstring(c.playback.sustainCutoff).c_str());
            out::reload(false);
            if (code == ui::kSliderCommit)
                save_config();
            break;
        case ID_SLIDER_OPACITY:
            c.ui.opacity = std::clamp(g.opacity.value(), 100, 255);
            SetLayeredWindowAttributes(wnd, 0, static_cast<BYTE>(c.ui.opacity), LWA_ALPHA);
            if (code == ui::kSliderCommit)
                save_config();
            break;
        case ID_BTN_FOLDER: {
            std::error_code ec;
            fs::path folder = fs::absolute(g.dir, ec);
            ShellExecuteW(wnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case ID_BTN_STOP:
            emergency_stop();
            break;
        case ID_BTN_CLEARLOG:
            SetWindowTextW(g.log, L"");
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: {
            g.wnd = wnd;
            using DpiFn = UINT(WINAPI*)(HWND);
            auto dpiFor = reinterpret_cast<DpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
            if (dpiFor) {
                g.dpi = static_cast<int>(dpiFor(wnd));
            }
            else {
                HDC dc = GetDC(nullptr);
                g.dpi = GetDeviceCaps(dc, LOGPIXELSX);
                ReleaseDC(nullptr, dc);
            }
            create_controls(wnd);
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_DPICHANGED: {
            g.dpi = HIWORD(wp);
            apply_fonts();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(wnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (cfg().ui.darkMode < 0 && lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet"))
                apply_theme();
            break;
        case WM_MOUSEACTIVATE:
            if (GetWindowLongW(wnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(wnd, &pt);
                if (ChildWindowFromPointEx(wnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT) == g.search) {
                    SetForegroundWindow(wnd);
                    return MA_ACTIVATE;
                }
                return MA_NOACTIVATE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            paint(wnd);
            return 0;
        case WM_MEASUREITEM: {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis->CtlType == ODT_LISTBOX) {
                mis->itemHeight = S(20);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis->CtlType == ODT_LISTBOX) {
                draw_list_item(dis);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                const int id = static_cast<int>(dis->CtlID);
                COLORREF fill = CLR_INVALID;
                if (id == ID_BTN_STOP)
                    fill = ui::pal().warn;
                else if (id == ID_BTN_OUTPUT && cfg().playback.output == midi::OutputMode::MidiConnect)
                    fill = RGB(255, 190, 90);
                else if (id == ID_BTN_OUTPUT && cfg().playback.typedKeys)
                    fill = ui::pal().solo;
                else if (id == ID_BTN_SUSTAIN && cfg().playback.sustain == midi::SustainMode::Up)
                    fill = ui::pal().solo;
                ui::draw_button(dis, is_on(id), g.dpi, fill);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            const ui::Palette& p = ui::pal();
            if (ctl == g.log) {
                SetBkColor(hdc, p.logBg);
                SetTextColor(hdc, p.logText);
                return reinterpret_cast<LRESULT>(g.brLog);
            }
            SetBkColor(hdc, p.panel);
            SetTextColor(hdc, ctl == g.tempoLabel || ctl == g.keyLabel || ctl == g.latency || ctl == g.opacityLabel ? p.subtext : p.text);
            return reinterpret_cast<LRESULT>(g.brPanel);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkColor(hdc, ui::pal().field);
            SetTextColor(hdc, ui::pal().fieldText);
            return reinterpret_cast<LRESULT>(g.brField);
        }
        case WM_COMMAND:
            on_command(wnd, LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_TIMER:
            if (wp == IDT_FRAME) {
                auto now = std::chrono::steady_clock::now();
                double dt = std::chrono::duration<double>(now - g.lastFrame).count();
                g.lastFrame = now;
                auto a = g.player.held(), b = live::held();
                std::array<uint64_t, 2> mask{ a[0] | b[0], a[1] | b[1] };
                g.piano.set_mask(mask);
                g.visual.step(live::active() ? live::hits() : g.player.hits(), mask, dt);
                if (g.liveRows) {
                    g.tracks.Animate(live::channels(), 16, dt);
                }
                else {
                    const Player::Tracks t = g.player.tracks();
                    g.tracks.Animate(t.live.get(), t.count, dt);
                }
                show_time();
                if (playing() || live::active())
                    input::refresh();
                if (playing() && input::target() && input::guard() && !input::target_in_front()) {
                    if (++g.away > 6)
                        target_left();
                }
                else {
                    g.away = 0;
                    if (input::target_in_front())
                        g.awayNoted = false;
                }
                if (g.loading && !g.song) {
                    static int lastPct = -1;
                    int pct = g.loadProgress.load();
                    if (pct != lastPct) {
                        lastPct = pct;
                        std::wstring t = L"Loading " + g.loadingPath.filename().wstring() + L"... " + std::to_wstring(pct) + L"%";
                        g.lcd.set_text(t, L"Reading the file");
                    }
                }
            }
            else if (wp == IDT_STATS) {
                show_status();
                update_focusability();
            }
            else if (wp == IDT_DEVICES) {
                KillTimer(wnd, IDT_DEVICES);
                size_t before = g.devices.size();
                fill_devices();
                if (g.devices.size() != before)
                    say("[MIDI Devices] %zu input device%s", g.devices.size(), g.devices.size() == 1 ? "" : "s");
            }
            return 0;
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVNODES_CHANGED || wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE)
                SetTimer(wnd, IDT_DEVICES, 700, nullptr);
            return TRUE;
        case WM_APP_LOG:
            flush_log();
            return 0;
        case WM_APP_LOADED:
            on_loaded(reinterpret_cast<Loaded*>(lp));
            return 0;
        case WM_APP_PLAYER:
            if (wp == Player::Ended) {
                say("[PLAYBACK] Finished.");
                show_time(true);
                update_focusability();
                if (g.shuffle && !live::active())
                    step_song(1, true);
            }
            return 0;
        case WM_APP_HOTKEY:
            switch (wp) {
            case HK_PLAY: play_pause(); break;
            case HK_REWIND: if (g.song && !live::active()) g.player.skip(-10'000'000'000); break;
            case HK_SKIP: if (g.song && !live::active()) g.player.skip(10'000'000'000); break;
            case HK_STOP: emergency_stop(); break;
            default: break;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            KillTimer(wnd, IDT_FRAME);
            KillTimer(wnd, IDT_STATS);
            g.cancelLoad = true;
            if (g.loader.joinable())
                g.loader.join();
            stop_live(true);
            g.player.shutdown();
            if (g.connectBegun)
                out::sink(out::Mode::MidiConnect).end();
            save_config();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }

    void opt_out_of_throttling() {
        using Fn = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        auto fn = k ? reinterpret_cast<Fn>(GetProcAddress(k, "SetProcessInformation")) : nullptr;
        if (!fn)
            return;
        PROCESS_POWER_THROTTLING_STATE s{};
        s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        s.StateMask = 0;
        if (!fn(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s))) {
            s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            fn(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
        }
    }

    void dpi_aware() {
        using Ctx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto set = reinterpret_cast<Ctx>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
        if (!set || !set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            SetProcessDPIAware();
    }

}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH))
        SetCurrentDirectoryW(fs::path(exe).parent_path().c_str());

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Global\\MIDI++_On_Top");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kClass, nullptr)) {
            if (IsIconic(other))
                ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    dpi_aware();
    const auto started = std::chrono::steady_clock::now();
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdiplus = 0;
    Gdiplus::GdiplusStartup(&gdiplus, &gsi, nullptr);

    std::error_code ec;
    fs::create_directories(L"midi", ec);
    bool fresh = !fs::exists(L"config.json", ec);
    std::vector<std::string> warnings = cfg().loadFromFile("config.json");
    if (fresh || !fs::exists(L"config.json", ec))
        save_config();
    ui::set_dark(cfg().ui.darkMode < 0 ? system_dark() : cfg().ui.darkMode == 1);
    ShowSplashScreen(inst);
    opt_out_of_throttling();
    input::init();
    input::set_guard(cfg().playback.gameWindowOnly);
    out::reload();

    g.inst = inst;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON_SMALL), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    const int sysDpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    HWND wnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, kClass, kTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(880, sysDpi, 96), MulDiv(760, sysDpi, 96), nullptr, nullptr, inst, nullptr);
    if (!wnd) {
        CloseSplashScreen();
        MessageBoxW(nullptr, L"Failed to create main window!", L"Error", MB_ICONERROR);
        return 1;
    }

    g_player = &g.player;
    g.player.set_output(cfg().playback.output);
    g.player.set_batching(cfg().playback.batchMs);
    g.player.start(wnd, WM_APP_PLAYER);
    g.hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, on_foreground, 0, 0, WINEVENT_OUTOFCONTEXT);
    g.hotkeys = std::thread(hotkey_loop);

    say(" ===== MIDI++ v1.0.6 | Developed by Zeph, Tested by Gene =====");
    say("Hotkeys: %s play/pause, %s rewind, %s skip, %s stop all keys",
        input::key_label(cfg().hotkeys.PLAY_PAUSE_KEY).c_str(), input::key_label(cfg().hotkeys.REWIND_KEY).c_str(),
        input::key_label(cfg().hotkeys.SKIP_KEY).c_str(), input::key_label(cfg().hotkeys.EMERGENCY_EXIT_KEY).c_str());
    for (const std::string& w : warnings)
        say("[Config] %s", w.c_str());

    layout();
    auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed < std::chrono::milliseconds(900))
        std::this_thread::sleep_for(std::chrono::milliseconds(900) - elapsed);
    CloseSplashScreen();
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g.quit = true;
    if (g.hotkeys.joinable())
        g.hotkeys.join();
    if (g.hook)
        UnhookWinEvent(g.hook);
    Gdiplus::GdiplusShutdown(gdiplus);
    if (single)
        CloseHandle(single);
    return static_cast<int>(msg.wParam);
}
