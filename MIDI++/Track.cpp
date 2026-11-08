#include "TrackControl.hpp"
#include "Output.hpp"
#include "Widgets.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <sstream>

static std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    UINT cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0) ? CP_UTF8 : CP_ACP;
    int n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static void Fill(HDC hdc, const RECT& r, COLORREF c) {
    SetDCBrushColor(hdc, c);
    FillRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

// if this SHIT DOES NOT FUCKING REGISTER CORRECTLY IM SWITCHING TO FUCKING MAC
static bool RegisterTrackControlClass(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TrackControl::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // Default background
    wc.lpszClassName = L"TrackControlClass";

    ATOM atom = RegisterClassExW(&wc);
    if (atom == 0) {
        return false;
    }

    registered = true;
    return true;
}

TrackControl::TrackControl()
    : m_hWnd(nullptr)
    , m_hParentWnd(nullptr)
    , m_x(0), m_y(0), m_width(0), m_height(0)
    , m_primed(false)
    , m_scrollPos(0)
    , m_maxVisibleTracks(0)
    , m_hoveredTrack(-1)
    , m_selectedTrack(-1)
    , m_pressedTrack(-1)
    , m_pressedColumn(Column::Mute)
    , m_hotTrack(-1)
    , m_hotColumn(Column::Mute)
    , m_hFont(nullptr)
    , m_hBoldFont(nullptr)
{
    MakeFonts();
}

TrackControl::~TrackControl() {
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hBoldFont) DeleteObject(m_hBoldFont);
}

bool TrackControl::Create(HWND hParentWnd, int x, int y, int width, int height) {
    if (!hParentWnd) {
        return false;
    }

    m_hParentWnd = hParentWnd;
    m_x = x;
    m_y = y;
    m_width = width;
    m_height = height;
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(hParentWnd, GWLP_HINSTANCE);
    if (!hInstance) {
        return false;
    }

    if (!RegisterTrackControlClass(hInstance)) {
        return false;
    }

    m_hWnd = CreateWindowW(
        L"TrackControlClass",
        L"",
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        hParentWnd,
        NULL,
        hInstance,
        this
    );

    if (!m_hWnd) {
        return false;
    }

    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);

    m_maxVisibleTracks = std::max(1, (height - HeaderH()) / RowH());
    UpdateScrollInfo();
    InitializeDefaultTracks();

    return true;
}

void TrackControl::InitializeDefaultTracks() {
    // Initialize with a default track
    std::vector<TrackInfo> defaultTracks;
    TrackInfo defaultTrack;
    defaultTrack.trackName = "No tracks loaded";
    defaultTrack.instrumentName = "Load a MIDI file to see tracks";
    defaultTrack.noteCount = 0;
    defaultTrack.channel = -1;
    defaultTrack.programNumber = -1;
    defaultTracks.push_back(defaultTrack);

    SetTracks(defaultTracks);
}

void TrackControl::SetTracks(const std::vector<TrackInfo>& tracks) {
    m_tracks = tracks;
    m_hoveredTrack = -1;
    m_pressedTrack = -1;
    m_hotTrack = -1;
    m_selectedTrack = -1;
    m_level.assign(m_tracks.size(), 0.0f);
    m_seen.assign(m_tracks.size(), 0);
    m_keys.assign(m_tracks.size(), { 0, 0 });
    m_primed = false;
    m_scrollPos = std::clamp(m_scrollPos, 0, std::max(0, static_cast<int>(m_tracks.size()) - m_maxVisibleTracks));

    UpdateScrollInfo();
    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void TrackControl::SetOnChange(std::function<void()> fn) {
    m_onChange = std::move(fn);
}

void TrackControl::MakeFonts() {
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hBoldFont) DeleteObject(m_hBoldFont);
    LOGFONTW lf = {};
    lf.lfHeight = -S(12);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_hFont = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(9);
    lf.lfWeight = FW_BOLD;
    m_hBoldFont = CreateFontIndirectW(&lf);
}

void TrackControl::Restyle() {
    if (m_hWnd)
        InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TrackControl::SetDpi(int dpi) {
    m_dpi = dpi;
    MakeFonts();
    m_maxVisibleTracks = std::max(1, (m_height - HeaderH()) / RowH());
    UpdateScrollInfo();
    if (m_hWnd)
        InvalidateRect(m_hWnd, nullptr, FALSE);
}

void TrackControl::RefreshDisplay() {
    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void TrackControl::Animate(const out::Activity* live, size_t count, double dt) {
    if (!m_hWnd || m_tracks.empty())
        return;
    const float decay = static_cast<float>(std::exp(-3.2 * std::clamp(dt, 0.0, 0.1)));
    const int first = m_scrollPos, last = m_scrollPos + m_maxVisibleTracks;
    bool changed = false;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        float lv = m_level[i] * decay;
        std::array<uint64_t, 2> keys{ 0, 0 };
        if (live && i < count) {
            const uint32_t h = live[i].hit.load(std::memory_order_relaxed);
            if (!m_primed)
                m_seen[i] = h >> 8;
            else if ((h >> 8) != m_seen[i]) {
                m_seen[i] = h >> 8;
                lv = std::max(lv, 0.35f + 0.65f * static_cast<float>(h & 127) / 127.0f);
            }
            keys = { live[i].mask[0].load(std::memory_order_relaxed), live[i].mask[1].load(std::memory_order_relaxed) };
            if (keys[0] | keys[1])
                lv = std::max(lv, 0.22f);
        }
        if (lv < 0.02f)
            lv = 0.0f;
        const int index = static_cast<int>(i);
        if (index >= first && index < last
            && (std::lround(lv * 16.0f) != std::lround(m_level[i] * 16.0f) || keys != m_keys[i] || (lv > 0.0f) != (m_level[i] > 0.0f)))
            changed = true;
        m_level[i] = lv;
        m_keys[i] = keys;
    }
    m_primed = live != nullptr;
    if (changed)
        InvalidateRect(m_hWnd, nullptr, FALSE);
}

int TrackControl::GetColumnWidth(Column col) const {
    static constexpr int base[] = { 16, 16, 22, 84, 110, 26, 96, 46, 300 };
    int fixed = S(6) * 2;
    for (int b : base)
        fixed += S(b) + S(6);
    int extra = std::max(0, m_width - fixed);
    int w = S(base[static_cast<int>(col)]);
    if (col == Column::TrackName)
        w += extra * 11 / 20;
    else if (col == Column::Program)
        w += extra - extra * 11 / 20;
    return w;
}

int TrackControl::GetColumnPosition(Column col) const {
    int pos = S(6);
    for (int i = 0; i < static_cast<int>(col); ++i)
        pos += GetColumnWidth(static_cast<Column>(i)) + S(6);
    return pos;
}

void TrackControl::UpdateScrollInfo() {
    if (!m_hWnd) return;

    SCROLLINFO si = { sizeof(SCROLLINFO) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = static_cast<int>(m_tracks.size()) - 1;
    si.nPage = m_maxVisibleTracks;
    si.nPos = m_scrollPos;

    SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
}

void TrackControl::ScrollTracks(int amount) {
    int oldScrollPos = m_scrollPos;
    m_scrollPos = std::max(0, std::min(static_cast<int>(m_tracks.size()) - m_maxVisibleTracks, m_scrollPos + amount));

    if (oldScrollPos != m_scrollPos) {
        UpdateScrollInfo();
        // Use FALSE to prevent background erasing since WM_PAINT fully redraws the control FUCK windows
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

int TrackControl::HitTest(int, int y) {
    if (y < HeaderH()) {
        return -1; // Hit the header
    }

    int trackIndex = ((y - HeaderH()) / RowH()) + m_scrollPos;
    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
        return trackIndex;
    }

    return -1;
}

bool TrackControl::IsButtonHit(Column col, int trackIndex, int x, int y) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size()))
        return false;

    int displayIndex = trackIndex - m_scrollPos;
    if (displayIndex < 0 || displayIndex >= m_maxVisibleTracks)
        return false;

    const int left = GetColumnPosition(col) - S(3);
    const int top = HeaderH() + displayIndex * RowH();
    return x >= left && x < left + GetColumnWidth(col) + S(6) && y >= top && y < top + RowH();
}

void TrackControl::InvalidateRow(int trackIndex) {
    const int displayIndex = trackIndex - m_scrollPos;
    if (!m_hWnd || displayIndex < 0 || displayIndex >= m_maxVisibleTracks)
        return;
    RECT r = { 0, HeaderH() + displayIndex * RowH(), m_width, HeaderH() + (displayIndex + 1) * RowH() };
    InvalidateRect(m_hWnd, &r, FALSE);
}

void TrackControl::OnMouseMove(int x, int y) {
    const int trackIndex = HitTest(x, y);
    if (trackIndex != m_hoveredTrack) {
        InvalidateRow(m_hoveredTrack);
        m_hoveredTrack = trackIndex;
        InvalidateRow(m_hoveredTrack);
    }
    int hot = -1;
    Column hotColumn = Column::Mute;
    if (trackIndex >= 0 && m_tracks[trackIndex].channel != -1) {
        if (IsButtonHit(Column::Mute, trackIndex, x, y)) {
            hot = trackIndex;
            hotColumn = Column::Mute;
        }
        else if (IsButtonHit(Column::Solo, trackIndex, x, y)) {
            hot = trackIndex;
            hotColumn = Column::Solo;
        }
    }
    if (hot != m_hotTrack || hotColumn != m_hotColumn) {
        InvalidateRow(m_hotTrack);
        m_hotTrack = hot;
        m_hotColumn = hotColumn;
        InvalidateRow(m_hotTrack);
    }
    if (m_hWnd) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&tme);
    }
}

void TrackControl::OnLButtonDown(int x, int y) {
    int trackIndex = HitTest(x, y);

    if (trackIndex >= 0 && m_tracks[trackIndex].channel == -1) {
        return;
    }

    for (Column col : { Column::Mute, Column::Solo }) {
        if (IsButtonHit(col, trackIndex, x, y)) {
            m_pressedTrack = trackIndex;
            m_pressedColumn = col;
            InvalidateRow(trackIndex);
            SetCapture(m_hWnd);
            return;
        }
    }

    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size()) && m_selectedTrack != trackIndex) {
        InvalidateRow(m_selectedTrack);
        m_selectedTrack = trackIndex;
        InvalidateRow(m_selectedTrack);
    }
}

void TrackControl::OnLButtonUp(int x, int y) {
    ReleaseCapture();
    const int pressed = m_pressedTrack;
    m_pressedTrack = -1;
    if (pressed < 0 || pressed >= static_cast<int>(m_tracks.size()))
        return;
    InvalidateRow(pressed);
    if (!IsButtonHit(m_pressedColumn, pressed, x, y))
        return;

    if (m_pressedColumn == Column::Mute)
        m_tracks[pressed].isMuted = !m_tracks[pressed].isMuted;
    else
        m_tracks[pressed].isSoloed = !m_tracks[pressed].isSoloed;
    if (m_onChange)
        m_onChange();
}

void TrackControl::DrawHeaders(HDC hdc) {
    RECT headerRect = { 0, 0, m_width, HeaderH() };
    const ui::Lcd& c = ui::lcd();
    Fill(hdc, headerRect, c.header);
    RECT rule = { 0, HeaderH() - 1, m_width, HeaderH() };
    Fill(hdc, rule, c.rule);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c.label);
    HFONT hOldFont = (HFONT)SelectObject(hdc, m_hBoldFont);

    const wchar_t* headers[] = { L"M", L"S", L"#", L"LEVEL", L"TRACK", L"CH", L"INSTRUMENT", L"NOTES", L"KEYS" };
    UINT alignments[] = { DT_CENTER, DT_CENTER, DT_RIGHT, DT_LEFT, DT_LEFT, DT_CENTER, DT_LEFT, DT_RIGHT, DT_LEFT };

    for (int i = 0; i < 9; ++i) {
        int x = GetColumnPosition(static_cast<Column>(i));
        RECT textRect = { x, 0, x + GetColumnWidth(static_cast<Column>(i)), HeaderH() };
        DrawTextW(hdc, headers[i], -1, &textRect, alignments[i] | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(hdc, hOldFont);
}

void TrackControl::DrawLed(HDC hdc, const RECT& rect, COLORREF fill, bool hot) {
    const int r = GetRValue(fill), g = GetGValue(fill), b = GetBValue(fill);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCBrushColor(hdc, fill);
    SetDCPenColor(hdc, hot ? ui::lcd().text : RGB(r * 3 / 5, g * 3 / 5, b * 3 / 5));
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, S(5), S(5));
    RECT shine = { rect.left + S(3), rect.top + std::max(1, S(2)), rect.right - S(3), rect.top + std::max(2, S(3)) };
    Fill(hdc, shine, RGB(r + (255 - r) * 2 / 5, g + (255 - g) * 2 / 5, b + (255 - b) * 2 / 5));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
}

void TrackControl::DrawKeys(HDC hdc, const RECT& rc, const std::array<uint64_t, 2>& keys, bool muted) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int blackH = std::max(2, h * 2 / 5);
    const int whiteTop = rc.top + blackH + std::max(1, S(1));
    int white = 0;
    for (int n = 21; n <= 108; ++n) {
        const int pc = n % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const bool on = !muted && ((keys[static_cast<size_t>(n >> 6)] >> (n & 63)) & 1);
        if (!black) {
            const int x1 = static_cast<int>(rc.left) + white * w / 52;
            const int x2 = std::max(x1 + 1, static_cast<int>(rc.left) + (white + 1) * w / 52 - 1);
            RECT r = { x1, whiteTop, x2, rc.bottom };
            Fill(hdc, r, on ? ui::lcd().lit : ui::lcd().keyOff);
            ++white;
        }
        else {
            const int cx = static_cast<int>(rc.left) + white * w / 52;
            const int half = std::max(1, w * 3 / 520);
            RECT r = { cx - half, rc.top, cx + half, rc.top + blackH };
            Fill(hdc, r, on ? ui::lcd().lit : ui::lcd().blackOff);
        }
    }
}

void TrackControl::DrawTrack(HDC hdc, int index, int yPos) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size()))
        return;

    const TrackInfo& track = m_tracks[index];
    const ui::Lcd& c = ui::lcd();
    const bool placeholder = track.channel == -1;
    RECT trackRect = { 0, yPos, m_width, yPos + RowH() };
    Fill(hdc, trackRect, index == m_selectedTrack ? c.hot : index == m_hoveredTrack ? c.hover : c.list);

    const bool soloAny = std::any_of(m_tracks.begin(), m_tracks.end(), [](const TrackInfo& t) { return t.isSoloed; });
    const bool silent = track.isMuted || (soloAny && !track.isSoloed);
    const float level = index < static_cast<int>(m_level.size()) ? m_level[index] : 0.0f;
    const bool active = !silent && level > 0.0f;
    const int cy = yPos + RowH() / 2;

    if (!placeholder) {
        const int ledY = cy - BtnH() / 2;
        int x = GetColumnPosition(Column::Mute) + (GetColumnWidth(Column::Mute) - BtnW()) / 2;
        RECT led = { x, ledY, x + BtnW(), ledY + BtnH() };
        const bool pressMute = m_pressedTrack == index && m_pressedColumn == Column::Mute;
        DrawLed(hdc, led, track.isMuted != pressMute ? c.mute : c.muteOff, m_hotTrack == index && m_hotColumn == Column::Mute);
        x = GetColumnPosition(Column::Solo) + (GetColumnWidth(Column::Solo) - BtnW()) / 2;
        led = { x, ledY, x + BtnW(), ledY + BtnH() };
        const bool pressSolo = m_pressedTrack == index && m_pressedColumn == Column::Solo;
        DrawLed(hdc, led, track.isSoloed != pressSolo ? c.solo : c.soloOff, m_hotTrack == index && m_hotColumn == Column::Solo);

        const int segX = GetColumnPosition(Column::Level);
        const int segW = GetColumnWidth(Column::Level);
        const int gap = std::max(1, S(1));
        const int lit = active ? static_cast<int>(std::lround(level * 16.0f)) : 0;
        const int segH = std::max(3, RowH() - S(8));
        for (int s = 0; s < 16; ++s) {
            const int sx1 = segX + s * (segW + gap) / 16;
            const int sx2 = segX + (s + 1) * (segW + gap) / 16 - gap;
            RECT seg = { sx1, cy - segH / 2, sx2, cy - segH / 2 + segH };
            const bool on = s < lit;
            Fill(hdc, seg, s < 10 ? (on ? c.lit : c.ghost) : s < 14 ? (on ? c.amber : c.amberOff) : (on ? c.red : c.redOff));
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, m_hFont);
    const COLORREF nameColor = placeholder ? c.dim
        : silent ? c.muted : track.isDrums ? (active ? c.amber : c.dim) : active ? c.text : c.name;

    wchar_t buffer[32];
    RECT textRect = { 0, yPos, 0, yPos + RowH() };
    if (!placeholder) {
        swprintf_s(buffer, L"%d", index + 1);
        textRect.left = GetColumnPosition(Column::TrackNumber);
        textRect.right = textRect.left + GetColumnWidth(Column::TrackNumber);
        SetTextColor(hdc, c.dim);
        DrawTextW(hdc, buffer, -1, &textRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    textRect.left = placeholder ? GetColumnPosition(Column::Mute) : GetColumnPosition(Column::TrackName);
    textRect.right = GetColumnPosition(Column::TrackName) + GetColumnWidth(Column::TrackName);
    SetTextColor(hdc, nameColor);
    DrawTextW(hdc, Wide(track.trackName).c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetTextColor(hdc, c.dim);
    if (track.channel >= 0) {
        swprintf_s(buffer, L"%d", track.channel + 1);
        textRect.left = GetColumnPosition(Column::Channel);
        textRect.right = textRect.left + GetColumnWidth(Column::Channel);
        DrawTextW(hdc, buffer, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    textRect.left = GetColumnPosition(Column::Program);
    textRect.right = textRect.left + GetColumnWidth(Column::Program);
    std::string program = track.isDrums ? "Drums: " + track.instrumentName : track.instrumentName;
    SetTextColor(hdc, c.dim);
    DrawTextW(hdc, Wide(program).c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (!placeholder && track.noteCount >= 0) {
        swprintf_s(buffer, L"%d", track.noteCount);
        textRect.left = GetColumnPosition(Column::NoteCount);
        textRect.right = textRect.left + GetColumnWidth(Column::NoteCount);
        SetTextColor(hdc, c.dim);
        DrawTextW(hdc, buffer, -1, &textRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (!placeholder) {
        const int kx = GetColumnPosition(Column::Keys);
        RECT keys = { kx, yPos + S(3), kx + GetColumnWidth(Column::Keys), yPos + RowH() - S(3) };
        static const std::array<uint64_t, 2> none{ 0, 0 };
        DrawKeys(hdc, keys, index < static_cast<int>(m_keys.size()) ? m_keys[index] : none, silent);
    }

    RECT rule = { 0, yPos + RowH() - 1, m_width, yPos + RowH() };
    Fill(hdc, rule, c.rule);
    SelectObject(hdc, hOldFont);
}

void TrackControl::DrawControl(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    Fill(hdc, clientRect, ui::lcd().list);

    DrawHeaders(hdc);

    int visibleTracks = std::min(m_maxVisibleTracks, static_cast<int>(m_tracks.size()) - m_scrollPos);
    for (int i = 0; i < visibleTracks; ++i) {
        int trackIndex = i + m_scrollPos;
        int yPos = HeaderH() + (i * RowH());
        DrawTrack(hdc, trackIndex, yPos);
    }
}
// Absolutely fucking mental
LRESULT TrackControl::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        RECT clientRect;
        GetClientRect(m_hWnd, &clientRect);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, std::max<LONG>(1, clientRect.right), std::max<LONG>(1, clientRect.bottom));
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        DrawControl(memDC);

        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(m_hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_VSCROLL: {
        int action = LOWORD(wParam);
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
        GetScrollInfo(m_hWnd, SB_VERT, &si);
        int oldPos = si.nPos;
        switch (action) {
        case SB_LINEUP: si.nPos -= 1; break;
        case SB_LINEDOWN: si.nPos += 1; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
        case SB_TOP: si.nPos = si.nMin; break;
        case SB_BOTTOM: si.nPos = si.nMax; break;
        }
        si.nPos = std::max(si.nMin, std::min(si.nPos, si.nMax - static_cast<int>(si.nPage) + 1));
        if (si.nPos != oldPos) {
            SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
            m_scrollPos = si.nPos;
            // Pass FALSE to avoid unnecessary background erase
            InvalidateRect(m_hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollTracks(-zDelta / WHEEL_DELTA);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnMouseMove(x, y);
        return 0;
    }
    case WM_MOUSELEAVE:
        InvalidateRow(m_hoveredTrack);
        InvalidateRow(m_hotTrack);
        m_hoveredTrack = -1;
        m_hotTrack = -1;
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnLButtonDown(x, y);
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        OnLButtonUp(x, y);
        return 0;
    }
    case WM_SIZE: {
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        m_maxVisibleTracks = std::max(1, (m_height - HeaderH()) / RowH());
        UpdateScrollInfo();
        return 0;
    }
    }
    return DefWindowProc(m_hWnd, message, wParam, lParam);
}

LRESULT CALLBACK TrackControl::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TrackControl* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = static_cast<TrackControl*>(pCreate->lpCreateParams);
        pThis->m_hWnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else {
        pThis = reinterpret_cast<TrackControl*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (pThis) {
        return pThis->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}
