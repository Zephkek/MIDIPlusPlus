#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>

namespace ui {

struct Palette {
    COLORREF bg, panel, border, grid, header, text, subtext, field, fieldText;
    COLORREF button, buttonHot, buttonDown, buttonBorder, on, onText, onBorder, warn, warnText;
    COLORREF solo, soloBorder, rowHover, rowSelected, drum, drumBorder, drumText;
    COLORREF logBg, logText, select, selectText, accent, sliderTrack, sliderFill, knob;
    COLORREF keyWhite, keyWhiteOff, keyBlack, keyBlackOff, keyHeldWhite, keyHeldBlack, keyLabel, keyLabelBlack, keyMiddleC;
    COLORREF barLow, barHigh, barPeak, star, folder;
};

struct Lcd {
    COLORREF bg, list, bezel, text, digit, label, lit, dim, ghost, amber, amberOff, red, redOff;
    COLORREF mute, muteOff, solo, soloOff, header, hot, hover, rule, keyOff, blackOff, name, muted;
};

const Palette& pal();
const Lcd& lcd();
bool dark();
void set_dark(bool on);
int scale(int v, int dpi);
HFONT font(int dpi, int dip, int weight, const wchar_t* face = L"Segoe UI");
void draw_button(const DRAWITEMSTRUCT* dis, bool on, int dpi, COLORREF fill = CLR_INVALID);
void theme_window(HWND hwnd);
void theme_control(HWND hwnd, const wchar_t* darkClass);

constexpr WORD kSliderChange = 1;
constexpr WORD kSliderCommit = 2;

class Slider {
public:
    HWND create(HWND parent, int id, int lo, int hi, int value);
    void set(int value);
    int value() const { return drag_ ? dragValue_ : value_; }
    bool dragging() const { return drag_; }
    void set_dpi(int dpi) { dpi_ = dpi; InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC hdc, const RECT& rc);
    int at(int x) const;
    void notify(WORD code);
    HWND hwnd_ = nullptr;
    int id_ = 0, dpi_ = 96, lo_ = 0, hi_ = 100, value_ = 0, dragValue_ = 0;
    bool drag_ = false;
};

class PianoView {
public:
    HWND create(HWND parent);
    void set_mask(const std::array<uint64_t, 2>& mask);
    void set_keys(const std::array<std::wstring, 128>& labels);
    void set_dpi(int dpi) { dpi_ = dpi; InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC hdc, const RECT& rc);
    HWND hwnd_ = nullptr;
    int dpi_ = 96;
    std::array<uint64_t, 2> mask_{};
    std::array<std::wstring, 128> labels_{};
};

class Visualizer {
public:
    HWND create(HWND parent);
    void step(const std::array<uint32_t, 128>& hits, const std::array<uint64_t, 2>& held, double dt);
    void set_dpi(int dpi) { dpi_ = dpi; InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC hdc, const RECT& rc);
    HWND hwnd_ = nullptr;
    int dpi_ = 96;
    bool idle_ = true;
    std::array<float, 128> energy_{};
    std::array<uint32_t, 128> seen_{};
    std::array<float, 96> bars_{};
    std::array<float, 96> peaks_{};
};

class Display {
public:
    HWND create(HWND parent);
    void set_text(const std::wstring& title, const std::wstring& info);
    void set_counters(int song, int songs, int64_t posNs, int64_t totalNs, int bpm, bool playing);
    void set_dpi(int dpi) { dpi_ = dpi; InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void paint(HDC hdc, const RECT& rc);
    HWND hwnd_ = nullptr;
    int dpi_ = 96;
    std::wstring title_, info_;
    int song_ = 0, songs_ = 0, pos_ = 0, total_ = 0, bpm_ = 0;
    bool playing_ = false;
};

class StatusBar {
public:
    HWND create(HWND parent);
    void set(const std::wstring& left, const std::wstring& right);
    void set_dpi(int dpi) { dpi_ = dpi; InvalidateRect(hwnd_, nullptr, FALSE); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    HWND hwnd_ = nullptr;
    int dpi_ = 96;
    std::wstring left_, right_;
};

}
