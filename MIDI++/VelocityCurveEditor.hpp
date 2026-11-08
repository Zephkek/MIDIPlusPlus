#pragma once
#include <Windows.h>
#include <gdiplus.h>
#include <functional>
#include <vector>
#include <string>
#include <memory>

class VelocityCurveEditor {
public:
    VelocityCurveEditor();
    ~VelocityCurveEditor();
    bool Create(HWND parent, int curveIndex, std::function<void(int)> saved);
    void OnClose();
    void ResetToLinear();

private:
    // Window and control creation
    void CreateControls();
    void CreateButton(const wchar_t* text, int id);
    void LayoutControls();
    void Restyle();

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
    int S(int v) const { return MulDiv(v, dpi, 96); }
    bool InGrid(int x, int y) const;
    float MapIndexToX(int index, const RECT& rect) const;
    float MapVelocityToY(int velocity, const RECT& rect) const;
    int MapXToIndex(int x, const RECT& rect) const;
    int MapYToVelocity(int y) const;
    void SetPoint(int index, int velocity);
    void SmoothNearbyPoints(int center);
    void SmoothCurve();
    void LoadPreset(const std::string& preset);
    void SaveCurve();
    void InvalidateCurveArea();

    // Info display helper functions
    const wchar_t* GetDynamicText(int velocity);
    const wchar_t* GetPlayingFeelText(float ratio);
    char GetKeyForPoint(int point);

private:
    // Window handles
    HWND hwndEditor;
    HWND hwndParent;
    HWND hwndCurveName;
    HWND hwndGridlines;

    // State variables
    std::vector<int> points;
    bool isDragging;
    int selectedPoint;
    int hoveredPoint;
    int dpi;
    bool showGridlines;
    bool tracking;
    std::string currentPreset;
    std::wstring statusText;
    std::function<void(int)> onSaved;
    HBRUSH fieldBrush;

    // Window dimensions and layout
    static constexpr int EDITOR_WIDTH = 720;
    static constexpr int EDITOR_HEIGHT = 500;

    // Margins and spacing
    static constexpr int MARGIN = 14;
    static constexpr int GRID_LEFT_MARGIN = 58;
    static constexpr int GRID_RIGHT_MARGIN = 22;

    // Grid dimensions
    static constexpr int GRID_TOP = 58;
    static constexpr int GRID_BOTTOM = 380;

    // Top controls layout
    static constexpr int TOP_MARGIN = 14;
    static constexpr int TOP_CONTROL_HEIGHT = 28;
    static constexpr int NAME_LABEL_WIDTH = 74;
    static constexpr int NAME_FIELD_WIDTH = 210;
    static constexpr int GRID_TOGGLE_WIDTH = 72;

    // Bottom section
    static constexpr int INFO_TOP = 424;
    static constexpr int BUTTONS_TOP = 456;
    static constexpr int BUTTON_HEIGHT = 30;
    static constexpr int BUTTON_WIDTH = 86;
    static constexpr int BUTTON_GAP = 8;

    // Visual properties
    static constexpr int POINT_COUNT = 32;
    static constexpr int MAX_VELOCITY = 127;
    static constexpr float CURVE_LINE_WIDTH = 2.0f;
    static constexpr int HIT_RADIUS = 9;

    // Font sizes
    static constexpr int LARGE_FONT_SIZE = 13;
    static constexpr int INFO_FONT_SIZE = 12;

    // Static members
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
