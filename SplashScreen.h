#pragma once

#include <Windows.h>
#include <vector>
#include <chrono>
#include <random>

class SplashScreen;

SplashScreen& GetSplashScreen();

void ShowSplashScreen(HINSTANCE hInstance);
void CloseSplashScreen();

struct MusicalNote {
    float x;
    float y;
    float scale;
    float rotation;
    float alpha;
    float velocityX;
    float velocityY;
    float rotationSpeed;
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
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static SplashScreen* s_instance;

private:
    HWND m_hwnd;

    HINSTANCE m_hInstance;

    int m_width;
    int m_height;

    bool m_isActive;

    std::vector<MusicalNote> m_notes;
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_distX;
    std::uniform_real_distribution<float> m_distY;
    std::uniform_real_distribution<float> m_distScale;
    std::uniform_real_distribution<float> m_distRotation;
    std::uniform_real_distribution<float> m_distLifetime;
    std::uniform_int_distribution<int> m_distIsSharp;

    HBITMAP m_hBackBuffer;
    HDC     m_hdcBackBuffer;
    HBITMAP m_hSplashBitmap;
    HBITMAP m_hNoteBitmap;
    HBITMAP m_hSharpBitmap;
    HFONT   m_hFont;

    UINT_PTR m_timerID;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFrameTime;
    float m_deltaTime;

    void CreateBackBuffer();
    void DestroyBackBuffer();
    void LoadResources();
    void ReleaseResources();
    void Update();
    void Render();
    void SpawnNote();
    void DrawNote(HDC hdc, const MusicalNote& note);
    void DrawSplashImage(HDC hdc);

    void DrawTextContent(HDC hdc);

    friend SplashScreen& GetSplashScreen();
};
