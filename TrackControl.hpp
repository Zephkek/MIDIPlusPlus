#pragma once
#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// Forward declarations
class VirtualPianoPlayer;

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

    // Create the control - this is the only call needed from outside
    bool Create(HWND hParentWnd, int x, int y, int width, int height);

    // Set the tracks to display
    void SetTracks(const std::vector<TrackInfo>& tracks);

    // Set reference to player (for callbacks)
    void SetPlayer(VirtualPianoPlayer* player);

    // Refresh the visual representation
    void RefreshDisplay();

    // Process a window message for this control
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // Window procedure for this control
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Get the window handle
    HWND GetWindowHandle() const { return m_hWnd; }

private:
    // Constants for visual layout
    static constexpr int HEADER_HEIGHT = 20;
    static constexpr int TRACK_HEIGHT = 30;
    static constexpr int SCROLLBAR_WIDTH = 16;
    static constexpr int BUTTON_WIDTH = 40;
    static constexpr int BUTTON_HEIGHT = 22;
    static constexpr int BUTTON_MARGIN = 4;
    RECT m_rcTrackUpdate = {};

    // Track columns and their widths
    enum class Column {
        TrackNumber,
        TrackName,
        Channel,
        Program,
        NoteCount,
        Mute,
        Solo
    };

    // Get column width for a specific column
    int GetColumnWidth(Column col) const;

    // Get column position (x-coordinate)
    int GetColumnPosition(Column col) const;

    // Internal draw functions
    void DrawControl(HDC hdc);
    void DrawHeaders(HDC hdc);
    void DrawTrack(HDC hdc, int index, int yPos);
    void DrawButton(HDC hdc, const RECT& rect, bool isPressed, bool isToggled, const char* text);

    // Handle mouse interaction
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);

    // Scrolling helpers
    void UpdateScrollInfo();
    void ScrollTracks(int amount);

    // Hit testing
    int HitTest(int x, int y);
    bool IsButtonHit(Column col, int trackIndex, int x, int y);

    // Internal state
    HWND m_hWnd;
    HWND m_hParentWnd;
    int m_x, m_y, m_width, m_height;

    std::vector<TrackInfo> m_tracks;
    int m_scrollPos;
    int m_maxVisibleTracks;
    int m_hoveredTrack;
    int m_selectedTrack;

    // Button state tracking
    struct ButtonState {
        bool isHovered;
        bool isPressed;
    };

    std::unordered_map<int, ButtonState> m_muteButtonStates;
    std::unordered_map<int, ButtonState> m_soloButtonStates;

    // Font and brush resources
    HFONT m_hFont;
    HFONT m_hBoldFont;
    HBRUSH m_hBackgroundBrush;
    HBRUSH m_hTrackBrush;
    HBRUSH m_hSelectedTrackBrush;
    HBRUSH m_hHeaderBrush;

    VirtualPianoPlayer* m_player;

    void InitializeDefaultTracks();
};