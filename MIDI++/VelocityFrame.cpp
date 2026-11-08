#include "config.hpp"
#include "VelocityCurveEditor.hpp"
#include "Output.hpp"
#include "Widgets.hpp"
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <vector>
#include <memory>

// For GDI+ drawing:
#include <GdiPlus.h>
#include <windowsx.h>
#pragma comment(lib, "Gdiplus.lib")

namespace {

    constexpr char kVelocityKeys[] = "1234567890qwertyuiopasdfghjklzxc";

    Gdiplus::Color tone(COLORREF c, int alpha = 255) {
        return Gdiplus::Color(static_cast<BYTE>(std::clamp(alpha, 0, 255)), GetRValue(c), GetGValue(c), GetBValue(c));
    }

    int window_dpi(HWND hwnd) {
        using Fn = UINT(WINAPI*)(HWND);
        static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        UINT d = fn && hwnd ? fn(hwnd) : 0;
        return d ? static_cast<int>(d) : 96;
    }

    void adjust_rect(RECT& r, DWORD style, DWORD ex, int dpi) {
        using Fn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
        if (!fn || !fn(&r, style, FALSE, ex, static_cast<UINT>(dpi)))
            AdjustWindowRectEx(&r, style, FALSE, ex);
    }

    std::wstring widen(const std::string& s) {
        if (s.empty())
            return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

}

//------------------------------------------------------------------------------
// VelocityCurveEditor implementation

VelocityCurveEditor::VelocityCurveEditor() :
    hwndEditor(nullptr),
    hwndParent(nullptr),
    hwndCurveName(nullptr),
    hwndGridlines(nullptr),
    points(POINT_COUNT),
    isDragging(false),
    selectedPoint(-1),
    hoveredPoint(-1),
    dpi(96),
    showGridlines(true),
    tracking(false),
    currentPreset("Linear"),
    fieldBrush(nullptr)
{
    ResetToLinear();
}

void VelocityCurveEditor::DrawGrid(Gdiplus::Graphics& graphics, const RECT& rect) {
    const ui::Palette& p = ui::pal();
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    const Gdiplus::Color gridColor = tone(p.border);      // major grid lines
    const Gdiplus::Color minorGridColor = tone(p.grid);   // minor grid lines
    const Gdiplus::Color borderColor = tone(p.buttonBorder);
    const Gdiplus::Color textColor = tone(p.subtext);
    Gdiplus::Pen gridPen(gridColor, 1);
    Gdiplus::Pen minorGridPen(minorGridColor, 1);
    Gdiplus::Pen borderPen(borderColor, 1);
    Gdiplus::SolidBrush textBrush(textColor);
    Gdiplus::SolidBrush labelTextBrush(tone(p.text));
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font axisFont(&fontFamily, static_cast<Gdiplus::REAL>(S(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font labelFont(&fontFamily, static_cast<Gdiplus::REAL>(S(12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    const Gdiplus::REAL gridLeft = static_cast<Gdiplus::REAL>(S(GRID_LEFT_MARGIN));
    const Gdiplus::REAL gridRight = static_cast<Gdiplus::REAL>(rect.right - S(GRID_RIGHT_MARGIN));
    const Gdiplus::REAL gridTop = static_cast<Gdiplus::REAL>(S(GRID_TOP));
    const Gdiplus::REAL gridBottom = static_cast<Gdiplus::REAL>(S(GRID_BOTTOM));
    const Gdiplus::REAL gridWidth = gridRight - gridLeft;
    const Gdiplus::REAL gridHeight = gridBottom - gridTop;

    Gdiplus::SolidBrush gridBgBrush(tone(p.field));
    Gdiplus::RectF gridRectF(gridLeft, gridTop, gridWidth, gridHeight);
    graphics.FillRectangle(&gridBgBrush, gridRectF);

    if (showGridlines) {
        for (int i = 0; i < POINT_COUNT; i++) {
            Gdiplus::REAL x = MapIndexToX(i, rect);
            graphics.DrawLine(&minorGridPen, x, gridTop, x, gridBottom);
        }
        // Draw minor horizontal grid lines (every 8 velocity units)
        for (int v = 0; v <= MAX_VELOCITY; v += 8) {
            Gdiplus::REAL y = MapVelocityToY(v, rect);
            graphics.DrawLine(&minorGridPen, gridLeft, y, gridRight, y);
        }
        // Draw major vertical grid lines (every 4 points)
        for (int i = 0; i < POINT_COUNT; i += 4) {
            Gdiplus::REAL x = MapIndexToX(i, rect);
            graphics.DrawLine(&gridPen, x, gridTop, x, gridBottom);
        }
        // Draw major horizontal grid lines (every 16 velocity units)
        for (int v = 0; v <= MAX_VELOCITY; v += 16) {
            Gdiplus::REAL y = MapVelocityToY(v, rect);
            graphics.DrawLine(&gridPen, gridLeft, y, gridRight, y);
        }
    }

    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::StringFormat rightFormat;
    rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    rightFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // Draw step labels (every 4 steps)
    for (int i = 0; i < POINT_COUNT; i += 4) {
        Gdiplus::REAL x = MapIndexToX(i, rect);
        wchar_t label[8];
        swprintf_s(label, L"%d", i + 1);
        graphics.DrawString(label, -1, &axisFont, Gdiplus::RectF(x - S(20), gridBottom + S(5), static_cast<Gdiplus::REAL>(S(40)), static_cast<Gdiplus::REAL>(S(16))),
            &centerFormat, &textBrush);
    }
    {
        Gdiplus::REAL finalX = MapIndexToX(POINT_COUNT - 1, rect);
        graphics.DrawString(L"32", -1, &axisFont, Gdiplus::RectF(finalX - S(20), gridBottom + S(5), static_cast<Gdiplus::REAL>(S(40)), static_cast<Gdiplus::REAL>(S(16))),
            &centerFormat, &textBrush);
    }
    for (int v = 0; v <= MAX_VELOCITY; v += 16) {
        Gdiplus::REAL y = MapVelocityToY(v, rect);
        wchar_t label[8];
        swprintf_s(label, L"%d", v);
        graphics.DrawString(label, -1, &axisFont, Gdiplus::RectF(gridLeft - S(40), y - S(8), static_cast<Gdiplus::REAL>(S(34)), static_cast<Gdiplus::REAL>(S(16))),
            &rightFormat, &textBrush);
    }
    {
        Gdiplus::REAL finalY = MapVelocityToY(MAX_VELOCITY, rect);
        graphics.DrawString(L"127", -1, &axisFont, Gdiplus::RectF(gridLeft - S(40), finalY - S(8), static_cast<Gdiplus::REAL>(S(34)), static_cast<Gdiplus::REAL>(S(16))),
            &rightFormat, &textBrush);
    }

    graphics.DrawRectangle(&borderPen, gridRectF);

    graphics.TranslateTransform(static_cast<Gdiplus::REAL>(S(16)), gridTop + gridHeight / 2);
    graphics.RotateTransform(-90);
    graphics.DrawString(L"Velocity", -1, &labelFont, Gdiplus::PointF(0, static_cast<Gdiplus::REAL>(-S(8))), &centerFormat, &labelTextBrush);
    graphics.ResetTransform();

    // X-axis label
    graphics.DrawString(L"Step", -1, &labelFont,
        Gdiplus::RectF(gridLeft, gridBottom + S(21), gridWidth, static_cast<Gdiplus::REAL>(S(18))),
        &centerFormat, &labelTextBrush);
}

void VelocityCurveEditor::DrawCurve(Gdiplus::Graphics& graphics, const RECT& rect) {
    const Gdiplus::Color curveColor = tone(ui::pal().accent);
    const Gdiplus::Color fillColor = tone(ui::pal().accent, 40);
    const float k = dpi / 96.0f;
    std::vector<Gdiplus::PointF> curvePoints;
    curvePoints.reserve(POINT_COUNT);
    for (int i = 0; i < POINT_COUNT; i++) {
        curvePoints.emplace_back(MapIndexToX(i, rect), MapVelocityToY(points[i], rect));
    }
    Gdiplus::GraphicsPath fillPath;
    std::vector<Gdiplus::PointF> fillPoints = curvePoints;
    fillPoints.push_back(Gdiplus::PointF(MapIndexToX(POINT_COUNT - 1, rect), static_cast<Gdiplus::REAL>(S(GRID_BOTTOM))));
    fillPoints.push_back(Gdiplus::PointF(MapIndexToX(0, rect), static_cast<Gdiplus::REAL>(S(GRID_BOTTOM))));
    fillPath.AddLines(fillPoints.data(), static_cast<INT>(fillPoints.size()));
    fillPath.CloseFigure();
    Gdiplus::SolidBrush fillBrush(fillColor);
    graphics.FillPath(&fillBrush, &fillPath);

    Gdiplus::Pen curvePen(curveColor, CURVE_LINE_WIDTH * k);
    curvePen.SetLineJoin(Gdiplus::LineJoinRound);
    curvePen.SetStartCap(Gdiplus::LineCapRound);
    curvePen.SetEndCap(Gdiplus::LineCapRound);

    Gdiplus::Pen shadowPen(Gdiplus::Color(40, 0, 0, 0), (CURVE_LINE_WIDTH + 1) * k);
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
            pointSize = 6.0f * k;
            glowSize = 12.0f * k;
        }
        else if (i == hoveredPoint) {
            pointColor = Gdiplus::Color(255, 255, 140, 0);
            pointSize = 5.0f * k;
            glowSize = 10.0f * k;
        }
        else {
            pointColor = curveColor;
            pointSize = 4.0f * k;
            glowSize = 8.0f * k;
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
            graphics.FillEllipse(&shadowBrush, x - pointSize + k, y - pointSize + k, pointSize * 2, pointSize * 2);
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
    return kVelocityKeys[std::clamp(point, 0, POINT_COUNT - 1)];
}

void VelocityCurveEditor::DrawInfoPanel(HDC hdc) {
    const ui::Palette& p = ui::pal();
    RECT rc = GetClientRectRect();
    SetBkMode(hdc, TRANSPARENT);

    RECT label = { S(MARGIN), S(TOP_MARGIN), S(MARGIN + NAME_LABEL_WIDTH), S(TOP_MARGIN + TOP_CONTROL_HEIGHT) };
    SetTextColor(hdc, p.text);
    HGDIOBJ old = SelectObject(hdc, ui::font(dpi, INFO_FONT_SIZE, FW_NORMAL));
    DrawTextW(hdc, L"Curve name", -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    wchar_t info[512];
    const bool hovered = hoveredPoint >= 0 && hoveredPoint < POINT_COUNT;
    if (hovered) {
        const int inputVel = (hoveredPoint * MAX_VELOCITY) / (POINT_COUNT - 1);
        const int outputVel = points[hoveredPoint];
        const float velocityRatio = outputVel / static_cast<float>(inputVel ? inputVel : 1);
        swprintf_s(info, _countof(info),
            L"Step %d  |  In: %d -> Out: %d  |  Ratio: %.2fx  |  %s (%s)  |  Key: Alt+%c", // replace someday... never
            hoveredPoint + 1, inputVel, outputVel, velocityRatio,
            GetDynamicText(outputVel), GetPlayingFeelText(velocityRatio),
            GetKeyForPoint(hoveredPoint));
    }
    else {
        swprintf_s(info, _countof(info), L"%s", statusText.empty()
            ? L"Drag the points or draw across the grid. Hold Alt while dragging to bend the neighbors too."
            : statusText.c_str());
    }
    RECT strip = { S(MARGIN), S(INFO_TOP), rc.right - S(MARGIN), S(INFO_TOP) + S(22) };
    SetTextColor(hdc, hovered ? p.text : p.subtext);
    SelectObject(hdc, ui::font(dpi, INFO_FONT_SIZE, hovered ? FW_SEMIBOLD : FW_NORMAL));
    DrawTextW(hdc, info, -1, &strip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, old);
}

float VelocityCurveEditor::MapIndexToX(int index, const RECT& rect) const {
    const float left = static_cast<float>(S(GRID_LEFT_MARGIN));
    const float right = static_cast<float>(rect.right - S(GRID_RIGHT_MARGIN));
    return left + index * (right - left) / static_cast<float>(POINT_COUNT - 1);
}

float VelocityCurveEditor::MapVelocityToY(int velocity, const RECT&) const {
    const float top = static_cast<float>(S(GRID_TOP));
    const float bottom = static_cast<float>(S(GRID_BOTTOM));
    return bottom - velocity * (bottom - top) / static_cast<float>(MAX_VELOCITY);
}

int VelocityCurveEditor::MapXToIndex(int x, const RECT& rect) const {
    const float left = static_cast<float>(S(GRID_LEFT_MARGIN));
    const float right = static_cast<float>(rect.right - S(GRID_RIGHT_MARGIN));
    float normalizedX = (x - left) * (POINT_COUNT - 1) / (right - left);
    return static_cast<int>(std::lround(std::clamp(normalizedX, 0.0f, static_cast<float>(POINT_COUNT - 1))));
}

int VelocityCurveEditor::MapYToVelocity(int y) const {
    const float top = static_cast<float>(S(GRID_TOP));
    const float bottom = static_cast<float>(S(GRID_BOTTOM));
    float normalizedY = (bottom - y) * MAX_VELOCITY / (bottom - top);
    return static_cast<int>(std::lround(std::clamp(normalizedY, 0.0f, static_cast<float>(MAX_VELOCITY))));
}

bool VelocityCurveEditor::InGrid(int x, int y) const {
    RECT rect = GetClientRectRect();
    const int pad = S(HIT_RADIUS);
    return x >= S(GRID_LEFT_MARGIN) - pad && x <= rect.right - S(GRID_RIGHT_MARGIN) + pad
        && y >= S(GRID_TOP) - pad && y <= S(GRID_BOTTOM) + pad;
}

void VelocityCurveEditor::SetPoint(int index, int velocity) {
    // Snap to preset values if within threshold
    constexpr int snapThreshold = 3;
    const int snapValues[] = { 0, 32, 64, 96, 127 };
    for (int sv : snapValues) {
        if (std::abs(velocity - sv) <= snapThreshold) {
            velocity = sv;
            break;
        }
    }
    points[index] = std::clamp(velocity, 0, MAX_VELOCITY);
}

void VelocityCurveEditor::OnMouseDown(int x, int y) {
    SetFocus(hwndEditor);
    if (!InGrid(x, y))
        return;

    RECT rect = GetClientRectRect();
    float closestDist = FLT_MAX;
    int closestPoint = -1;

    for (int i = 0; i < POINT_COUNT; i++) {
        float px = MapIndexToX(i, rect);
        float py = MapVelocityToY(points[i], rect);
        float dist = std::hypot(x - px, y - py);

        if (dist <= S(HIT_RADIUS) * 1.5f && dist < closestDist) {
            closestDist = dist;
            closestPoint = i;
        }
    }
    if (closestPoint == -1) {
        closestPoint = MapXToIndex(x, rect);
        SetPoint(closestPoint, MapYToVelocity(y));
    }
    selectedPoint = closestPoint;
    isDragging = true;
    statusText.clear();
    SetCapture(hwndEditor);
    InvalidateCurveArea();
}

void VelocityCurveEditor::OnMouseMove(int x, int y) {
    if (!tracking) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwndEditor, 0 };
        tracking = TrackMouseEvent(&tme) != FALSE;
    }
    RECT rect = GetClientRectRect();
    if (isDragging && selectedPoint >= 0) {
        const int newVelocity = MapYToVelocity(y);
        const int column = MapXToIndex(x, rect);
        if (column != selectedPoint) {
            const int from = points[selectedPoint];
            const int span = column - selectedPoint;
            for (int i = selectedPoint; i != column; i += span > 0 ? 1 : -1)
                SetPoint(i, from + (newVelocity - from) * (i - selectedPoint) / span);
            selectedPoint = column;
        }
        SetPoint(selectedPoint, newVelocity);
        if (GetKeyState(VK_MENU) < 0) {
            SmoothNearbyPoints(selectedPoint);
        }
        hoveredPoint = selectedPoint;
        InvalidateCurveArea();
        return;
    }

    int oldHovered = hoveredPoint;
    hoveredPoint = -1;
    if (InGrid(x, y)) {
        float minDist = S(HIT_RADIUS) * 1.5f;
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
    }
    if (oldHovered != hoveredPoint) {
        InvalidateCurveArea();
    }
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
    statusText.clear();
    InvalidateCurveArea();
}

void VelocityCurveEditor::SaveCurve() {
    wchar_t curveNameW[256];
    GetWindowTextW(hwndCurveName, curveNameW, _countof(curveNameW));
    char utf8[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, curveNameW, -1, utf8, sizeof(utf8), nullptr, nullptr);
    std::string name(utf8);
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

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

    int index = 0;
    if (it != curves.end()) {
        int ret = MessageBoxW(hwndEditor, L"A curve with this name already exists. Overwrite?",
            L"Confirm Overwrite", MB_YESNO | MB_ICONQUESTION);
        if (ret != IDYES)
            return;
        *it = curve;
        index = static_cast<int>(it - curves.begin());
    }
    else {
        curves.push_back(curve);
        index = static_cast<int>(curves.size()) - 1;
    }

    currentPreset = name;
    statusText = L"Saved \"" + widen(name) + L"\" and switched playback to it.";
    if (onSaved)
        onSaved(5 + index);
    InvalidateCurveArea();
}

void VelocityCurveEditor::InvalidateCurveArea() {
    InvalidateRect(hwndEditor, nullptr, FALSE);
}

VelocityCurveEditor::~VelocityCurveEditor() {
    if (fieldBrush) { DeleteObject(fieldBrush); fieldBrush = nullptr; }
    if (hwndEditor == activeWindow) { activeWindow = nullptr; }
}

bool VelocityCurveEditor::Create(HWND parent, int curveIndex, std::function<void(int)> saved) {
    if (activeWindow && IsWindow(activeWindow)) {
        SetForegroundWindow(activeWindow);
        return false;
    }
    activeWindow = nullptr;
    hwndParent = parent;
    onSaved = std::move(saved);

    std::wstring name = L"Custom Curve";
    const auto& custom = midi::Config::getInstance().playback.customVelocityCurves;
    if (curveIndex >= 5 && curveIndex - 5 < static_cast<int>(custom.size())) {
        const auto& c = custom[static_cast<size_t>(curveIndex - 5)];
        std::copy(c.velocityValues.begin(), c.velocityValues.end(), points.begin());
        currentPreset = c.name;
        name = widen(c.name);
    }
    else if (curveIndex >= 0 && curveIndex < 5) {
        const auto table = out::builtin_curve(curveIndex);
        std::copy(table.begin(), table.end(), points.begin());
    }

    if (!isClassRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = EditorWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(103));
        if (!RegisterClassExW(&wc))
            return false;
        isClassRegistered = true;
    }

    dpi = window_dpi(parent);
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD ex = WS_EX_TOOLWINDOW;
    RECT r = { 0, 0, S(EDITOR_WIDTH), S(EDITOR_HEIGHT) };
    adjust_rect(r, style, ex, dpi);
    const int width = r.right - r.left, height = r.bottom - r.top;
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int xPos = parentRect.left + (parentRect.right - parentRect.left - width) / 2;
    int yPos = parentRect.top + (parentRect.bottom - parentRect.top - height) / 2;

    hwndEditor = CreateWindowExW(
        ex,
        WINDOW_CLASS_NAME,
        L"Velocity Curve Editor",
        style,
        xPos, yPos, width, height,
        parent, nullptr, GetModuleHandle(nullptr), this
    );
    if (!hwndEditor)
        return false;

    const int actual = window_dpi(hwndEditor);
    if (actual != dpi) {
        dpi = actual;
        RECT a = { 0, 0, S(EDITOR_WIDTH), S(EDITOR_HEIGHT) };
        adjust_rect(a, style, ex, dpi);
        SetWindowPos(hwndEditor, nullptr, 0, 0, a.right - a.left, a.bottom - a.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    CreateControls();
    SetWindowTextW(hwndCurveName, name.c_str());
    Restyle();
    activeWindow = hwndEditor;

    ShowWindow(hwndEditor, SW_SHOW);
    UpdateWindow(hwndEditor);
    return true;
}

void VelocityCurveEditor::CreateControls() {
    hwndCurveName = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwndEditor, reinterpret_cast<HMENU>(ID_VCURVE_NAME), GetModuleHandle(nullptr), nullptr
    );
    SendMessageW(hwndCurveName, EM_SETLIMITTEXT, 60, 0);

    hwndGridlines = CreateWindowW(
        L"BUTTON", L"Grid",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0,
        hwndEditor, reinterpret_cast<HMENU>(ID_VCURVE_GRIDLINES), GetModuleHandle(nullptr), nullptr
    );

    // Create main control buttons, centered horizontally.
    CreateButton(L"Save", ID_VCURVE_SAVE);
    CreateButton(L"Reset", ID_VCURVE_RESET);
    CreateButton(L"Linear", ID_VCURVE_LINEAR);
    CreateButton(L"Log", ID_VCURVE_LOG);
    CreateButton(L"Exp", ID_VCURVE_EXP);
    CreateButton(L"S-Curve", ID_VCURVE_SCURVE);
    CreateButton(L"Smooth", ID_VCURVE_SMOOTH);
    LayoutControls();
}

void VelocityCurveEditor::CreateButton(const wchar_t* text, int id) {
    CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        hwndEditor,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr),
        nullptr
    );
}

void VelocityCurveEditor::LayoutControls() {
    RECT rc = GetClientRectRect();
    const int nameX = S(MARGIN + NAME_LABEL_WIDTH);
    const int nameH = S(24);
    SetWindowPos(hwndCurveName, nullptr, nameX, S(TOP_MARGIN) + (S(TOP_CONTROL_HEIGHT) - nameH) / 2,
        S(NAME_FIELD_WIDTH), nameH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hwndGridlines, nullptr, rc.right - S(MARGIN) - S(GRID_TOGGLE_WIDTH), S(TOP_MARGIN),
        S(GRID_TOGGLE_WIDTH), S(TOP_CONTROL_HEIGHT), SWP_NOZORDER | SWP_NOACTIVATE);

    const int ids[] = { ID_VCURVE_SAVE, ID_VCURVE_RESET, ID_VCURVE_LINEAR, ID_VCURVE_LOG, ID_VCURVE_EXP, ID_VCURVE_SCURVE, ID_VCURVE_SMOOTH };
    const int count = static_cast<int>(std::size(ids));
    const int totalButtonWidth = count * S(BUTTON_WIDTH) + (count - 1) * S(BUTTON_GAP);
    int x = (rc.right - totalButtonWidth) / 2;
    for (int id : ids) {
        SetWindowPos(GetDlgItem(hwndEditor, id), nullptr, x, S(BUTTONS_TOP), S(BUTTON_WIDTH), S(BUTTON_HEIGHT), SWP_NOZORDER | SWP_NOACTIVATE);
        x += S(BUTTON_WIDTH) + S(BUTTON_GAP);
    }
    SendMessageW(hwndCurveName, WM_SETFONT, reinterpret_cast<WPARAM>(ui::font(dpi, LARGE_FONT_SIZE, FW_NORMAL)), TRUE);
}

void VelocityCurveEditor::Restyle() {
    if (fieldBrush)
        DeleteObject(fieldBrush);
    fieldBrush = CreateSolidBrush(ui::pal().field);
    ui::theme_window(hwndEditor);
    ui::theme_control(hwndCurveName, L"DarkMode_CFD");
    RedrawWindow(hwndEditor, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}

void VelocityCurveEditor::OnClose() {
    DestroyWindow(hwndEditor);
}

void VelocityCurveEditor::ResetToLinear() {
    for (int i = 0; i < POINT_COUNT; i++) {
        points[i] = (i * MAX_VELOCITY) / (POINT_COUNT - 1);
    }
}

LRESULT CALLBACK VelocityCurveEditor::EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* created = reinterpret_cast<VelocityCurveEditor*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        created->hwndEditor = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    VelocityCurveEditor* editor = reinterpret_cast<VelocityCurveEditor*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!editor) return DefWindowProc(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT:
        editor->OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        editor->OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        editor->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        editor->tracking = false;
        if (editor->hoveredPoint != -1 && !editor->isDragging) {
            editor->hoveredPoint = -1;
            editor->InvalidateCurveArea();
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        editor->OnMouseUp();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursor(nullptr, editor->InGrid(pt.x, pt.y) ? IDC_CROSS : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) editor->OnClose();
        return 0;
    case WM_COMMAND:
        return editor->OnCommand(wParam, lParam);
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        ui::draw_button(dis, dis->CtlID == ID_VCURVE_GRIDLINES && editor->showGridlines, editor->dpi);
        return TRUE;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wParam), ui::pal().fieldText);
        SetBkColor(reinterpret_cast<HDC>(wParam), ui::pal().field);
        return reinterpret_cast<LRESULT>(editor->fieldBrush);
    case WM_THEMECHANGED:
        editor->Restyle();
        return 0;
    case WM_DPICHANGED: {
        editor->dpi = HIWORD(wParam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        editor->LayoutControls();
        editor->InvalidateCurveArea();
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        if (hwnd == activeWindow)
            activeWindow = nullptr;
        if (editor->hwndParent && IsWindow(editor->hwndParent))
            SetForegroundWindow(editor->hwndParent);
        delete editor;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT VelocityCurveEditor::OnCommand(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) {
    case ID_VCURVE_SAVE:
        if (HIWORD(wParam) == BN_CLICKED) { SaveCurve(); return 0; }
        break;
    case ID_VCURVE_RESET:
        if (HIWORD(wParam) == BN_CLICKED) { ResetToLinear(); statusText.clear(); InvalidateCurveArea(); return 0; }
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
        if (HIWORD(wParam) == BN_CLICKED) { SmoothCurve(); statusText.clear(); InvalidateCurveArea(); return 0; }
        break;
    case ID_VCURVE_GRIDLINES:
        if (HIWORD(wParam) == BN_CLICKED) {
            showGridlines = !showGridlines;
            InvalidateRect(hwndGridlines, nullptr, FALSE);
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
    RECT clientRect = GetClientRectRect();
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));
    SetDCBrushColor(memDC, ui::pal().panel);
    FillRect(memDC, &clientRect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    {
        Gdiplus::Graphics graphics(memDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        DrawGrid(graphics, clientRect);
        DrawCurve(graphics, clientRect);
    }
    DrawInfoPanel(memDC);
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwndEditor, &ps);
}
