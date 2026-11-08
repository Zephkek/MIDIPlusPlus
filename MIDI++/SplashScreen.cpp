// i probably enjoyed working on this the most LMFAO

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wingdi.h>
#include <objidl.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <memory>
#include "SplashScreen.h"
#include "Widgets.hpp"
#include "resource.h"
#pragma comment(lib, "Msimg32.lib")
#pragma comment(lib, "Shlwapi.lib")

// Resource IDs from your resource file.
#define IDI_ICON1            102
#define IDI_APP_ICON         103
#define IDI_APP_ICON_SMALL   104

constexpr int SPLASH_WIDTH = 400;
constexpr int SPLASH_HEIGHT = 200;
constexpr int TARGET_FPS = 60;
constexpr int TIMER_ID = 1;
constexpr int NOTE_SPAWN_INTERVAL = 200;
constexpr int MAX_NOTES = 50;  // Any more and my GPU would file for divorce
constexpr float PI = 3.14159265359f;  // M_PI
static float DegreesToRadians(float degrees) {
    return degrees * (PI / 180.0f);
}

struct MusicalNote {
    float x, y;
    float velocityX, velocityY;
    float scale;
    float rotation;
    float rotationSpeed;
    float alpha;
    float lifetime;
    float maxLifetime;
    bool isSharp;
};

class SplashScreen {
public:
    SplashScreen();
    ~SplashScreen();

    bool Initialize(HINSTANCE hInstance);
    void Show();
    void Close();
    HWND GetHWND() const;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Update();
    void Render();
    void SpawnNote();

private:
    void LoadResources();  // Where the magic happens (and by magic I mean GDI memory leaks)
    void ReleaseResources();
    void CreateBackBuffer();
    void DestroyBackBuffer();
    void DrawNote(Gdiplus::Graphics& g, const MusicalNote& note);
    void DrawSplashImage(HDC hdc);
    void DrawTextContent(Gdiplus::Graphics& g);
    void DrawStaffLines(Gdiplus::Graphics& g);
    float S(float v) const { return v * m_scale; }

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    int m_width;
    int m_height;
    int m_pw;
    int m_ph;
    float m_scale;
    bool m_isActive;
    bool m_closing;
    float m_fade;
    float m_time;
    HBITMAP m_hBackBuffer;
    HDC m_hdcBackBuffer;
    void* m_pBackBits;
    HBITMAP m_hSplashBitmap;
    int m_logoW;
    int m_logoH;
    std::unique_ptr<Gdiplus::GraphicsPath> m_notePath;
    std::unique_ptr<Gdiplus::GraphicsPath> m_sharpPath;
    std::unique_ptr<Gdiplus::FontFamily> m_family;
    std::unique_ptr<Gdiplus::Font> m_titleFont;
    std::unique_ptr<Gdiplus::Font> m_textFont;
    std::unique_ptr<Gdiplus::Font> m_smallFont;
    UINT_PTR m_timerID;
    float m_deltaTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    std::vector<MusicalNote> m_notes;

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_distX;
    std::uniform_real_distribution<float> m_distY;
    std::uniform_real_distribution<float> m_distScale;
    std::uniform_real_distribution<float> m_distRotation;
    std::uniform_real_distribution<float> m_distLifetime;
    std::uniform_int_distribution<int> m_distIsSharp;
    std::uniform_int_distribution<int> m_distEdge;

public:
    static SplashScreen* s_instance;
};

SplashScreen* SplashScreen::s_instance = nullptr;
static std::atomic<bool> s_closeRequested{ false };
static std::atomic<HWND> s_hwnd{ nullptr };

namespace {

    Gdiplus::Color tone(COLORREF c, int alpha = 255) {
        return Gdiplus::Color(static_cast<BYTE>(std::clamp(alpha, 0, 255)), GetRValue(c), GetGValue(c), GetBValue(c));
    }

    void rounded(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
        const float d = r * 2.0f;
        p.AddArc(x, y, d, d, 180.0f, 90.0f);
        p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
        p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
        p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
    }

    UINT monitor_dpi(HMONITOR mon) {
        using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static Fn fn = [] {
            HMODULE m = LoadLibraryW(L"shcore.dll");
            return m ? reinterpret_cast<Fn>(GetProcAddress(m, "GetDpiForMonitor")) : nullptr;
        }();
        UINT x = 96, y = 96;
        if (!fn || FAILED(fn(mon, 0, &x, &y))) {
            HDC dc = GetDC(nullptr);
            x = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
            ReleaseDC(nullptr, dc);
        }
        return x ? x : 96;
    }

}

SplashScreen& GetSplashScreen() {
    if (!SplashScreen::s_instance) {
        SplashScreen::s_instance = new SplashScreen();
    }
    return *SplashScreen::s_instance;
}

void ShowSplashScreen(HINSTANCE hInstance) {
    std::thread splashThread([hInstance]() {
        auto& splash = GetSplashScreen();
        if (splash.Initialize(hInstance)) {
            splash.Show();
        }
        delete SplashScreen::s_instance;
        SplashScreen::s_instance = nullptr;
        });
    splashThread.detach();
}

HWND SplashScreen::GetHWND() const {
    return m_hwnd;
}

void CloseSplashScreen() {
    s_closeRequested = true;
    if (HWND hwnd = s_hwnd.load()) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

SplashScreen::SplashScreen()
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_width(SPLASH_WIDTH)
    , m_height(SPLASH_HEIGHT)
    , m_pw(SPLASH_WIDTH)
    , m_ph(SPLASH_HEIGHT)
    , m_scale(1.0f)
    , m_isActive(false)
    , m_closing(false)
    , m_fade(0.0f)
    , m_time(0.0f)
    , m_hBackBuffer(nullptr)
    , m_hdcBackBuffer(nullptr)
    , m_pBackBits(nullptr)
    , m_hSplashBitmap(nullptr)
    , m_logoW(0)
    , m_logoH(0)
    , m_timerID(0)
    , m_deltaTime(0.0f)
{
    std::random_device rd;
    m_rng = std::mt19937(rd());
    m_distX = std::uniform_real_distribution<float>(0.0f, (float)m_width);
    m_distY = std::uniform_real_distribution<float>(0.0f, (float)m_height);
    m_distScale = std::uniform_real_distribution<float>(0.3f, 1.0f);
    m_distRotation = std::uniform_real_distribution<float>(0.0f, 360.0f);
    m_distLifetime = std::uniform_real_distribution<float>(1.5f, 4.0f);
    m_distIsSharp = std::uniform_int_distribution<int>(0, 1);
    m_distEdge = std::uniform_int_distribution<int>(0, 3);
}

SplashScreen::~SplashScreen() {
    ReleaseResources();
    DestroyBackBuffer();
}

bool SplashScreen::Initialize(HINSTANCE hInstance) {
    if (s_closeRequested)
        return false;
    m_hInstance = hInstance;

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = SplashScreen::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MIDI++SplashScreen";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON_SMALL));

    if (!RegisterClassEx(&wc)) {
        return false;  // fuck you windows
    }

    POINT origin = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    m_scale = monitor_dpi(mon) / 96.0f;
    m_pw = static_cast<int>(std::lround(S(static_cast<float>(m_width))));
    m_ph = static_cast<int>(std::lround(S(static_cast<float>(m_height))));
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - m_pw) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - m_ph) / 2;

    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"MIDI++SplashScreen",
        L"MIDI++",
        WS_POPUP,
        x, y, m_pw, m_ph,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        return false;
    }

    s_hwnd = m_hwnd;
    LoadResources();
    CreateBackBuffer();

    return m_hdcBackBuffer != nullptr;
}

LRESULT CALLBACK SplashScreen::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SplashScreen* splash = SplashScreen::s_instance;
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ID && splash) {
            if (s_closeRequested)
                splash->m_closing = true;
            splash->Update();
            if (splash->m_closing && splash->m_fade <= 0.0f) {
                DestroyWindow(hwnd);
                return 0;
            }
            splash->Render();
        }
        return 0;
    case WM_CLOSE:
        if (splash)
            splash->m_closing = true;
        return 0;
    case WM_DESTROY:
        s_hwnd = nullptr;
        if (splash) {
            splash->m_isActive = false;
            if (splash->m_timerID) {
                KillTimer(hwnd, splash->m_timerID);
                splash->m_timerID = 0;
            }
            splash->m_hwnd = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void SplashScreen::Show() {
    if (!m_hwnd || m_isActive)
        return;

    m_isActive = true;
    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    Render();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_timerID = SetTimer(m_hwnd, TIMER_ID, 1000 / TARGET_FPS, nullptr);

    MSG msg;
    while (m_isActive && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void SplashScreen::Close() {
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

void SplashScreen::CreateBackBuffer() {
    HDC hdcWindow = GetDC(m_hwnd);
    m_hdcBackBuffer = CreateCompatibleDC(hdcWindow);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_pw;
    bmi.bmiHeader.biHeight = -m_ph;  // Negative cuz GDI is weird like that idfk
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    m_hBackBuffer = CreateDIBSection(hdcWindow, &bmi, DIB_RGB_COLORS, &m_pBackBits, NULL, 0);
    if (m_hBackBuffer)
        SelectObject(m_hdcBackBuffer, m_hBackBuffer);
    ReleaseDC(m_hwnd, hdcWindow);
    if (!m_hBackBuffer) {
        DeleteDC(m_hdcBackBuffer);
        m_hdcBackBuffer = nullptr;
    }
}

void SplashScreen::DestroyBackBuffer() {
    if (m_hdcBackBuffer) {
        DeleteDC(m_hdcBackBuffer);
        m_hdcBackBuffer = nullptr;
    }
    if (m_hBackBuffer) {
        DeleteObject(m_hBackBuffer);
        m_hBackBuffer = nullptr;
    }
}

void SplashScreen::LoadResources() {
    if (HRSRC res = FindResourceW(m_hInstance, MAKEINTRESOURCEW(IDR_LOGO), RT_RCDATA)) {
        HGLOBAL data = LoadResource(m_hInstance, res);
        const BYTE* bytes = data ? static_cast<const BYTE*>(LockResource(data)) : nullptr;
        IStream* stream = bytes ? SHCreateMemStream(bytes, SizeofResource(m_hInstance, res)) : nullptr;
        if (stream) {
            Gdiplus::Bitmap logo(stream);
            if (logo.GetLastStatus() == Gdiplus::Ok && logo.GetWidth()) {
                m_logoH = static_cast<int>(std::lround(S(92.0f)));
                m_logoW = static_cast<int>(std::lround(m_logoH * static_cast<float>(logo.GetWidth()) / logo.GetHeight()));
                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = m_logoW;
                bmi.bmiHeader.biHeight = -m_logoH;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HDC hdcScreen = GetDC(nullptr);
                m_hSplashBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                ReleaseDC(nullptr, hdcScreen);
                if (m_hSplashBitmap) {
                    Gdiplus::Bitmap target(m_logoW, m_logoH, m_logoW * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
                    Gdiplus::Graphics g(&target);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                    g.Clear(Gdiplus::Color(0, 0, 0, 0));
                    g.DrawImage(&logo, 0, 0, m_logoW, m_logoH);
                }
            }
            stream->Release();
        }
    }

    m_notePath = std::make_unique<Gdiplus::GraphicsPath>();
    m_notePath->AddEllipse(-15.0f, 10.0f, 30.0f, 20.0f);
    m_notePath->AddRectangle(Gdiplus::RectF(8.5f, -35.0f, 3.0f, 51.0f));

    m_sharpPath = std::make_unique<Gdiplus::GraphicsPath>();
    // im too cheap to use a font lmfao
    m_sharpPath->StartFigure();
    m_sharpPath->AddLine(-8.0f, -20.0f, -10.0f, 20.0f);
    m_sharpPath->StartFigure();
    m_sharpPath->AddLine(8.0f, -20.0f, 10.0f, 20.0f);
    m_sharpPath->StartFigure();
    m_sharpPath->AddLine(-15.0f, -10.0f, 15.0f, 0.0f);
    m_sharpPath->StartFigure();
    m_sharpPath->AddLine(-15.0f, 5.0f, 15.0f, 15.0f);

    m_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI");
    m_titleFont = std::make_unique<Gdiplus::Font>(m_family.get(), S(34.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    m_textFont = std::make_unique<Gdiplus::Font>(m_family.get(), S(13.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    m_smallFont = std::make_unique<Gdiplus::Font>(m_family.get(), S(11.0f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
}

void SplashScreen::ReleaseResources() {
    if (m_hSplashBitmap) {
        DeleteObject(m_hSplashBitmap);
        m_hSplashBitmap = nullptr;
    }
    m_notePath.reset();
    m_sharpPath.reset();
    m_titleFont.reset();
    m_textFont.reset();
    m_smallFont.reset();
    m_family.reset();
}

void SplashScreen::Update() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime);
    m_lastFrameTime = now;
    m_deltaTime = ms.count() / 1000.0f;
    if (m_deltaTime > 0.1f) m_deltaTime = 0.1f;  // Cap for when Windows decides to take a nap, well it will take it anyway because it SUCKS
    m_time += m_deltaTime;
    m_fade = m_closing ? std::max(0.0f, m_fade - m_deltaTime / 0.15f) : std::min(1.0f, m_fade + m_deltaTime / 0.2f);

    for (auto it = m_notes.begin(); it != m_notes.end();) {
        it->lifetime -= m_deltaTime;
        it->x += it->velocityX * m_deltaTime;
        it->y += it->velocityY * m_deltaTime;
        it->rotation += it->rotationSpeed * m_deltaTime;  // SPINNNNNNN

        float normLife = it->lifetime / it->maxLifetime;
        if (normLife < 0.2f) {
            it->alpha = normLife / 0.2f;
        }
        else if (normLife > 0.8f) {
            it->alpha = (1.0f - normLife) / 0.2f;
        }
        else {
            it->alpha = 1.0f;
        }

        if (it->lifetime <= 0.0f) {
            it = m_notes.erase(it);  // Gone, reduced to atoms
        }
        else {
            ++it;
        }
    }

    static float timeSinceSpawn = 0.0f;
    timeSinceSpawn += (m_deltaTime * 1000.0f);
    if (timeSinceSpawn >= NOTE_SPAWN_INTERVAL && m_notes.size() < MAX_NOTES) {
        SpawnNote();
        timeSinceSpawn = 0.0f;
    }
}

void SplashScreen::Render() {
    if (!m_hwnd || !m_hdcBackBuffer)
        return;

    const ui::Palette& p = ui::pal();
    const float radius = S(10.0f);
    {
        Gdiplus::Bitmap surface(m_pw, m_ph, m_pw * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(m_pBackBits));
        Gdiplus::Graphics g(&surface);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        Gdiplus::GraphicsPath panel;
        rounded(panel, 0.5f, 0.5f, m_pw - 1.0f, m_ph - 1.0f, radius);
        Gdiplus::SolidBrush fill(tone(p.panel));
        g.FillPath(&fill, &panel);
        g.SetClip(&panel);

        DrawStaffLines(g);

        for (auto& note : m_notes) {
            DrawNote(g, note);
        }

        const float track = S(3.0f);
        Gdiplus::SolidBrush rail(tone(p.sliderTrack));
        g.FillRectangle(&rail, 0.0f, m_ph - track, static_cast<float>(m_pw), track);
        const float span = m_pw * 0.28f;
        const float phase = std::fmod(m_time, 1.4f) / 1.4f;
        Gdiplus::SolidBrush run(tone(p.accent));
        g.FillRectangle(&run, -span + phase * (m_pw + span), m_ph - track, span, track);
        g.ResetClip();

        Gdiplus::Pen edge(tone(p.buttonBorder), 1.0f);
        g.DrawPath(&edge, &panel);
    }
    DrawSplashImage(m_hdcBackBuffer);
    {
        Gdiplus::Bitmap surface(m_pw, m_ph, m_pw * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(m_pBackBits));
        Gdiplus::Graphics g(&surface);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        DrawTextContent(g);
    }

    POINT src = { 0, 0 };
    SIZE size = { m_pw, m_ph };
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = static_cast<BYTE>(std::lround(255.0f * m_fade * m_fade * (3.0f - 2.0f * m_fade)));
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(m_hwnd, nullptr, nullptr, &size, m_hdcBackBuffer, &src, 0, &bf, ULW_ALPHA);
}

void SplashScreen::SpawnNote() {
    MusicalNote note;
    int edge = m_distEdge(m_rng);
    switch (edge) {
    case 0: // TOP
        note.x = m_distX(m_rng);
        note.y = -40.0f;
        note.velocityX = m_distX(m_rng) * 0.1f - (m_width * 0.05f);
        note.velocityY = m_distX(m_rng) * 0.1f + 40.0f;
        break;
    case 1: // RIGHT
        note.x = m_width + 40.0f;
        note.y = m_distY(m_rng);
        note.velocityX = -(m_distX(m_rng) * 0.1f + 40.0f);
        note.velocityY = m_distY(m_rng) * 0.1f - (m_height * 0.05f);
        break;
    case 2: // BOTTOM
        note.x = m_distX(m_rng);
        note.y = m_height + 40.0f;
        note.velocityX = m_distX(m_rng) * 0.1f - (m_width * 0.05f);
        note.velocityY = -(m_distY(m_rng) * 0.1f + 40.0f);
        break;
    default: // LEFT
        note.x = -40.0f;
        note.y = m_distY(m_rng);
        note.velocityX = m_distX(m_rng) * 0.1f + 40.0f;
        note.velocityY = m_distY(m_rng) * 0.1f - (m_height * 0.05f);
        break;
    }
    note.scale = m_distScale(m_rng);
    note.rotation = m_distRotation(m_rng);
    note.rotationSpeed = (m_distRotation(m_rng) - 180.0f) * 0.4f;  // Some spin left, some right
    note.alpha = 0.0f;
    note.maxLifetime = m_distLifetime(m_rng);
    note.lifetime = note.maxLifetime;
    note.isSharp = (m_distIsSharp(m_rng) == 1);

    m_notes.push_back(note);
}

void SplashScreen::DrawNote(Gdiplus::Graphics& g, const MusicalNote& note) {
    // Matrix math for rotation, prof was right im actually going to use these lmfao
    XFORM xform;
    float rads = DegreesToRadians(note.rotation);
    xform.eM11 = cos(rads) * note.scale;
    xform.eM12 = sin(rads) * note.scale;
    xform.eM21 = -sin(rads) * note.scale;
    xform.eM22 = cos(rads) * note.scale;
    xform.eDx = note.x;
    xform.eDy = note.y;

    Gdiplus::Matrix m(S(xform.eM11), S(xform.eM12), S(xform.eM21), S(xform.eM22), S(xform.eDx), S(xform.eDy));
    g.SetTransform(&m);
    const Gdiplus::Color c = tone(ui::pal().accent, static_cast<int>(note.alpha * (ui::dark() ? 120.0f : 150.0f)));
    if (note.isSharp) {
        Gdiplus::Pen pen(c, 3.0f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawPath(&pen, m_sharpPath.get());
    }
    else {
        Gdiplus::SolidBrush brush(c);
        g.FillPath(&brush, m_notePath.get());
    }
    g.ResetTransform();
}

void SplashScreen::DrawSplashImage(HDC hdc) {
    if (!m_hSplashBitmap)
        return;

    int iconSize = m_logoH; // Big icon energy
    int x = static_cast<int>(std::lround(S(34.0f)));
    int y = (m_ph - iconSize) / 2 - static_cast<int>(std::lround(S(6.0f))); // Nudged up a bit for balance

    HDC hdcTemp = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(hdcTemp, m_hSplashBitmap);

    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 230;  // Slightly transparent cuz we fancy
    bf.AlphaFormat = AC_SRC_ALPHA;

    AlphaBlend(hdc, x, y, m_logoW, iconSize,
        hdcTemp, 0, 0, m_logoW, iconSize,
        bf);

    SelectObject(hdcTemp, old);
    DeleteDC(hdcTemp);
}

void SplashScreen::DrawTextContent(Gdiplus::Graphics& g) {
    const ui::Palette& p = ui::pal();
    Gdiplus::SolidBrush title(tone(p.text));
    Gdiplus::SolidBrush sub(tone(p.subtext));
    const float left = S(150.0f);
    g.DrawString(L"MIDI++", -1, m_titleFont.get(), Gdiplus::PointF(left - S(2.0f), S(50.0f)), &title);

    static std::vector<std::wstring> jokes = {
        L"Tuning the piano keys...",
        L"Warming up fingers...",
        L"Practicing scales...",
        L"Counting beats...",
        L"Finding middle C...",
        L"Adjusting the metronome...",
        L"Looking for sheet music...",
        L"Quiet please, pianist thinking...",
        L"Almost concert-ready...",
        L"Sharpening flats, flattening sharps...",
    };

    static float jokeTimer = std::uniform_real_distribution<float>(0.0f, 5.0f * jokes.size())(m_rng);
    jokeTimer += m_deltaTime;
    int jokeIndex = static_cast<int>(jokeTimer / 5) % jokes.size();
    std::wstring jokeText = jokes[jokeIndex];
    g.DrawString(jokeText.c_str(), -1, m_textFont.get(), Gdiplus::PointF(left, S(100.0f)), &sub);

    std::wstring versionText = L"v1.0.6 - made by Zeph";
    Gdiplus::StringFormat right;
    right.SetAlignment(Gdiplus::StringAlignmentFar);
    g.DrawString(versionText.c_str(), -1, m_smallFont.get(),
        Gdiplus::RectF(0.0f, m_ph - S(28.0f), m_pw - S(14.0f), S(18.0f)), &right, &sub);
}

void SplashScreen::DrawStaffLines(Gdiplus::Graphics& g) {
    Gdiplus::Pen pen(tone(ui::pal().border, ui::dark() ? 255 : 200), S(1.5f));

    float staffSpacing = S(15.0f);
    float centerY = m_ph / 2.0f;
    float startY = centerY - 2 * staffSpacing;

    // exactly 5 because MUSIC THEORY SAYS SO
    for (int i = 0; i < 5; i++) {
        float y = startY + i * staffSpacing;
        g.DrawLine(&pen, 0.0f, y, static_cast<float>(m_pw), y);
    }
}
