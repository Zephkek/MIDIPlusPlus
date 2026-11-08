#include "LegitWindow.hpp"
#include "Widgets.hpp"
#include "config.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace {

    enum : int { ID_ENABLE = 100, ID_TIMING, ID_SKIP, ID_PAUSE, ID_PMIN, ID_PMAX, ID_APPLY, ID_CLOSE };

    HWND g_wnd = nullptr;
    int g_dpi = 96;
    bool g_enabled = false;
    std::function<void()> g_apply;
    HBRUSH g_panel = nullptr, g_field = nullptr;

    int S(int v) { return MulDiv(v, g_dpi, 96); }

    HWND make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font(g_dpi, 12, FW_NORMAL)), TRUE);
        return c;
    }

    void set_num(HWND wnd, int id, double v) {
        SetDlgItemInt(wnd, id, static_cast<UINT>(std::lround(std::max(0.0, v))), FALSE);
    }

    int get_num(HWND wnd, int id, int lo, int hi) {
        BOOL ok = FALSE;
        int v = static_cast<int>(GetDlgItemInt(wnd, id, &ok, FALSE));
        return std::clamp(ok ? v : lo, lo, hi);
    }

    void restyle() {
        if (g_panel)
            DeleteObject(g_panel);
        if (g_field)
            DeleteObject(g_field);
        g_panel = CreateSolidBrush(ui::pal().panel);
        g_field = CreateSolidBrush(ui::pal().field);
    }

    void fill(HWND wnd) {
        const auto& l = midi::Config::getInstance().legit;
        g_enabled = l.ENABLED;
        SetDlgItemTextW(wnd, ID_ENABLE, g_enabled ? L"Legit mode: On" : L"Legit mode: Off");
        InvalidateRect(GetDlgItem(wnd, ID_ENABLE), nullptr, TRUE);
        set_num(wnd, ID_TIMING, l.TIMING_VARIATION * 100.0);
        set_num(wnd, ID_SKIP, l.NOTE_SKIP_CHANCE * 100.0);
        set_num(wnd, ID_PAUSE, l.EXTRA_DELAY_CHANCE * 100.0);
        set_num(wnd, ID_PMIN, l.EXTRA_DELAY_MIN * 1000.0);
        set_num(wnd, ID_PMAX, l.EXTRA_DELAY_MAX * 1000.0);
    }

    void apply(HWND wnd) {
        auto& l = midi::Config::getInstance().legit;
        l.ENABLED = g_enabled;
        l.TIMING_VARIATION = get_num(wnd, ID_TIMING, 0, 100) / 100.0;
        l.NOTE_SKIP_CHANCE = get_num(wnd, ID_SKIP, 0, 100) / 100.0;
        l.EXTRA_DELAY_CHANCE = get_num(wnd, ID_PAUSE, 0, 100) / 100.0;
        int pmin = get_num(wnd, ID_PMIN, 0, 10000), pmax = get_num(wnd, ID_PMAX, 0, 10000);
        if (pmax < pmin)
            std::swap(pmin, pmax);
        l.EXTRA_DELAY_MIN = pmin / 1000.0;
        l.EXTRA_DELAY_MAX = pmax / 1000.0;
        fill(wnd);
        if (g_apply)
            g_apply();
    }

    LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: {
            restyle();
            ui::theme_window(wnd);
            make(wnd, L"static",
                L"Legit mode makes the autoplayer play like a person: the timing drifts a little, "
                L"a few notes get skipped and short pauses show up now and then. "
                L"Live MIDI input is never changed.",
                0, 14, 12, 332, 62, 0);
            make(wnd, L"button", L"", BS_OWNERDRAW | WS_TABSTOP, 14, 80, 160, 26, ID_ENABLE);
            struct Row { const wchar_t* label; int id; const wchar_t* unit; };
            const Row rows[] = {
                { L"Timing variation", ID_TIMING, L"% off the beat" },
                { L"Skipped notes", ID_SKIP, L"% of notes" },
                { L"Random pauses", ID_PAUSE, L"% of chords" },
            };
            int y = 118;
            for (const Row& r : rows) {
                make(wnd, L"static", r.label, 0, 14, y + 3, 110, 20, 0);
                ui::theme_control(make(wnd, L"edit", L"", ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP, 128, y, 50, 22, r.id), L"DarkMode_CFD");
                make(wnd, L"static", r.unit, 0, 186, y + 3, 150, 20, 0);
                y += 30;
            }
            make(wnd, L"static", L"Pause length", 0, 14, y + 3, 110, 20, 0);
            ui::theme_control(make(wnd, L"edit", L"", ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP, 128, y, 50, 22, ID_PMIN), L"DarkMode_CFD");
            make(wnd, L"static", L"to", 0, 186, y + 3, 20, 20, 0);
            ui::theme_control(make(wnd, L"edit", L"", ES_NUMBER | ES_RIGHT | WS_BORDER | WS_TABSTOP, 208, y, 50, 22, ID_PMAX), L"DarkMode_CFD");
            make(wnd, L"static", L"ms", 0, 266, y + 3, 40, 20, 0);
            make(wnd, L"button", L"Apply", BS_OWNERDRAW | WS_TABSTOP, 176, y + 42, 80, 26, ID_APPLY);
            make(wnd, L"button", L"Close", BS_OWNERDRAW | WS_TABSTOP, 266, y + 42, 80, 26, ID_CLOSE);
            fill(wnd);
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            ui::draw_button(dis, dis->CtlID == ID_ENABLE && g_enabled, g_dpi);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case ID_ENABLE:
                g_enabled = !g_enabled;
                SetDlgItemTextW(wnd, ID_ENABLE, g_enabled ? L"Legit mode: On" : L"Legit mode: Off");
                InvalidateRect(GetDlgItem(wnd, ID_ENABLE), nullptr, TRUE);
                break;
            case ID_APPLY:
            case IDOK:
                apply(wnd);
                break;
            case ID_CLOSE:
            case IDCANCEL:
                DestroyWindow(wnd);
                break;
            default:
                break;
            }
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(wnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, g_panel);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wp), ui::pal().text);
            SetBkColor(reinterpret_cast<HDC>(wp), ui::pal().panel);
            return reinterpret_cast<LRESULT>(g_panel);
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wp), ui::pal().fieldText);
            SetBkColor(reinterpret_cast<HDC>(wp), ui::pal().field);
            return reinterpret_cast<LRESULT>(g_field);
        case WM_THEMECHANGED:
            restyle();
            ui::theme_window(wnd);
            for (int id : { ID_TIMING, ID_SKIP, ID_PAUSE, ID_PMIN, ID_PMAX })
                ui::theme_control(GetDlgItem(wnd, id), L"DarkMode_CFD");
            RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
            return 0;
        case WM_CLOSE:
            DestroyWindow(wnd);
            return 0;
        case WM_DESTROY:
            g_wnd = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcW(wnd, msg, wp, lp);
    }

}

void ShowLegitWindow(HWND owner, HFONT, std::function<void()> onApply) {
    g_apply = std::move(onApply);
    if (g_wnd) {
        fill(g_wnd);
        ShowWindow(g_wnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_wnd);
        return;
    }
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"MIDI++Legit";
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(103));
        registered = RegisterClassExW(&wc) != 0;
    }
    using DpiFn = UINT(WINAPI*)(HWND);
    auto dpiFor = reinterpret_cast<DpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    g_dpi = dpiFor ? static_cast<int>(dpiFor(owner)) : 96;
    RECT r{ 0, 0, S(360), S(308) };
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_TOOLWINDOW);
    RECT o{};
    GetWindowRect(owner, &o);
    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"MIDI++Legit", L"Legit Mode", style,
        o.left + S(260), o.top + S(120), r.right - r.left, r.bottom - r.top, owner, nullptr, inst, nullptr);
    ShowWindow(g_wnd, SW_SHOWNORMAL);
}
