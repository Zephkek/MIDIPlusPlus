#pragma once
#include <Windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <memory>

class VelocityCurveEditor {
public:
    VelocityCurveEditor();
    ~VelocityCurveEditor();
    bool Create(HWND parent);
    void OnClose();
    void ResetToLinear();

private:
    // Window and control creation
    void CreateControls();
    void CreateButton(const wchar_t* text, int x, int y, int width, int id);

    // Event handlers
    static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void OnMouseDown(int x, int y);
    void OnMouseMove(int x, int y);
    RECT GetClientRectRect() const;
    void OnMouseUp();

    // Drawing functions
    void DrawGrid(Gdiplus::Graphics& graphics, const RECT& rect);
    void DrawCurve(Gdiplus::Graphics& graphics, const RECT& rect);
    void DrawInfoPanel(HDC hdc);

    // Utility functions
    float MapIndexToX(int index, const RECT& rect) const;
    float MapVelocityToY(int velocity, const RECT& rect) const;
    int MapXToIndex(int x, const RECT& rect) const;
    int MapYToVelocity(int y) const;
    void SmoothNearbyPoints(int center);
    void SmoothCurve();
    void LoadPreset(const std::string& preset);
    void SaveCurve();
    void InvalidateCurveArea();
    RECT GetClientRect() const;

    // Info display helper functions
    const wchar_t* GetDynamicText(int velocity);
    const wchar_t* GetPlayingFeelText(float ratio);
    char GetKeyForPoint(int point);

private:
    // Window handles
    HWND hwndEditor;
    HWND hwndParent;
    HWND hwndCurveName;
    HWND hwndVelocityInfo;
    HWND hwndStepInfo;
    HWND hwndGridlines;

    // State variables
    std::vector<int> points;
    bool isDragging;
    int selectedPoint;
    int hoveredPoint;
    int lastMouseX;
    int lastMouseY;
    HFONT hInfoFont;
    HFONT hLargeFont;
    bool showGridlines;
    std::string currentPreset;
    bool isAltKeyDown;
    bool isBeingDestroyed;

    // Window dimensions and layout
    static constexpr int EDITOR_WIDTH = 900;
    static constexpr int EDITOR_HEIGHT = 700;

    // Margins and spacing
    static constexpr int MARGIN = 20;
    static constexpr float GRID_LEFT_MARGIN = 70.0f;    
    static constexpr float GRID_RIGHT_MARGIN = 30.0f;  

    // Grid dimensions
    static constexpr int GRID_TOP = 70;                 
    static constexpr int GRID_BOTTOM = 550;              

    // Top controls layout
    static constexpr int TOP_MARGIN = 15;
    static constexpr int TOP_CONTROL_HEIGHT = 32;
    static constexpr int NAME_FIELD_WIDTH = 180;
    static constexpr int INFO_FIELD_WIDTH = 500;
    static constexpr int GRID_TOGGLE_WIDTH = 80;
    static constexpr int CONTROL_SPACING = 20;

    // Bottom section
    static constexpr int BUTTONS_TOP = GRID_BOTTOM + 65;
    static constexpr int BUTTON_HEIGHT = 32;
    static constexpr int BUTTON_WIDTH = 100;
    static constexpr int BUTTON_GAP = 15;

    // Visual properties
    static constexpr int POINT_COUNT = 32;
    static constexpr int MAX_VELOCITY = 127;
    static constexpr float CURVE_LINE_WIDTH = 2.0f;
    static constexpr int HIT_RADIUS = 12.0f;

    // Font sizes
    static constexpr int LARGE_FONT_SIZE = 16;
    static constexpr int INFO_FONT_SIZE = 12;

    // Static members
    static inline VelocityCurveEditor* g_activeEditor;
    static constexpr const wchar_t* WINDOW_CLASS_NAME = L"VelocityCurveEditorClass";
    static inline bool isClassRegistered;
    static inline HWND activeWindow;
};

// Control IDs
enum VelocityCurveEditorControls {
    ID_VCURVE_NAME = 2600,
    ID_VCURVE_LINEAR,
    ID_VCURVE_LOG,
    ID_VCURVE_EXP,
    ID_VCURVE_SCURVE,
    ID_VCURVE_SMOOTH,
    ID_VCURVE_RESET,
    ID_VCURVE_SAVE,
    ID_VCURVE_GRIDLINES
};

// Global instance
extern std::unique_ptr<VelocityCurveEditor> g_curveEditor;