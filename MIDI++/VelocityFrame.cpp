#include "PlaybackSystem.hpp"
#include "VelocityCurveEditor.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

// For GDI+ drawing:
#include <GdiPlus.h>
#include <windowsx.h>
#pragma comment(lib, "Gdiplus.lib")

constexpr int POINT_COUNT = 33;     // number of control points
constexpr int MAX_VELOCITY = 127;
constexpr int HIT_RADIUS = 6;      // radius (in pixels) for hit detection

// Grid margins and positions
constexpr int GRID_LEFT_MARGIN = 50;
constexpr int GRID_RIGHT_MARGIN = 20;
constexpr int GRID_TOP = 20;
constexpr int GRID_BOTTOM = 220;    // bottom of grid (vertical pixel coordinate)

// Editor window and control sizes
constexpr int EDITOR_WIDTH = 500;
constexpr int EDITOR_HEIGHT = 350;
constexpr int TOP_MARGIN = 10;
constexpr int NAME_FIELD_WIDTH = 150;
constexpr int TOP_CONTROL_HEIGHT = 30;
constexpr int INFO_FIELD_WIDTH = 250;
constexpr int BUTTON_WIDTH = 70;
constexpr int BUTTON_HEIGHT = 30;
constexpr int BUTTON_GAP = 10;
constexpr int BUTTONS_TOP = 280;
constexpr int MARGIN = 10;
constexpr int GRID_TOGGLE_WIDTH = 60;
constexpr int INFO_FONT_SIZE = 20;
constexpr int LARGE_FONT_SIZE = 22;

//------------------------------------------------------------------------------
// Helper function to create a font (wrapper around CreateFontIndirectW)
HFONT CreateCustomFont(int height, int weight = FW_NORMAL, LPCWSTR faceName = L"Segoe UI")
{
    LOGFONTW lf = {};
    lf.lfHeight = height;
    lf.lfWeight = weight;
    wcscpy_s(lf.lfFaceName, faceName);
    return CreateFontIndirectW(&lf);
}

//------------------------------------------------------------------------------
// VelocityCurveEditor implementation

std::unique_ptr<VelocityCurveEditor> g_curveEditor;

VelocityCurveEditor::VelocityCurveEditor() :
    hwndEditor(nullptr),
    hwndParent(nullptr),
    hwndCurveName(nullptr),
    hwndVelocityInfo(nullptr),
    hwndStepInfo(nullptr),
    hwndGridlines(nullptr),
    points(POINT_COUNT),
    isDragging(false),
    selectedPoint(-1),
    hoveredPoint(-1),
    lastMouseX(-1),
    lastMouseY(-1),
    hInfoFont(nullptr),
    hLargeFont(nullptr),
    showGridlines(true),
    currentPreset("Linear"),
    isAltKeyDown(false),
    isBeingDestroyed(false)
{
    ResetToLinear();
}

void VelocityCurveEditor::DrawGrid(Gdiplus::Graphics& graphics, const RECT& rect) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    const Gdiplus::Color gridColor(70, 180, 180, 180);      // major grid lines
    const Gdiplus::Color minorGridColor(40, 180, 180, 180);   // minor grid lines
    const Gdiplus::Color borderColor(140, 60, 60, 60);
    const Gdiplus::Color textColor(255, 45, 45, 45);
    Gdiplus::Pen gridPen(gridColor, 1);
    Gdiplus::Pen minorGridPen(minorGridColor, 1);
    Gdiplus::Pen borderPen(borderColor, 2);
    Gdiplus::SolidBrush textBrush(textColor);
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font axisFont(&fontFamily, 9, Gdiplus::FontStyleRegular);
    Gdiplus::Font labelFont(&fontFamily, 11, Gdiplus::FontStyleBold);

    const Gdiplus::REAL gridWidth = rect.right - GRID_LEFT_MARGIN - GRID_RIGHT_MARGIN;
    const Gdiplus::REAL gridHeight = GRID_BOTTOM - GRID_TOP;

    Gdiplus::SolidBrush gridBgBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::RectF gridRectF(GRID_LEFT_MARGIN, GRID_TOP, gridWidth, gridHeight);
    graphics.FillRectangle(&gridBgBrush, gridRectF);

    for (int i = 0; i <= POINT_COUNT; i++) {
        Gdiplus::REAL x = MapIndexToX(i, rect);
        graphics.DrawLine(&minorGridPen, x, static_cast<Gdiplus::REAL>(GRID_TOP), x, static_cast<Gdiplus::REAL>(GRID_BOTTOM));
    }
    // Draw minor horizontal grid lines (every 8 velocity units)
    for (int v = 0; v <= MAX_VELOCITY; v += 8) {
        Gdiplus::REAL y = MapVelocityToY(v, rect);
        graphics.DrawLine(&minorGridPen, GRID_LEFT_MARGIN, y, static_cast<Gdiplus::REAL>(rect.right - GRID_RIGHT_MARGIN), y);
    }
    // Draw major vertical grid lines (every 4 points)
    for (int i = 0; i <= POINT_COUNT; i += 4) {
        Gdiplus::REAL x = MapIndexToX(i, rect);
        graphics.DrawLine(&gridPen, x, static_cast<Gdiplus::REAL>(GRID_TOP), x, static_cast<Gdiplus::REAL>(GRID_BOTTOM));
    }
    // Draw major horizontal grid lines (every 16 velocity units)
    for (int v = 0; v <= MAX_VELOCITY; v += 16) {
        Gdiplus::REAL y = MapVelocityToY(v, rect);
        graphics.DrawLine(&gridPen, GRID_LEFT_MARGIN, y, static_cast<Gdiplus::REAL>(rect.right - GRID_RIGHT_MARGIN), y);
    }

    // Draw step labels (every 4 steps)
    for (int i = 0; i <= POINT_COUNT - 1; i += 4) {
        Gdiplus::REAL x = MapIndexToX(i, rect);
        wchar_t label[8];
        swprintf_s(label, L"%d", i);
        Gdiplus::RectF labelRect;
        graphics.MeasureString(label, -1, &axisFont, Gdiplus::PointF(0, 0), &labelRect);
        Gdiplus::PointF labelPos(x - labelRect.Width / 2, static_cast<Gdiplus::REAL>(GRID_BOTTOM + 5));
        graphics.DrawString(label, -1, &axisFont, labelPos, &textBrush);
    }
    for (int v = 0; v <= MAX_VELOCITY; v += 16) {
        Gdiplus::REAL y = MapVelocityToY(v, rect);
        wchar_t label[8];
        swprintf_s(label, L"%d", v);
        Gdiplus::PointF labelPos(GRID_LEFT_MARGIN - 30, y - 6);
        graphics.DrawString(label, -1, &axisFont, labelPos, &textBrush);
    }
    {
        Gdiplus::REAL finalX = MapIndexToX(POINT_COUNT - 1, rect);
        wchar_t finalStepLabel[8];
        swprintf_s(finalStepLabel, L"32");
        Gdiplus::RectF finalStepRect;
        graphics.MeasureString(finalStepLabel, -1, &axisFont, Gdiplus::PointF(0, 0), &finalStepRect);
        graphics.DrawString(finalStepLabel, -1, &axisFont,
            Gdiplus::PointF(finalX - finalStepRect.Width / 2, static_cast<Gdiplus::REAL>(GRID_BOTTOM + 5)),
            &textBrush);

        Gdiplus::REAL finalY = MapVelocityToY(MAX_VELOCITY, rect);
        wchar_t finalVelLabel[8];
        swprintf_s(finalVelLabel, L"127");
        graphics.DrawString(finalVelLabel, -1, &axisFont,
            Gdiplus::PointF(GRID_LEFT_MARGIN - 30, finalY - 6),
            &textBrush);
    }

    graphics.DrawRectangle(&borderPen, gridRectF);

    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);

    graphics.TranslateTransform(30, GRID_TOP + gridHeight / 2);
    graphics.RotateTransform(-90);
    graphics.DrawString(L"Velocity", -1, &labelFont, Gdiplus::PointF(0, -20), &centerFormat, &textBrush);
    graphics.ResetTransform();

    // X-axis label
    graphics.DrawString(L"Step", -1, &labelFont,
        Gdiplus::RectF(GRID_LEFT_MARGIN, static_cast<Gdiplus::REAL>(GRID_BOTTOM + 25), gridWidth, 20),
        &centerFormat, &textBrush);
}

void VelocityCurveEditor::DrawCurve(Gdiplus::Graphics& graphics, const RECT& rect) {
    const Gdiplus::Color curveColor(255, 30, 170, 220);
    const Gdiplus::Color fillColor(40, 30, 170, 220);
    std::vector<Gdiplus::PointF> curvePoints;
    curvePoints.reserve(POINT_COUNT);
    for (int i = 0; i < POINT_COUNT; i++) {
        curvePoints.emplace_back(MapIndexToX(i, rect), MapVelocityToY(points[i], rect));
    }
    Gdiplus::GraphicsPath fillPath;
    std::vector<Gdiplus::PointF> fillPoints = curvePoints;
    fillPoints.push_back(Gdiplus::PointF(MapIndexToX(POINT_COUNT - 1, rect), static_cast<Gdiplus::REAL>(GRID_BOTTOM)));
    fillPoints.push_back(Gdiplus::PointF(MapIndexToX(0, rect), static_cast<Gdiplus::REAL>(GRID_BOTTOM)));
    fillPath.AddLines(fillPoints.data(), static_cast<INT>(fillPoints.size()));
    fillPath.CloseFigure();
    Gdiplus::SolidBrush fillBrush(fillColor);
    graphics.FillPath(&fillBrush, &fillPath);

    Gdiplus::Pen curvePen(curveColor, CURVE_LINE_WIDTH);
    curvePen.SetLineJoin(Gdiplus::LineJoinRound);
    curvePen.SetStartCap(Gdiplus::LineCapRound);
    curvePen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::Pen shadowPen(Gdiplus::Color(40, 0, 0, 0), CURVE_LINE_WIDTH + 1);
    graphics.DrawLines(&shadowPen, curvePoints.data(), static_cast<INT>(curvePoints.size()));

    graphics.DrawLines(&curvePen, curvePoints.data(), static_cast<INT>(curvePoints.size()));
    for (int i = 0; i < POINT_COUNT; i++) {
        const float x = curvePoints[i].X;
        const float y = curvePoints[i].Y;

        Gdiplus::Color pointColor;
        float pointSize;
        float glowSize;
        if (i == selectedPoint) {
            pointColor = Gdiplus::Color(255, 255, 140, 0);
            pointSize = 6.0f;
            glowSize = 12.0f;
        }
        else if (i == hoveredPoint) {
            pointColor = Gdiplus::Color(255, 255, 140, 0);
            pointSize = 5.0f;
            glowSize = 10.0f;
        }
        else {
            pointColor = curveColor;
            pointSize = 4.0f;
            glowSize = 8.0f;
        }
        {
            Gdiplus::GraphicsPath glowPath;
            glowPath.AddEllipse(x - glowSize / 2, y - glowSize / 2, glowSize, glowSize);
            Gdiplus::PathGradientBrush glowBrush(&glowPath);
            const Gdiplus::Color glowCenter(120, pointColor.GetR(), pointColor.GetG(), pointColor.GetB());
            const Gdiplus::Color glowSurround(0, pointColor.GetR(), pointColor.GetG(), pointColor.GetB());
            INT colorCount = 1;
            glowBrush.SetCenterColor(glowCenter);
            glowBrush.SetSurroundColors(&glowSurround, &colorCount);
            graphics.FillPath(&glowBrush, &glowPath);
        }
        {
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(60, 0, 0, 0));
            graphics.FillEllipse(&shadowBrush, x - pointSize + 1, y - pointSize + 1, pointSize * 2, pointSize * 2);
        }
        {
            Gdiplus::SolidBrush pointBrush(pointColor);
            graphics.FillEllipse(&pointBrush, x - pointSize, y - pointSize, pointSize * 2, pointSize * 2);
        }
        {
            Gdiplus::SolidBrush highlightBrush(Gdiplus::Color(140, 255, 255, 255));
            graphics.FillEllipse(&highlightBrush, x - pointSize / 3, y - pointSize / 3, pointSize / 2, pointSize / 2);
        }
    }
}

const wchar_t* VelocityCurveEditor::GetDynamicText(int velocity) {
    if (velocity < 16) return L"ppp";
    if (velocity < 32) return L"pp";
    if (velocity < 48) return L"p";
    if (velocity < 64) return L"mp";
    if (velocity < 80) return L"mf";
    if (velocity < 96) return L"f";
    if (velocity < 112) return L"ff";
    return L"fff";
}

const wchar_t* VelocityCurveEditor::GetPlayingFeelText(float ratio) {
    if (ratio < 0.5f) return L"Very Soft Touch";
    if (ratio < 0.8f) return L"Light Touch";
    if (ratio < 1.2f) return L"Natural";
    if (ratio < 1.5f) return L"Heavy Touch";
    return L"Very Heavy Touch";
}

char VelocityCurveEditor::GetKeyForPoint(int point) {
    static const char keys[] = "1234567890QWERTYUIOPASDFGHJKLZXCV";
    return keys[point % (int)strlen(keys)];
}

void VelocityCurveEditor::DrawInfoPanel(HDC hdc) {
    HWND hInfoWnd = hwndVelocityInfo;
    const LONG_PTR baseStyle = GetWindowLongPtrW(hInfoWnd, GWL_STYLE);

    if (hoveredPoint >= 0 && hoveredPoint < POINT_COUNT) {
        const int inputVel = (hoveredPoint * MAX_VELOCITY) / (POINT_COUNT - 1);
        const int outputVel = points[hoveredPoint];
        const float velocityRatio = outputVel / static_cast<float>(inputVel ? inputVel : 1);
        wchar_t info[512];
        swprintf_s(info, _countof(info),
            L"⦿ Step %d  │  In: %d → Out: %d  │  Ratio: %.2fx  │  %s (%s)  │  Key: [%c]", // replace someday... never
            hoveredPoint, inputVel, outputVel, velocityRatio,
            GetDynamicText(outputVel), GetPlayingFeelText(velocityRatio),
            GetKeyForPoint(hoveredPoint));

        HFONT hFont = CreateCustomFont(-20, FW_SEMIBOLD);
        SetWindowLongPtrW(hInfoWnd, GWL_STYLE, (baseStyle & ~(SS_LEFT | SS_RIGHT)) | SS_CENTER);
        SendMessageW(hInfoWnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SetWindowTextW(hInfoWnd, info);
        DeleteObject(hFont);
    }
    else {
        HFONT hDefaultFont = CreateCustomFont(-18, FW_SEMIBOLD);
        SetWindowLongPtrW(hInfoWnd, GWL_STYLE, (baseStyle & ~(SS_LEFT | SS_RIGHT)) | SS_CENTER);
        SendMessageW(hInfoWnd, WM_SETFONT, reinterpret_cast<WPARAM>(hDefaultFont), TRUE);
        SetWindowTextW(hInfoWnd, L"● Hover over velocity curve points to inspect values ●");
        DeleteObject(hDefaultFont);
    }
    InvalidateRect(hInfoWnd, nullptr, TRUE);
    UpdateWindow(hInfoWnd);
}

float VelocityCurveEditor::MapIndexToX(int index, const RECT& rect) const {
    return GRID_LEFT_MARGIN + (index * (rect.right - GRID_LEFT_MARGIN - GRID_RIGHT_MARGIN) / static_cast<float>(POINT_COUNT - 1));
}

float VelocityCurveEditor::MapVelocityToY(int velocity, const RECT& rect) const {
    return GRID_BOTTOM - ((velocity * (GRID_BOTTOM - GRID_TOP)) / static_cast<float>(MAX_VELOCITY));
}

int VelocityCurveEditor::MapXToIndex(int x, const RECT& rect) const {
    float normalizedX = (x - GRID_LEFT_MARGIN) * (POINT_COUNT - 1) / static_cast<float>(rect.right - GRID_LEFT_MARGIN - GRID_RIGHT_MARGIN);
    return static_cast<int>(std::clamp(normalizedX, 0.0f, static_cast<float>(POINT_COUNT - 1)));
}

int VelocityCurveEditor::MapYToVelocity(int y) const {
    float normalizedY = (GRID_BOTTOM - y) * MAX_VELOCITY / static_cast<float>(GRID_BOTTOM - GRID_TOP);
    return static_cast<int>(std::clamp(normalizedY, 0.0f, static_cast<float>(MAX_VELOCITY)));
}

void VelocityCurveEditor::OnMouseDown(int x, int y) {
    if (y < GRID_TOP || y > GRID_BOTTOM)
        return;

    RECT rect;
    ::GetClientRect(hwndEditor, &rect);

    selectedPoint = -1;
    float closestDist = FLT_MAX;
    int closestPoint = -1;

    for (int i = 0; i < POINT_COUNT; i++) {
        float px = MapIndexToX(i, rect);
        float py = MapVelocityToY(points[i], rect);
        float dist = std::hypot(x - px, y - py);

        if (dist <= HIT_RADIUS * 1.5f && dist < closestDist) {
            closestDist = dist;
            closestPoint = i;
        }
    }
    if (closestPoint != -1) {
        selectedPoint = closestPoint;
        isDragging = true;
        SetCapture(hwndEditor);
        InvalidateCurveArea();
    }
}

void VelocityCurveEditor::OnMouseMove(int x, int y) {
    if (isDragging && selectedPoint >= 0) {
        RECT rect = GetClientRectRect();
        int newVelocity = MapYToVelocity(y);

        // Snap to preset values if within threshold
        constexpr int snapThreshold = 3;
        const int snapValues[] = { 0, 32, 64, 96, 127 };
        for (int sv : snapValues) {
            if (std::abs(newVelocity - sv) <= snapThreshold) {
                newVelocity = sv;
                break;
            }
        }

        if (points[selectedPoint] != newVelocity) {
            points[selectedPoint] = std::clamp(newVelocity, 0, 127);
            if (isAltKeyDown) {
                SmoothNearbyPoints(selectedPoint);
            }
            InvalidateCurveArea();
        }
        return;
    }

    if (y >= GRID_TOP - HIT_RADIUS && y <= GRID_BOTTOM + HIT_RADIUS) {
        RECT rect = GetClientRectRect();
        int oldHovered = hoveredPoint;
        float minDist = HIT_RADIUS * 1.5f;
        hoveredPoint = -1;

        int baseIndex = MapXToIndex(x, rect);
        for (int i = std::max(0, baseIndex - 1); i <= std::min(POINT_COUNT - 1, baseIndex + 1); i++) {
            float px = MapIndexToX(i, rect);
            float py = MapVelocityToY(points[i], rect);
            float dist = std::hypot(static_cast<float>(x) - px, static_cast<float>(y) - py);
            if (dist < minDist) {
                minDist = dist;
                hoveredPoint = i;
            }
        }
        if (oldHovered != hoveredPoint) {
            InvalidateCurveArea();
        }
    }
    else if (hoveredPoint != -1) {
        hoveredPoint = -1;
        InvalidateCurveArea();
    }
    lastMouseX = x;
    lastMouseY = y;
}

RECT VelocityCurveEditor::GetClientRectRect() const {
    RECT r;
    ::GetClientRect(hwndEditor, &r);
    return r;
}

void VelocityCurveEditor::OnMouseUp() {
    if (isDragging) {
        isDragging = false;
        ReleaseCapture();
        selectedPoint = -1;
        InvalidateCurveArea();
    }
}

void VelocityCurveEditor::SmoothNearbyPoints(int center) {
    constexpr int radius = 2;
    for (int i = std::max(0, center - radius); i <= std::min(POINT_COUNT - 1, center + radius); i++) {
        if (i == center)
            continue;
        float distance = static_cast<float>(std::abs(i - center));
        float weight = 1.0f - (distance / (radius + 1));
        points[i] = static_cast<int>(points[i] + weight * (points[center] - points[i]));
    }
}

void VelocityCurveEditor::SmoothCurve() {
    std::vector<int> smoothed = points;
    for (int i = 1; i < POINT_COUNT - 1; i++) {
        smoothed[i] = (points[i - 1] + 2 * points[i] + points[i + 1]) / 4;
    }
    points = smoothed;
}

void VelocityCurveEditor::LoadPreset(const std::string& preset) {
    currentPreset = preset;
    if (preset == "Linear") {
        ResetToLinear();
    }
    else if (preset == "Logarithmic") {
        for (int i = 0; i < POINT_COUNT; i++) {
            double x = static_cast<double>(i) / (POINT_COUNT - 1);
            points[i] = static_cast<int>(127.0 * (std::log(x * 9 + 1) / std::log(10)));
        }
    }
    else if (preset == "Exponential") {
        for (int i = 0; i < POINT_COUNT; i++) {
            double x = static_cast<double>(i) / (POINT_COUNT - 1);
            points[i] = static_cast<int>(127.0 * std::pow(x, 2));
        }
    }
    else if (preset == "S-Curve") {
        for (int i = 0; i < POINT_COUNT; i++) {
            double x = static_cast<double>(i) / (POINT_COUNT - 1);
            points[i] = static_cast<int>(127.0 * (0.5 + 0.5 * std::tanh((x - 0.5) * 5)));
        }
    }
    InvalidateCurveArea();
}

void VelocityCurveEditor::SaveCurve() {
    wchar_t curveNameW[256];
    GetWindowTextW(hwndCurveName, curveNameW, _countof(curveNameW));
    std::wstring wstr(curveNameW);
    std::string name(wstr.begin(), wstr.end());

    if (name.empty()) {
        MessageBoxW(hwndEditor, L"Please enter a name for the curve.", L"Error", MB_OK | MB_ICONWARNING);
        return;
    }

    midi::CustomVelocityCurve curve;
    curve.name = name;
    std::copy(points.begin(), points.end(), curve.velocityValues.begin());

    auto& config = midi::Config::getInstance();
    auto& curves = config.playback.customVelocityCurves;
    auto it = std::find_if(curves.begin(), curves.end(), [&name](const auto& c) { return c.name == name; });

    if (it != curves.end()) {
        int ret = MessageBoxW(hwndEditor, L"A curve with this name already exists. Overwrite?",
            L"Confirm Overwrite", MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES)
            *it = curve;
        else
            return;
    }
    else {
        curves.push_back(curve);
    }

    try {
        config.saveToFile("config.json");
        MessageBoxW(hwndEditor, L"Curve saved successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
    }
    catch (const std::exception& e) {
        MessageBoxA(hwndEditor, e.what(), "Error saving curve", MB_OK | MB_ICONERROR);
    }
}

void VelocityCurveEditor::InvalidateCurveArea() {
    RECT rect;
    ::GetClientRect(hwndEditor, &rect);
    RECT curveRect = { 0, GRID_TOP, rect.right, GRID_BOTTOM };
    InvalidateRect(hwndEditor, &curveRect, FALSE);
}

RECT VelocityCurveEditor::GetClientRect() const {
    RECT rect;
    ::GetClientRect(hwndEditor, &rect);
    return rect;
}

VelocityCurveEditor::~VelocityCurveEditor() {
    if (hInfoFont) { DeleteObject(hInfoFont); hInfoFont = nullptr; }
    if (hLargeFont) { DeleteObject(hLargeFont); hLargeFont = nullptr; }
    if (hwndEditor == activeWindow) { activeWindow = nullptr; }
}

bool VelocityCurveEditor::Create(HWND parent) {
    if (activeWindow && IsWindow(activeWindow)) {
        SetForegroundWindow(activeWindow);
        return false;
    }
    if (activeWindow && !IsWindow(activeWindow))
        activeWindow = nullptr;

    hwndParent = parent;
    if (!isClassRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = EditorWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        if (!RegisterClassExW(&wc)) {
            UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandle(nullptr));
            if (!RegisterClassExW(&wc))
                return false;
        }
        isClassRegistered = true;
    }
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int xPos = parentRect.left + (parentRect.right - parentRect.left - EDITOR_WIDTH) / 2;
    int yPos = parentRect.top + (parentRect.bottom - parentRect.top - EDITOR_HEIGHT) / 2;

    hwndEditor = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        WINDOW_CLASS_NAME,
        L"Velocity Curve Editor",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        xPos, yPos, EDITOR_WIDTH, EDITOR_HEIGHT,
        parent, nullptr, GetModuleHandle(nullptr), nullptr
    );
    if (!hwndEditor)
        return false;

    hInfoFont = CreateCustomFont(-12, FW_NORMAL);
    hLargeFont = CreateCustomFont(-14, FW_BOLD);

    CreateControls();

    SetWindowLongPtr(hwndEditor, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    activeWindow = hwndEditor;

    ShowWindow(hwndEditor, SW_SHOW);
    UpdateWindow(hwndEditor);
    return true;
}

void VelocityCurveEditor::CreateControls() {
    hInfoFont = CreateCustomFont(-INFO_FONT_SIZE, FW_NORMAL);
    hLargeFont = CreateCustomFont(-LARGE_FONT_SIZE, FW_BOLD);

    int currentX = MARGIN;
    hwndCurveName = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"Custom Curve",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        currentX + 50, TOP_MARGIN, NAME_FIELD_WIDTH, TOP_CONTROL_HEIGHT,
        hwndEditor, reinterpret_cast<HMENU>(ID_VCURVE_NAME), GetModuleHandle(nullptr), nullptr
    );
    SendMessage(hwndCurveName, WM_SETFONT, reinterpret_cast<WPARAM>(hLargeFont), TRUE);

    currentX += NAME_FIELD_WIDTH + BUTTON_GAP;
    hwndVelocityInfo = CreateWindowW(
        L"STATIC", L"Hover over points to see values",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        currentX + 42, TOP_MARGIN, INFO_FIELD_WIDTH, TOP_CONTROL_HEIGHT,
        hwndEditor, nullptr, GetModuleHandle(nullptr), nullptr
    );
    SendMessage(hwndVelocityInfo, WM_SETFONT, reinterpret_cast<WPARAM>(hInfoFont), TRUE);

    hwndGridlines = CreateWindowW(
        L"BUTTON", L"Grid",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        EDITOR_WIDTH - MARGIN - GRID_TOGGLE_WIDTH - 25, TOP_MARGIN,
        GRID_TOGGLE_WIDTH, TOP_CONTROL_HEIGHT,
        hwndEditor, reinterpret_cast<HMENU>(ID_VCURVE_GRIDLINES), GetModuleHandle(nullptr), nullptr
    );
    SendMessage(hwndGridlines, WM_SETFONT, reinterpret_cast<WPARAM>(hInfoFont), TRUE);
    SendMessage(hwndGridlines, BM_SETCHECK, BST_CHECKED, 0);

    // Create main control buttons, centered horizontally.
    const int totalButtons = 7;
    const int totalButtonWidth = totalButtons * BUTTON_WIDTH + (totalButtons - 1) * BUTTON_GAP;
    const int buttonStartX = (EDITOR_WIDTH - totalButtonWidth) / 2;
    const int buttonY = BUTTONS_TOP;
    CreateButton(L"Save", buttonStartX, buttonY, BUTTON_WIDTH, ID_VCURVE_SAVE);
    CreateButton(L"Reset", buttonStartX + (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_RESET);
    CreateButton(L"Linear", buttonStartX + 2 * (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_LINEAR);
    CreateButton(L"Log", buttonStartX + 3 * (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_LOG);
    CreateButton(L"Exp", buttonStartX + 4 * (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_EXP);
    CreateButton(L"S-Curve", buttonStartX + 5 * (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_SCURVE);
    CreateButton(L"Smooth", buttonStartX + 6 * (BUTTON_WIDTH + BUTTON_GAP), buttonY, BUTTON_WIDTH, ID_VCURVE_SMOOTH);
}

void VelocityCurveEditor::CreateButton(const wchar_t* text, int x, int y, int width, int id) {
    HWND btn = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_NOTIFY,
        x, y, width, BUTTON_HEIGHT,
        hwndEditor,
        reinterpret_cast<HMENU>(id),
        GetModuleHandle(nullptr),
        nullptr
    );
    SendMessage(btn, WM_SETFONT, reinterpret_cast<WPARAM>(hInfoFont), TRUE);
}

void VelocityCurveEditor::OnClose() {
    if (!isBeingDestroyed) {
        isBeingDestroyed = true;
        DestroyWindow(hwndEditor);
    }
}

void VelocityCurveEditor::ResetToLinear() {
    for (int i = 0; i < POINT_COUNT; i++) {
        points[i] = (i * MAX_VELOCITY) / (POINT_COUNT - 1);
    }
}

LRESULT CALLBACK VelocityCurveEditor::EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    VelocityCurveEditor* editor = reinterpret_cast<VelocityCurveEditor*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!editor) return DefWindowProc(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT:
        editor->OnPaint();
        return 0;
    case WM_LBUTTONDOWN:
        editor->OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        editor->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        editor->OnMouseUp();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_MENU) editor->isAltKeyDown = true;
        if (wParam == VK_ESCAPE) editor->OnClose();
        return 0;
    case WM_KEYUP:
        if (wParam == VK_MENU) editor->isAltKeyDown = false;
        return 0;
    case WM_COMMAND:
        return editor->OnCommand(wParam, lParam);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (editor) {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            if (hwnd == activeWindow)
                activeWindow = nullptr;
            delete editor;
        }
        return 0;
    case WM_NCCREATE:
        return TRUE;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VelocityCurveEditor::OnCommand(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) {
    case ID_VCURVE_SAVE:
        if (HIWORD(wParam) == BN_CLICKED) { SaveCurve(); return 0; }
        break;
    case ID_VCURVE_RESET:
        if (HIWORD(wParam) == BN_CLICKED) { ResetToLinear(); InvalidateCurveArea(); return 0; }
        break;
    case ID_VCURVE_LINEAR:
        if (HIWORD(wParam) == BN_CLICKED) { LoadPreset("Linear"); return 0; }
        break;
    case ID_VCURVE_LOG:
        if (HIWORD(wParam) == BN_CLICKED) { LoadPreset("Logarithmic"); return 0; }
        break;
    case ID_VCURVE_EXP:
        if (HIWORD(wParam) == BN_CLICKED) { LoadPreset("Exponential"); return 0; }
        break;
    case ID_VCURVE_SCURVE:
        if (HIWORD(wParam) == BN_CLICKED) { LoadPreset("S-Curve"); return 0; }
        break;
    case ID_VCURVE_SMOOTH:
        if (HIWORD(wParam) == BN_CLICKED) { SmoothCurve(); InvalidateCurveArea(); return 0; }
        break;
    case ID_VCURVE_GRIDLINES:
        if (HIWORD(wParam) == BN_CLICKED) {
            showGridlines = (SendMessage(hwndGridlines, BM_GETCHECK, 0, 0) == BST_CHECKED);
            InvalidateCurveArea();
            return 0;
        }
        break;
    }
    return DefWindowProc(hwndEditor, WM_COMMAND, wParam, lParam);
}

void VelocityCurveEditor::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwndEditor, &ps);
    HDC memDC = CreateCompatibleDC(hdc);
    RECT clientRect;
    ::GetClientRect(hwndEditor, &clientRect);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));
    FillRect(memDC, &clientRect, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    Gdiplus::Graphics graphics(memDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    if (showGridlines)
        DrawGrid(graphics, clientRect);
    DrawCurve(graphics, clientRect);
    DrawInfoPanel(memDC);
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwndEditor, &ps);
}
