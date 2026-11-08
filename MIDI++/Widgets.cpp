#include "Widgets.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace ui {
namespace {

    const Palette kLight = {
        RGB(240, 240, 243), RGB(255, 255, 255), RGB(218, 218, 224), RGB(232, 232, 238), RGB(246, 246, 249),
        RGB(25, 25, 30), RGB(105, 105, 115), RGB(255, 255, 255), RGB(25, 25, 30),
        RGB(232, 232, 236), RGB(220, 226, 240), RGB(200, 210, 235), RGB(196, 196, 204),
        RGB(100, 230, 100), RGB(0, 0, 0), RGB(60, 170, 60), RGB(255, 200, 190), RGB(90, 20, 10),
        RGB(100, 149, 237), RGB(65, 105, 225), RGB(245, 247, 252), RGB(228, 238, 255),
        RGB(255, 222, 173), RGB(210, 105, 30), RGB(60, 30, 0),
        RGB(232, 250, 232), RGB(30, 80, 30), RGB(204, 232, 204), RGB(0, 0, 0), RGB(60, 170, 60),
        RGB(218, 218, 224), RGB(80, 190, 80), RGB(255, 255, 255),
        RGB(255, 255, 255), RGB(232, 232, 236), RGB(35, 35, 40), RGB(120, 120, 126),
        RGB(110, 225, 110), RGB(40, 170, 60), RGB(95, 95, 105), RGB(230, 230, 230), RGB(0, 110, 200),
        RGB(60, 170, 80), RGB(150, 240, 120), RGB(40, 120, 50), RGB(240, 180, 0), RGB(225, 175, 60),
    };

    const Palette kDark = {
        RGB(22, 22, 25), RGB(32, 32, 36), RGB(58, 58, 66), RGB(46, 46, 52), RGB(40, 40, 46),
        RGB(232, 232, 236), RGB(150, 150, 160), RGB(42, 42, 47), RGB(232, 232, 236),
        RGB(52, 52, 58), RGB(64, 64, 74), RGB(76, 76, 92), RGB(78, 78, 88),
        RGB(52, 160, 72), RGB(255, 255, 255), RGB(90, 200, 110), RGB(130, 50, 45), RGB(255, 225, 220),
        RGB(70, 110, 200), RGB(110, 150, 240), RGB(40, 40, 46), RGB(44, 56, 76),
        RGB(110, 70, 30), RGB(200, 120, 50), RGB(255, 225, 190),
        RGB(16, 20, 16), RGB(120, 225, 120), RGB(46, 90, 56), RGB(255, 255, 255), RGB(90, 210, 110),
        RGB(62, 62, 70), RGB(80, 200, 100), RGB(230, 230, 235),
        RGB(214, 214, 220), RGB(120, 120, 128), RGB(14, 14, 16), RGB(70, 70, 76),
        RGB(90, 215, 110), RGB(40, 170, 70), RGB(60, 60, 70), RGB(200, 200, 205), RGB(0, 90, 180),
        RGB(40, 140, 70), RGB(120, 240, 140), RGB(180, 255, 190), RGB(250, 200, 40), RGB(210, 170, 70),
    };

    bool g_dark = false;

    const Lcd kLcdDark = {
        RGB(24, 24, 28), RGB(30, 30, 34), RGB(54, 54, 62), RGB(236, 236, 240), RGB(226, 226, 232), RGB(118, 118, 128),
        RGB(84, 204, 104), RGB(150, 150, 160), RGB(38, 38, 44),
        RGB(230, 180, 72), RGB(40, 38, 36), RGB(226, 92, 74), RGB(42, 36, 36),
        RGB(222, 84, 68), RGB(58, 58, 66), RGB(232, 180, 64), RGB(58, 58, 66), RGB(36, 36, 41), RGB(36, 48, 40),
        RGB(35, 35, 40), RGB(40, 40, 46), RGB(62, 62, 70), RGB(44, 44, 51), RGB(206, 206, 214), RGB(108, 108, 118),
    };

    const Lcd kLcdLight = {
        RGB(244, 245, 248), RGB(255, 255, 255), RGB(214, 214, 222), RGB(28, 28, 34), RGB(44, 44, 52), RGB(128, 128, 138),
        RGB(64, 176, 86), RGB(105, 105, 115), RGB(226, 227, 232),
        RGB(216, 152, 32), RGB(236, 234, 228), RGB(214, 76, 60), RGB(238, 230, 228),
        RGB(214, 76, 60), RGB(214, 214, 222), RGB(226, 162, 34), RGB(214, 214, 222), RGB(246, 246, 249), RGB(232, 245, 234),
        RGB(245, 246, 250), RGB(236, 236, 241), RGB(222, 222, 228), RGB(182, 182, 190), RGB(52, 52, 60), RGB(160, 160, 168),
    };

    template <class T>
    T* self(HWND hwnd, UINT msg, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* p = static_cast<T*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
            return p;
        }
        return reinterpret_cast<T*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    void register_class(const wchar_t* name, WNDPROC proc) {
        WNDCLASSEXW wc{ sizeof(wc) };
        if (GetClassInfoExW(GetModuleHandleW(nullptr), name, &wc))
            return;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = name;
        RegisterClassExW(&wc);
    }

    template <class Paint>
    void buffered(HWND hwnd, Paint paint) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
    }

    void fill(HDC hdc, const RECT& r, COLORREF c) {
        SetDCBrushColor(hdc, c);
        FillRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    bool is_black(int note) {
        int pc = note % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    Gdiplus::Color gp(COLORREF c) {
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    constexpr uint8_t kSegments[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

    float segments(Gdiplus::Graphics& g, float x, float y, float h, const wchar_t* text, COLORREF on, COLORREF off) {
        const float w = h * 0.52f, t = h * 0.13f, e = t * 0.18f, k = 0.11f;
        Gdiplus::SolidBrush onBrush(gp(on)), offBrush(gp(off));
        auto skew = [&](float px, float py) { return Gdiplus::PointF(px + (y + h - py) * k, py); };
        auto horizontal = [&](float ox, float cy, Gdiplus::Brush* b) {
            const float x1 = ox + t * 0.5f + e, x2 = ox + w - t * 0.5f - e;
            Gdiplus::PointF p[6] = { skew(x1, cy), skew(x1 + t * 0.5f, cy - t * 0.5f), skew(x2 - t * 0.5f, cy - t * 0.5f),
                skew(x2, cy), skew(x2 - t * 0.5f, cy + t * 0.5f), skew(x1 + t * 0.5f, cy + t * 0.5f) };
            g.FillPolygon(b, p, 6);
        };
        auto vertical = [&](float cx, float y1, float y2, Gdiplus::Brush* b) {
            y1 += e;
            y2 -= e;
            Gdiplus::PointF p[6] = { skew(cx, y1), skew(cx + t * 0.5f, y1 + t * 0.5f), skew(cx + t * 0.5f, y2 - t * 0.5f),
                skew(cx, y2), skew(cx - t * 0.5f, y2 - t * 0.5f), skew(cx - t * 0.5f, y1 + t * 0.5f) };
            g.FillPolygon(b, p, 6);
        };
        auto digit = [&](float ox, uint8_t mask) {
            const float top = y + t * 0.5f, mid = y + h * 0.5f, bottom = y + h - t * 0.5f;
            const float left = ox + t * 0.5f, right = ox + w - t * 0.5f;
            horizontal(ox, top, (mask & 0x01) ? &onBrush : &offBrush);
            vertical(right, top, mid, (mask & 0x02) ? &onBrush : &offBrush);
            vertical(right, mid, bottom, (mask & 0x04) ? &onBrush : &offBrush);
            horizontal(ox, bottom, (mask & 0x08) ? &onBrush : &offBrush);
            vertical(left, mid, bottom, (mask & 0x10) ? &onBrush : &offBrush);
            vertical(left, top, mid, (mask & 0x20) ? &onBrush : &offBrush);
            horizontal(ox, mid, (mask & 0x40) ? &onBrush : &offBrush);
        };
        float cx = x;
        for (const wchar_t* c = text; *c; ++c) {
            if (*c >= L'0' && *c <= L'9') {
                digit(cx, kSegments[*c - L'0']);
                cx += w + h * 0.16f;
            }
            else if (*c == L':') {
                const float d = t * 1.05f;
                g.FillEllipse(&onBrush, skew(cx, y + h * 0.32f).X - d * 0.5f, y + h * 0.32f - d * 0.5f, d, d);
                g.FillEllipse(&onBrush, skew(cx, y + h * 0.70f).X - d * 0.5f, y + h * 0.70f - d * 0.5f, d, d);
                cx += h * 0.26f;
            }
            else if (*c == L'/') {
                Gdiplus::Pen pen(gp(on), t * 0.8f);
                pen.SetStartCap(Gdiplus::LineCapRound);
                pen.SetEndCap(Gdiplus::LineCapRound);
                Gdiplus::PointF a = skew(cx + h * 0.06f, y + h - t * 0.5f), b = skew(cx + h * 0.30f, y + t * 0.5f);
                g.DrawLine(&pen, a, b);
                cx += h * 0.50f;
            }
            else {
                digit(cx, 0);
                cx += w + h * 0.16f;
            }
        }
        return cx - x;
    }

}

const Palette& pal() { return g_dark ? kDark : kLight; }
const Lcd& lcd() { return g_dark ? kLcdDark : kLcdLight; }
bool dark() { return g_dark; }
void set_dark(bool on) { g_dark = on; }

int scale(int v, int dpi) {
    return MulDiv(v, dpi, 96);
}

HFONT font(int dpi, int dip, int weight, const wchar_t* face) {
    static std::map<std::tuple<int, int, int, std::wstring>, HFONT> cache;
    auto key = std::make_tuple(dpi, dip, weight, std::wstring(face));
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    LOGFONTW lf{};
    lf.lfHeight = -scale(dip, dpi);
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, face);
    HFONT f = CreateFontIndirectW(&lf);
    cache[key] = f;
    return f;
}

void draw_button(const DRAWITEMSTRUCT* dis, bool on, int dpi, COLORREF override) {
    const Palette& p = pal();
    COLORREF fill = override != CLR_INVALID ? override : on ? p.on : p.button;
    COLORREF edge = on && override == CLR_INVALID ? p.onBorder : p.buttonBorder;
    COLORREF text = on && override == CLR_INVALID ? p.onText : p.text;
    if (dis->itemState & ODS_SELECTED)
        fill = p.buttonDown;
    else if ((dis->itemState & ODS_HOTLIGHT) && !on && override == CLR_INVALID)
        fill = p.buttonHot;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    SetDCBrushColor(hdc, fill);
    FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(hdc, edge);
    FrameRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    wchar_t buf[128] = {};
    GetWindowTextW(dis->hwndItem, buf, 128);
    SetTextColor(hdc, text);
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ old = SelectObject(hdc, font(dpi, 12, on ? FW_SEMIBOLD : FW_NORMAL));
    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (dis->itemState & ODS_FOCUS) {
        InflateRect(&rc, -3, -3);
        DrawFocusRect(hdc, &rc);
    }
    SelectObject(hdc, old);
}

void theme_window(HWND hwnd) {
    BOOL on = g_dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on))))
        DwmSetWindowAttribute(hwnd, 19, &on, sizeof(on));
}

void theme_control(HWND hwnd, const wchar_t* darkClass) {
    SetWindowTheme(hwnd, g_dark ? darkClass : nullptr, nullptr);
    SendMessageW(hwnd, WM_THEMECHANGED, 0, 0);
    InvalidateRect(hwnd, nullptr, TRUE);
}

HWND Slider::create(HWND parent, int id, int lo, int hi, int value) {
    id_ = id;
    lo_ = lo;
    hi_ = std::max(lo + 1, hi);
    value_ = std::clamp(value, lo_, hi_);
    register_class(L"MIDI++Slider", proc);
    hwnd_ = CreateWindowExW(0, L"MIDI++Slider", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void Slider::set(int value) {
    value = std::clamp(value, lo_, hi_);
    if (value == value_)
        return;
    value_ = value;
    if (!drag_)
        InvalidateRect(hwnd_, nullptr, FALSE);
}

int Slider::at(int x) const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int pad = scale(8, dpi_);
    int w = std::max<int>(1, rc.right - 2 * pad);
    double f = std::clamp(static_cast<double>(x - pad) / w, 0.0, 1.0);
    return lo_ + static_cast<int>(std::lround(f * (hi_ - lo_)));
}

void Slider::notify(WORD code) {
    SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(id_, code), reinterpret_cast<LPARAM>(hwnd_));
}

void Slider::paint(HDC hdc, const RECT& rc) {
    const Palette& p = pal();
    fill(hdc, rc, p.panel);
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float pad = static_cast<float>(scale(8, dpi_));
    const float h = static_cast<float>(scale(4, dpi_));
    const float cy = rc.bottom / 2.0f;
    const float w = std::max(1.0f, rc.right - 2 * pad);
    const float x = pad + static_cast<float>(value() - lo_) / static_cast<float>(hi_ - lo_) * w;
    Gdiplus::SolidBrush track(gp(p.sliderTrack));
    Gdiplus::SolidBrush done(gp(p.sliderFill));
    g.FillRectangle(&track, pad, cy - h / 2, w, h);
    g.FillRectangle(&done, pad, cy - h / 2, x - pad, h);
    const float r = static_cast<float>(scale(6, dpi_));
    Gdiplus::SolidBrush knob(gp(p.knob));
    Gdiplus::Pen edge(gp(p.accent), static_cast<float>(scale(2, dpi_)));
    g.FillEllipse(&knob, x - r, cy - r, 2 * r, 2 * r);
    g.DrawEllipse(&edge, x - r, cy - r, 2 * r, 2 * r);
}

LRESULT CALLBACK Slider::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Slider* s = self<Slider>(hwnd, msg, lp);
    if (!s)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        s->hwnd_ = hwnd;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        buffered(hwnd, [s](HDC hdc, const RECT& rc) { s->paint(hdc, rc); });
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        s->drag_ = true;
        s->dragValue_ = s->at(GET_X_LPARAM(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        s->notify(kSliderChange);
        return 0;
    case WM_MOUSEMOVE:
        if (s->drag_) {
            int v = s->at(GET_X_LPARAM(lp));
            if (v != s->dragValue_) {
                s->dragValue_ = v;
                InvalidateRect(hwnd, nullptr, FALSE);
                s->notify(kSliderChange);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->drag_) {
            s->dragValue_ = s->at(GET_X_LPARAM(lp));
            s->value_ = s->dragValue_;
            s->drag_ = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            s->notify(kSliderCommit);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int step = std::max(1, (s->hi_ - s->lo_) / 64);
        s->set(s->value_ + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? step : -step));
        s->notify(kSliderCommit);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (s->drag_) {
            s->drag_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND PianoView::create(HWND parent) {
    register_class(L"MIDI++Piano", proc);
    hwnd_ = CreateWindowExW(0, L"MIDI++Piano", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
        nullptr, GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void PianoView::set_mask(const std::array<uint64_t, 2>& mask) {
    if (mask == mask_)
        return;
    mask_ = mask;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PianoView::set_keys(const std::array<std::wstring, 128>& labels) {
    labels_ = labels;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PianoView::paint(HDC hdc, const RECT& rc) {
    const Palette& p = pal();
    fill(hdc, rc, p.border);
    const int lo = 21, hi = 108;
    int whites = 0;
    for (int n = lo; n <= hi; ++n)
        whites += !is_black(n);
    const double kw = static_cast<double>(rc.right) / whites;
    const int h = rc.bottom;
    const bool labels = kw >= scale(11, dpi_);
    auto held = [&](int n) { return (mask_[n >> 6] >> (n & 63)) & 1; };
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(hdc, font(dpi_, 9, FW_NORMAL));

    int wi = 0;
    for (int n = lo; n <= hi; ++n) {
        if (is_black(n))
            continue;
        RECT k{ static_cast<LONG>(wi * kw), 1, static_cast<LONG>((wi + 1) * kw) - 1, h };
        fill(hdc, k, held(n) ? p.keyHeldWhite : labels_[n].empty() ? p.keyWhiteOff : p.keyWhite);
        if (labels && !labels_[n].empty()) {
            RECT t{ k.left, h - scale(16, dpi_), k.right, h - scale(2, dpi_) };
            SetTextColor(hdc, n == 60 ? p.keyMiddleC : p.keyLabel);
            DrawTextW(hdc, labels_[n].c_str(), -1, &t, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        ++wi;
    }

    wi = 0;
    const double bw = kw * 0.62;
    const int bh = static_cast<int>(h * 0.6);
    for (int n = lo; n <= hi; ++n) {
        if (!is_black(n)) {
            ++wi;
            continue;
        }
        const double cx = wi * kw;
        RECT k{ static_cast<LONG>(cx - bw / 2), 1, static_cast<LONG>(cx + bw / 2), bh };
        fill(hdc, k, held(n) ? p.keyHeldBlack : labels_[n].empty() ? p.keyBlackOff : p.keyBlack);
        if (labels && !labels_[n].empty()) {
            RECT t{ k.left - 2, bh - scale(15, dpi_), k.right + 2, bh - scale(2, dpi_) };
            SetTextColor(hdc, p.keyLabelBlack);
            DrawTextW(hdc, labels_[n].c_str(), -1, &t, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
    }
    SelectObject(hdc, oldFont);
}

LRESULT CALLBACK PianoView::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PianoView* p = self<PianoView>(hwnd, msg, lp);
    if (!p)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        p->hwnd_ = hwnd;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        buffered(hwnd, [p](HDC hdc, const RECT& rc) { p->paint(hdc, rc); });
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Visualizer::create(HWND parent) {
    register_class(L"MIDI++Visualizer", proc);
    hwnd_ = CreateWindowExW(0, L"MIDI++Visualizer", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
        nullptr, GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void Visualizer::step(const std::array<uint32_t, 128>& hits, const std::array<uint64_t, 2>& held, double dt) {
    dt = std::clamp(dt, 0.0, 0.1);
    const float decay = static_cast<float>(std::exp(-4.0 * dt));
    bool any = false;
    for (int n = 0; n < 128; ++n) {
        const uint32_t h = hits[n];
        if ((h >> 8) != seen_[n]) {
            seen_[n] = h >> 8;
            energy_[n] = std::max(energy_[n], 0.25f + 0.75f * static_cast<float>(h & 127) / 127.0f);
        }
        const bool on = (held[n >> 6] >> (n & 63)) & 1;
        energy_[n] = std::max(on ? 0.2f : 0.0f, energy_[n] * decay);
        if (energy_[n] < 0.004f)
            energy_[n] = 0.0f;
        any |= energy_[n] > 0.0f;
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int unit = scale(4, dpi_) + scale(2, dpi_);
    const int count = std::clamp(static_cast<int>(rc.right / std::max(1, unit)), 8, static_cast<int>(bars_.size()));
    const float fall = static_cast<float>(0.5 * dt);
    for (int b = 0; b < count; ++b) {
        const int lo = 21 + b * 88 / count, hi = 21 + (b + 1) * 88 / count;
        float v = 0.0f;
        for (int n = lo; n < std::max(hi, lo + 1); ++n)
            v = std::max(v, energy_[n]);
        bars_[b] = v;
        peaks_[b] = std::max(v, peaks_[b] - fall);
        any |= peaks_[b] > 0.0f;
    }
    for (int b = count; b < static_cast<int>(bars_.size()); ++b)
        bars_[b] = peaks_[b] = 0.0f;
    if (any || !idle_)
        InvalidateRect(hwnd_, nullptr, FALSE);
    idle_ = !any;
}

void Visualizer::paint(HDC hdc, const RECT& rc) {
    const Lcd& l = lcd();
    fill(hdc, rc, l.bg);
    const int bw = scale(4, dpi_), gap = scale(2, dpi_);
    const int count = std::clamp(static_cast<int>(rc.right / std::max(1, bw + gap)), 8, static_cast<int>(bars_.size()));
    const int used = count * (bw + gap) - gap;
    const int x0 = (rc.right - used) / 2;
    const int sh = scale(3, dpi_), sg = std::max(1, scale(1, dpi_));
    const int rows = std::max(4, static_cast<int>((rc.bottom + sg) / (sh + sg)));
    const int top = rc.bottom - (rows * (sh + sg) - sg);
    for (int b = 0; b < count; ++b) {
        const int x = x0 + b * (bw + gap);
        const int litRows = static_cast<int>(std::lround(bars_[b] * 0.88f * rows));
        const int peakRow = static_cast<int>(std::lround(peaks_[b] * 0.88f * rows)) - 1;
        for (int r = 0; r < rows; ++r) {
            const float f = static_cast<float>(r) / static_cast<float>(rows);
            const bool on = r < litRows || (r == peakRow && peakRow > 0);
            COLORREF c = f < 0.74f ? (on ? l.lit : l.rule) : f < 0.9f ? (on ? l.amber : l.amberOff) : (on ? l.red : l.redOff);
            const int y = rc.bottom - (r + 1) * (sh + sg) + sg;
            RECT seg{ x, std::max<LONG>(top, y), x + bw, y + sh };
            fill(hdc, seg, c);
        }
    }
}

LRESULT CALLBACK Visualizer::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Visualizer* v = self<Visualizer>(hwnd, msg, lp);
    if (!v)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        v->hwnd_ = hwnd;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        buffered(hwnd, [v](HDC hdc, const RECT& rc) { v->paint(hdc, rc); });
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Display::create(HWND parent) {
    register_class(L"MIDI++Display", proc);
    hwnd_ = CreateWindowExW(0, L"MIDI++Display", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
        nullptr, GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void Display::set_text(const std::wstring& title, const std::wstring& info) {
    if (title == title_ && info == info_)
        return;
    title_ = title;
    info_ = info;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Display::set_counters(int song, int songs, int64_t posNs, int64_t totalNs, int bpm, bool playing) {
    const int pos = static_cast<int>(std::max<int64_t>(0, posNs) / 1'000'000'000);
    const int total = static_cast<int>(std::max<int64_t>(0, totalNs) / 1'000'000'000);
    if (song == song_ && songs == songs_ && pos == pos_ && total == total_ && bpm == bpm_ && playing == playing_)
        return;
    song_ = song;
    songs_ = songs;
    pos_ = pos;
    total_ = total;
    bpm_ = bpm;
    playing_ = playing;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Display::paint(HDC hdc, const RECT& rc) {
    const Lcd& c = lcd();
    fill(hdc, rc, c.bg);
    SetBkMode(hdc, TRANSPARENT);
    const int pad = scale(9, dpi_);
    RECT title{ pad, scale(4, dpi_), rc.right - pad, scale(21, dpi_) };
    SetTextColor(hdc, c.text);
    HGDIOBJ old = SelectObject(hdc, font(dpi_, 14, FW_SEMIBOLD));
    DrawTextW(hdc, title_.c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT info{ pad, scale(21, dpi_), rc.right - pad, scale(35, dpi_) };
    SetTextColor(hdc, c.dim);
    SelectObject(hdc, font(dpi_, 11, FW_NORMAL));
    DrawTextW(hdc, info_.c_str(), -1, &info, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    wchar_t song[16], pos[16], total[16], bpm[8];
    auto mmss = [](wchar_t* out, size_t n, int s) {
        if (s >= 3600)
            swprintf_s(out, n, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
        else
            swprintf_s(out, n, L"%d:%02d", s / 60, s % 60);
    };
    swprintf_s(song, L"%4d/%4d", std::min(song_, 9999), std::min(songs_, 9999));
    mmss(pos, 16, pos_);
    mmss(total, 16, total_);
    swprintf_s(bpm, L"%3d", std::clamp(bpm_, 0, 999));
    const struct { const wchar_t* text; const wchar_t* label; } groups[] = {
        { song, L"SONG" }, { pos, L"TIME" }, { total, L"TOTAL" }, { bpm, L"BPM" },
    };
    const float h = static_cast<float>(scale(18, dpi_));
    const float top = static_cast<float>(scale(40, dpi_));
    float x = static_cast<float>(pad);
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::SolidBrush on(gp(c.lit));
        const float s = h * 0.62f, cy = top + h * 0.5f;
        if (playing_) {
            Gdiplus::PointF tri[3] = { { x, cy - s * 0.5f }, { x + s * 0.8f, cy }, { x, cy + s * 0.5f } };
            g.FillPolygon(&on, tri, 3);
        }
        else {
            g.FillRectangle(&on, x, cy - s * 0.5f, s * 0.28f, s);
            g.FillRectangle(&on, x + s * 0.5f, cy - s * 0.5f, s * 0.28f, s);
        }
        x += s + static_cast<float>(scale(8, dpi_));
        SelectObject(hdc, font(dpi_, 8, FW_BOLD));
        SetTextColor(hdc, c.label);
        for (const auto& grp : groups) {
            const float w = segments(g, x, top, h, grp.text, c.digit, c.ghost);
            g.Flush();
            RECT l{ static_cast<LONG>(x), static_cast<LONG>(top + h + scale(2, dpi_)), static_cast<LONG>(x + w + scale(20, dpi_)),
                static_cast<LONG>(top + h + scale(12, dpi_)) };
            DrawTextW(hdc, grp.label, -1, &l, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            x += w + static_cast<float>(scale(18, dpi_));
        }
    }
    SelectObject(hdc, old);
}

LRESULT CALLBACK Display::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Display* d = self<Display>(hwnd, msg, lp);
    if (!d)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        d->hwnd_ = hwnd;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        buffered(hwnd, [d](HDC hdc, const RECT& rc) { d->paint(hdc, rc); });
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND StatusBar::create(HWND parent) {
    register_class(L"MIDI++Status", proc);
    hwnd_ = CreateWindowExW(0, L"MIDI++Status", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
        nullptr, GetModuleHandleW(nullptr), this);
    return hwnd_;
}

void StatusBar::set(const std::wstring& left, const std::wstring& right) {
    if (left == left_ && right == right_)
        return;
    left_ = left;
    right_ = right;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK StatusBar::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StatusBar* s = self<StatusBar>(hwnd, msg, lp);
    if (!s)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_NCCREATE:
        s->hwnd_ = hwnd;
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        buffered(hwnd, [s](HDC hdc, const RECT& rc) {
            const Palette& p = pal();
            fill(hdc, rc, p.header);
            RECT top{ 0, 0, rc.right, 1 };
            fill(hdc, top, p.border);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, p.subtext);
            HGDIOBJ old = SelectObject(hdc, font(s->dpi_, 11, FW_NORMAL));
            RECT r = rc;
            r.left += scale(8, s->dpi_);
            r.right -= scale(8, s->dpi_);
            DrawTextW(hdc, s->left_.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            DrawTextW(hdc, s->right_.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, old);
        });
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}
