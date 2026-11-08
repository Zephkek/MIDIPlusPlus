#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace out { struct Activity; }

class TrackControl {
public:
    struct TrackInfo {
        std::string trackName;
        std::string instrumentName;
        int noteCount = 0;
        int channel = -1;
        int programNumber = -1;
        bool isMuted = false;
        bool isSoloed = false;
        bool isDrums = false;
    };

    TrackControl();
    ~TrackControl();
    bool Create(HWND hParentWnd, int x, int y, int width, int height);
    void SetTracks(const std::vector<TrackInfo>& tracks);
    void SetOnChange(std::function<void()> fn);
    void SetDpi(int dpi);
    void Restyle();
    void Animate(const out::Activity* live, size_t count, double dt);
    const std::vector<TrackInfo>& Tracks() const { return m_tracks; }
    void RefreshDisplay();
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    HWND GetWindowHandle() const { return m_hWnd; }

private:
    int S(int v) const { return MulDiv(v, m_dpi, 96); }
    int HeaderH() const { return S(18); }
    int RowH() const { return S(17); }
    int BtnW() const { return S(16); }
    int BtnH() const { return S(10); }
    void MakeFonts();
    int m_dpi = 96;

    enum class Column {
        Mute,
        Solo,
        TrackNumber,
        Level,
        TrackName,
        Channel,
        Program,
        NoteCount,
        Keys
    };
    int GetColumnWidth(Column col) const;
    int GetColumnPosition(Column col) const;
    void DrawControl(HDC hdc);
    void DrawHeaders(HDC hdc);
    void DrawTrack(HDC hdc, int index, int yPos);
    void DrawLed(HDC hdc, const RECT& rect, COLORREF fill, bool hot);
    void DrawKeys(HDC hdc, const RECT& rect, const std::array<uint64_t, 2>& keys, bool muted);
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void UpdateScrollInfo();
    void ScrollTracks(int amount);
    int HitTest(int x, int y);
    bool IsButtonHit(Column col, int trackIndex, int x, int y);
    void InvalidateRow(int trackIndex);

    HWND m_hWnd;
    HWND m_hParentWnd;
    int m_x, m_y, m_width, m_height;

    std::vector<TrackInfo> m_tracks;
    std::vector<float> m_level;
    std::vector<uint32_t> m_seen;
    std::vector<std::array<uint64_t, 2>> m_keys;
    bool m_primed;
    int m_scrollPos;
    int m_maxVisibleTracks;
    int m_hoveredTrack;
    int m_selectedTrack;
    int m_pressedTrack;
    Column m_pressedColumn;
    int m_hotTrack;
    Column m_hotColumn;

    HFONT m_hFont;
    HFONT m_hBoldFont;

    std::function<void()> m_onChange;

    void InitializeDefaultTracks();
};
