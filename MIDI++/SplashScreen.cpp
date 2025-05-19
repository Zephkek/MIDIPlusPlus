// i probably enjoyed working on this the most LMFAO

#define NOMINMAX
#include <windows.h>
#include <wingdi.h>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
#pragma comment(lib, "Msimg32.lib")

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
    void DrawNote(HDC hdc, const MusicalNote& note);
    void DrawSplashImage(HDC hdc);
    void DrawTextContent(HDC hdc);
    void DrawStaffLines(HDC hdc);
    void ProcessBitmapAlpha(void* pBits, int width, int height, COLORREF transparentColor);

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    int m_width;
    int m_height;
    bool m_isActive;
    HBITMAP m_hBackBuffer;
    HDC m_hdcBackBuffer;
    HBITMAP m_hSplashBitmap;
    HBITMAP m_hNoteBitmap;
    HBITMAP m_hSharpBitmap;
    HFONT m_hFont;
    UINT m_timerID;
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

    void* m_pNoteBitmapBits;
    void* m_pSharpBitmapBits;

public:
    static SplashScreen* s_instance;
};

SplashScreen* SplashScreen::s_instance = nullptr;

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
        });
    splashThread.detach();  
}

HWND SplashScreen::GetHWND() const {
    return m_hwnd;
}

void CloseSplashScreen() {
    if (SplashScreen::s_instance && SplashScreen::s_instance->GetHWND()) {
        PostMessage(SplashScreen::s_instance->GetHWND(), WM_CLOSE, 0, 0);
    }
}

SplashScreen::SplashScreen()
    : m_hwnd(nullptr)
    , m_hInstance(nullptr)
    , m_width(SPLASH_WIDTH)
    , m_height(SPLASH_HEIGHT)
    , m_isActive(false)
    , m_hBackBuffer(nullptr)
    , m_hdcBackBuffer(nullptr)
    , m_hSplashBitmap(nullptr)
    , m_hNoteBitmap(nullptr)
    , m_hSharpBitmap(nullptr)
    , m_hFont(nullptr)
    , m_timerID(0)
    , m_deltaTime(0.0f)
    , m_pNoteBitmapBits(nullptr)
    , m_pSharpBitmapBits(nullptr)
{
    std::random_device rd;
    m_rng = std::mt19937(rd());
    m_distX = std::uniform_real_distribution<float>(0.0f, (float)m_width);
    m_distY = std::uniform_real_distribution<float>(0.0f, (float)m_height);
    m_distScale = std::uniform_real_distribution<float>(0.3f, 1.0f); 
    m_distRotation = std::uniform_real_distribution<float>(0.0f, 360.0f);
    m_distLifetime = std::uniform_real_distribution<float>(1.5f, 4.0f);  
    m_distIsSharp = std::uniform_int_distribution<int>(0, 1);  
}

SplashScreen::~SplashScreen() {
    ReleaseResources();
    DestroyBackBuffer();
}

bool SplashScreen::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SplashScreen::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MIDI++SplashScreen";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON_SMALL));

    if (!RegisterClassEx(&wc)) {
        return false;  // fuck you windows
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - m_width) / 2;
    int y = (screenH - m_height) / 2;

    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        L"MIDI++SplashScreen",
        L"MIDI++",
        WS_POPUP,
        x, y, m_width, m_height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        return false; 
    }

    HRGN hRgn = CreateRoundRectRgn(0, 0, m_width, m_height, 20, 20);
    SetWindowRgn(m_hwnd, hRgn, TRUE);

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    LoadResources();
    CreateBackBuffer();

    return true;
}

LRESULT CALLBACK SplashScreen::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SplashScreen* splash = SplashScreen::s_instance;
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ID && splash) {
            splash->Update();
            splash->Render();
        }
        return 0;
    case WM_CLOSE:
        if (splash) {
            splash->m_isActive = false;
            if (splash->m_timerID) {
                KillTimer(hwnd, splash->m_timerID);
                splash->m_timerID = 0;
            }
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (splash) {
            delete s_instance;
            s_instance = nullptr;
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
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_timerID = SetTimer(m_hwnd, TIMER_ID, 1000 / TARGET_FPS, nullptr);

    Render();

    MSG msg;
    while (m_isActive && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void SplashScreen::Close() {
    if (!m_isActive)
        return;

    m_isActive = false;
    if (m_timerID) {
        KillTimer(m_hwnd, m_timerID);
        m_timerID = 0;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

void SplashScreen::CreateBackBuffer() {
    HDC hdcWindow = GetDC(m_hwnd);
    m_hdcBackBuffer = CreateCompatibleDC(hdcWindow);
    m_hBackBuffer = CreateCompatibleBitmap(hdcWindow, m_width, m_height);
    SelectObject(m_hdcBackBuffer, m_hBackBuffer);
    ReleaseDC(m_hwnd, hdcWindow);
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

void SplashScreen::ProcessBitmapAlpha(void* pBits, int width, int height, COLORREF transparentColor) {
    DWORD transColor = 0x00FF00FF; // RGB(255,0,255)
    DWORD* pixels = (DWORD*)pBits;
    int count = width * height;
    for (int i = 0; i < count; i++) {
        if (pixels[i] == transColor) {
            pixels[i] = 0x00000000; 
        }
        else {
            pixels[i] |= 0xFF000000; // full opaque
        }
    }
}

void SplashScreen::LoadResources() {
    m_hSplashBitmap = (HBITMAP)LoadImage(
        m_hInstance,
        MAKEINTRESOURCE(IDI_APP_ICON),
        IMAGE_ICON,
        128, 128,
        LR_DEFAULTCOLOR
    );

    HDC hdcScreen = GetDC(nullptr);

    {
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 40;
        bmi.bmiHeader.biHeight = -80;  // Negative cuz GDI is weird like that idfk
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        m_hNoteBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_pNoteBitmapBits, NULL, 0);

        HDC hdcNote = CreateCompatibleDC(hdcScreen);
        SelectObject(hdcNote, m_hNoteBitmap);

        RECT r = { 0, 0, 40, 80 };
        HBRUSH magentaBrush = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(hdcNote, &r, magentaBrush);
        DeleteObject(magentaBrush);

        HPEN blackPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(hdcNote, blackPen);
        HGDIOBJ oldBrush = SelectObject(hdcNote, blackBrush);
        Ellipse(hdcNote, 5, 50, 35, 70);  
        MoveToEx(hdcNote, 30, 55, nullptr);
        LineTo(hdcNote, 30, 5); 
        SelectObject(hdcNote, oldPen);
        SelectObject(hdcNote, oldBrush);
        DeleteObject(blackPen);
        DeleteObject(blackBrush);
        DeleteDC(hdcNote);
        ProcessBitmapAlpha(m_pNoteBitmapBits, 40, 80, RGB(255, 0, 255));
    }
    {
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 40;
        bmi.bmiHeader.biHeight = -80;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        m_hSharpBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_pSharpBitmapBits, NULL, 0);

        HDC hdcSharp = CreateCompatibleDC(hdcScreen);
        SelectObject(hdcSharp, m_hSharpBitmap);

        RECT r = { 0, 0, 40, 80 };
        HBRUSH magentaBrush = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(hdcSharp, &r, magentaBrush);
        DeleteObject(magentaBrush);
        HPEN blackPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(hdcSharp, blackPen);
        // im too cheap to use a font lmfao
        MoveToEx(hdcSharp, 12, 20, nullptr);
        LineTo(hdcSharp, 10, 60);
        MoveToEx(hdcSharp, 28, 20, nullptr);
        LineTo(hdcSharp, 30, 60);

        MoveToEx(hdcSharp, 5, 30, nullptr);
        LineTo(hdcSharp, 35, 40);
        MoveToEx(hdcSharp, 5, 45, nullptr);
        LineTo(hdcSharp, 35, 55);

        SelectObject(hdcSharp, oldPen);
        DeleteObject(blackPen);
        DeleteDC(hdcSharp);

        ProcessBitmapAlpha(m_pSharpBitmapBits, 40, 80, RGB(255, 0, 255));
    }

    m_hFont = CreateFont(
        32, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe UI"
    );

    ReleaseDC(nullptr, hdcScreen);
}

void SplashScreen::ReleaseResources() {
    if (m_hSplashBitmap) {
        DeleteObject(m_hSplashBitmap);
        m_hSplashBitmap = nullptr;
    }
    if (m_hNoteBitmap) {
        DeleteObject(m_hNoteBitmap);
        m_hNoteBitmap = nullptr;
        m_pNoteBitmapBits = nullptr;
    }
    if (m_hSharpBitmap) {
        DeleteObject(m_hSharpBitmap);
        m_hSharpBitmap = nullptr;
        m_pSharpBitmapBits = nullptr;
    }
    if (m_hFont) {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
}

void SplashScreen::Update() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime);
    m_lastFrameTime = now;
    m_deltaTime = ms.count() / 1000.0f;
    if (m_deltaTime > 0.1f) m_deltaTime = 0.1f;  // Cap for when Windows decides to take a nap, well it will take it anyway because it SUCKS

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

    RECT rc = { 0, 0, m_width, m_height };
    HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(m_hdcBackBuffer, &rc, bgBrush);
    DeleteObject(bgBrush);

    DrawStaffLines(m_hdcBackBuffer);

    DrawSplashImage(m_hdcBackBuffer);

    for (auto& note : m_notes) {
        DrawNote(m_hdcBackBuffer, note);
    }
    DrawTextContent(m_hdcBackBuffer);
    HDC hdcWindow = GetDC(m_hwnd);
    BitBlt(hdcWindow, 0, 0, m_width, m_height, m_hdcBackBuffer, 0, 0, SRCCOPY);
    ReleaseDC(m_hwnd, hdcWindow);
}
void SplashScreen::SpawnNote() {
    MusicalNote note;
    int edge = rand() % 4;  
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
    case 3: // LEFT 
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
void SplashScreen::DrawNote(HDC hdc, const MusicalNote& note) {
    HDC hdcTemp = CreateCompatibleDC(hdc);
    SelectObject(hdcTemp, note.isSharp ? m_hSharpBitmap : m_hNoteBitmap);

    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)(note.alpha * 255);
    bf.AlphaFormat = AC_SRC_ALPHA;  

    // Matrix math for rotation, prof was right im actually going to use these lmfao
    XFORM xform;
    float rads = DegreesToRadians(note.rotation);
    xform.eM11 = cos(rads) * note.scale;
    xform.eM12 = sin(rads) * note.scale;
    xform.eM21 = -sin(rads) * note.scale;
    xform.eM22 = cos(rads) * note.scale;
    xform.eDx = note.x;
    xform.eDy = note.y;

    int prevMode = SetGraphicsMode(hdc, GM_ADVANCED);
    XFORM oldXform;
    GetWorldTransform(hdc, &oldXform);

    SetWorldTransform(hdc, &xform);

    int w = 40, h = 80;
    AlphaBlend(hdc, -w / 2, -h / 2, w, h,
        hdcTemp, 0, 0, w, h,
        bf);
    SetWorldTransform(hdc, &oldXform);
    SetGraphicsMode(hdc, prevMode);

    DeleteDC(hdcTemp);
}

void SplashScreen::DrawSplashImage(HDC hdc) {
    if (!m_hSplashBitmap)
        return;

    int iconSize = 128; // Big icon energy
    int x = (m_width - iconSize) / 2;
    int y = (m_height - iconSize) / 2 - 30; // Nudged up a bit for balance

    HDC hdcTemp = CreateCompatibleDC(hdc);
    SelectObject(hdcTemp, m_hSplashBitmap);

    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 230;  // Slightly transparent cuz we fancy

    AlphaBlend(hdc, x, y, iconSize, iconSize,
        hdcTemp, 0, 0, iconSize, iconSize,
        bf);

    DeleteDC(hdcTemp);
}

void SplashScreen::DrawTextContent(HDC hdc) {
    SelectObject(hdc, m_hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0)); 
    RECT titleRect = { 0, 0, m_width, m_height / 2 };
    ::DrawTextW(hdc, L"MIDI++", -1, &titleRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

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

    static float jokeTimer = 0.0f;
    jokeTimer += m_deltaTime;
    int jokeIndex = static_cast<int>(jokeTimer / 5) % jokes.size();
    std::wstring jokeText = jokes[jokeIndex];

    HFONT smallFont = CreateFont(
        20, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe UI"
    );
    HFONT oldFont = (HFONT)SelectObject(hdc, smallFont);
    RECT jokeRect = { 0, m_height / 2, m_width, m_height };
    ::DrawTextW(hdc, jokeText.c_str(), -1, &jokeRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    std::wstring versionText = L"v1.0.4.R5 - made by Zeph";
    RECT versionRect = { 0, m_height - 30, m_width, m_height };
    ::DrawTextW(hdc, versionText.c_str(), -1, &versionRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(hdc, oldFont);
    DeleteObject(smallFont);
}


void SplashScreen::DrawStaffLines(HDC hdc) {
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    int staffSpacing = 15;           
    int centerY = m_height / 2;      
    int startY = centerY - 2 * staffSpacing; 

    // exactly 5 because MUSIC THEORY SAYS SO
    for (int i = 0; i < 5; i++) {
        int y = startY + i * staffSpacing;
        MoveToEx(hdc, 0, y, nullptr);
        LineTo(hdc, m_width, y);
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}