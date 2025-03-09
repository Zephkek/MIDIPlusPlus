#include "TrackControl.hpp"
#include "PlaybackSystem.hpp"
#include <windowsx.h>
#include <algorithm>
#include <sstream>

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
    , m_scrollPos(0)
    , m_maxVisibleTracks(0)
    , m_hoveredTrack(-1)
    , m_selectedTrack(-1)
    , m_hFont(nullptr)
    , m_hBoldFont(nullptr)
    , m_hBackgroundBrush(nullptr)
    , m_hTrackBrush(nullptr)
    , m_hSelectedTrackBrush(nullptr)
    , m_hHeaderBrush(nullptr)
    , m_player(nullptr)
{
    SetRectEmpty(&m_rcTrackUpdate);

    m_hBackgroundBrush = CreateSolidBrush(RGB(248, 248, 250));        
    m_hTrackBrush = CreateSolidBrush(RGB(252, 252, 255));          
    m_hSelectedTrackBrush = CreateSolidBrush(RGB(230, 240, 255));     
    m_hHeaderBrush = CreateSolidBrush(RGB(235, 235, 240));            

    NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

    LOGFONT lf = ncm.lfMessageFont;
    lf.lfHeight = (lf.lfHeight * 11) / 10;  
    lf.lfQuality = CLEARTYPE_QUALITY;      
    m_hFont = CreateFontIndirect(&lf);

    lf.lfWeight = FW_BOLD;
    m_hBoldFont = CreateFontIndirect(&lf);
}

TrackControl::~TrackControl() {
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hBoldFont) DeleteObject(m_hBoldFont);
    if (m_hBackgroundBrush) DeleteObject(m_hBackgroundBrush);
    if (m_hTrackBrush) DeleteObject(m_hTrackBrush);
    if (m_hSelectedTrackBrush) DeleteObject(m_hSelectedTrackBrush);
    if (m_hHeaderBrush) DeleteObject(m_hHeaderBrush);
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
        WS_CHILD | WS_VISIBLE | WS_BORDER,
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

    m_maxVisibleTracks = (height - HEADER_HEIGHT) / TRACK_HEIGHT;
    UpdateScrollInfo();
    InitializeDefaultTracks();
    if (g_player) {
        SetPlayer(g_player);
    }

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

    m_muteButtonStates.clear();
    m_soloButtonStates.clear();

    for (size_t i = 0; i < m_tracks.size(); ++i) {
        m_muteButtonStates[static_cast<int>(i)] = { false, false };
        m_soloButtonStates[static_cast<int>(i)] = { false, false };
    }

    UpdateScrollInfo();
    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

void TrackControl::SetPlayer(VirtualPianoPlayer* player) {
    m_player = player;
}

void TrackControl::RefreshDisplay() {
    if (m_hWnd) {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
}

int TrackControl::GetColumnWidth(Column col) const {
    switch (col) {
    case Column::TrackNumber: return 40;   
    case Column::TrackName: return 180;
    case Column::Channel: return 70; 
    case Column::Program: return 190;      
    case Column::NoteCount: return 80;
    case Column::Mute: return 60;         
    case Column::Solo: return 60;        
    default: return 0;
    }
}

int TrackControl::GetColumnPosition(Column col) const {
    int pos = 10; 
    const int COLUMN_PADDING = 12;

    for (int i = 0; i < static_cast<int>(col); ++i) {
        pos += GetColumnWidth(static_cast<Column>(i)) + COLUMN_PADDING;
    }

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

int TrackControl::HitTest(int x, int y) {
    if (y < HEADER_HEIGHT) {
        return -1; // Hit the header
    }

    int trackIndex = ((y - HEADER_HEIGHT) / TRACK_HEIGHT) + m_scrollPos;
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

    int buttonX = GetColumnPosition(col) + ((GetColumnWidth(col) - BUTTON_WIDTH) / 2);
    int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + ((TRACK_HEIGHT - BUTTON_HEIGHT) / 2);

    RECT buttonRect = { buttonX, buttonY, buttonX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT };
    return (x >= buttonRect.left && x < buttonRect.right &&
        y >= buttonRect.top && y < buttonRect.bottom);
}


void TrackControl::OnMouseMove(int x, int y) {
    int trackIndex = HitTest(x, y);
    if (trackIndex != m_hoveredTrack) {
        if (m_hoveredTrack >= 0) {
            int displayIndex = m_hoveredTrack - m_scrollPos;
            if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
                RECT trackRect = { 0, HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT),
                                   m_width, HEADER_HEIGHT + ((displayIndex + 1) * TRACK_HEIGHT) };
                InvalidateRect(m_hWnd, &trackRect, FALSE);
            }
        }
        m_hoveredTrack = trackIndex;
        if (m_hoveredTrack >= 0) {
            int displayIndex = m_hoveredTrack - m_scrollPos;
            if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
                RECT trackRect = { 0, HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT),
                                   m_width, HEADER_HEIGHT + ((displayIndex + 1) * TRACK_HEIGHT) };
                InvalidateRect(m_hWnd, &trackRect, FALSE);
            }
        }
    }

    for (size_t i = 0; i < m_tracks.size(); ++i) {
        int index = static_cast<int>(i);
        int displayIndex = index - m_scrollPos;
        if (displayIndex < 0 || displayIndex >= m_maxVisibleTracks)
            continue;

        bool wasMuteHovered = m_muteButtonStates[index].isHovered;
        bool wasSoloHovered = m_soloButtonStates[index].isHovered;

        m_muteButtonStates[index].isHovered = IsButtonHit(Column::Mute, index, x, y);
        m_soloButtonStates[index].isHovered = IsButtonHit(Column::Solo, index, x, y);

        if (wasMuteHovered != m_muteButtonStates[index].isHovered) {
            int buttonX = GetColumnPosition(Column::Mute) + ((GetColumnWidth(Column::Mute) - BUTTON_WIDTH) / 2);
            int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + (TRACK_HEIGHT - BUTTON_HEIGHT) / 2;
            RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
            InvalidateRect(m_hWnd, &buttonRect, FALSE);
        }
        if (wasSoloHovered != m_soloButtonStates[index].isHovered) {
            int buttonX = GetColumnPosition(Column::Solo) + ((GetColumnWidth(Column::Solo) - BUTTON_WIDTH) / 2);
            int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + (TRACK_HEIGHT - BUTTON_HEIGHT) / 2;
            RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
            InvalidateRect(m_hWnd, &buttonRect, FALSE);
        }
    }
}

void TrackControl::OnLButtonDown(int x, int y) {
    int trackIndex = HitTest(x, y);

    if (trackIndex >= 0 && m_tracks[trackIndex].channel == -1) {
        return;
    }

    if (IsButtonHit(Column::Mute, trackIndex, x, y)) {
        m_muteButtonStates[trackIndex].isPressed = true;
        int displayIndex = trackIndex - m_scrollPos;
        if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
            int buttonX = GetColumnPosition(Column::Mute) + ((GetColumnWidth(Column::Mute) - BUTTON_WIDTH) / 2);
            int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + (TRACK_HEIGHT - BUTTON_HEIGHT) / 2;
            RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
            InvalidateRect(m_hWnd, &buttonRect, FALSE);
        }
        SetCapture(m_hWnd);
        return;
    }

    if (IsButtonHit(Column::Solo, trackIndex, x, y)) {
        m_soloButtonStates[trackIndex].isPressed = true;
        int displayIndex = trackIndex - m_scrollPos;
        if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
            int buttonX = GetColumnPosition(Column::Solo) + ((GetColumnWidth(Column::Solo) - BUTTON_WIDTH) / 2);
            int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + (TRACK_HEIGHT - BUTTON_HEIGHT) / 2;
            RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
            InvalidateRect(m_hWnd, &buttonRect, FALSE);
        }
        SetCapture(m_hWnd);
        return;
    }

    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
        if (m_selectedTrack != trackIndex) {
            if (m_selectedTrack >= 0) {
                int oldDisplayIndex = m_selectedTrack - m_scrollPos;
                if (oldDisplayIndex >= 0 && oldDisplayIndex < m_maxVisibleTracks) {
                    RECT oldTrackRect = { 0, HEADER_HEIGHT + (oldDisplayIndex * TRACK_HEIGHT),
                                            m_width, HEADER_HEIGHT + ((oldDisplayIndex + 1) * TRACK_HEIGHT) };
                    InvalidateRect(m_hWnd, &oldTrackRect, FALSE);
                }
            }
            m_selectedTrack = trackIndex;
            int newDisplayIndex = trackIndex - m_scrollPos;
            if (newDisplayIndex >= 0 && newDisplayIndex < m_maxVisibleTracks) {
                RECT newTrackRect = { 0, HEADER_HEIGHT + (newDisplayIndex * TRACK_HEIGHT),
                                        m_width, HEADER_HEIGHT + ((newDisplayIndex + 1) * TRACK_HEIGHT) };
                InvalidateRect(m_hWnd, &newTrackRect, FALSE);
            }
        }
    }
}

void TrackControl::OnLButtonUp(int x, int y) {
    ReleaseCapture();
    int trackIndex = HitTest(x, y);
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size()))
        return;

    if (m_tracks[trackIndex].channel == -1)
        return;

    bool updated = false;
    if (m_muteButtonStates[trackIndex].isPressed) {
        m_muteButtonStates[trackIndex].isPressed = false;
        if (IsButtonHit(Column::Mute, trackIndex, x, y)) {
            m_tracks[trackIndex].isMuted = !m_tracks[trackIndex].isMuted;
            int displayIndex = trackIndex - m_scrollPos;
            if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
                int buttonX = GetColumnPosition(Column::Mute) + ((GetColumnWidth(Column::Mute) - BUTTON_WIDTH) / 2);
                int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + ((TRACK_HEIGHT - BUTTON_HEIGHT) / 2);
                RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
                InvalidateRect(m_hWnd, &buttonRect, FALSE);
            }
            if (m_player) {
                m_player->set_track_mute(trackIndex, m_tracks[trackIndex].isMuted);
            }
            updated = true;
        }
    }

    if (m_soloButtonStates[trackIndex].isPressed) {
        m_soloButtonStates[trackIndex].isPressed = false;
        if (IsButtonHit(Column::Solo, trackIndex, x, y)) {
            m_tracks[trackIndex].isSoloed = !m_tracks[trackIndex].isSoloed;
            int displayIndex = trackIndex - m_scrollPos;
            if (displayIndex >= 0 && displayIndex < m_maxVisibleTracks) {
                int buttonX = GetColumnPosition(Column::Solo) + ((GetColumnWidth(Column::Solo) - BUTTON_WIDTH) / 2);
                int buttonY = HEADER_HEIGHT + (displayIndex * TRACK_HEIGHT) + ((TRACK_HEIGHT - BUTTON_HEIGHT) / 2);
                RECT buttonRect = { buttonX - 2, buttonY - 2, buttonX + BUTTON_WIDTH + 4, buttonY + BUTTON_HEIGHT + 4 };
                InvalidateRect(m_hWnd, &buttonRect, FALSE);
            }
            if (m_player) {
                m_player->set_track_solo(trackIndex, m_tracks[trackIndex].isSoloed);
            }
            updated = true;
        }
    }

    if (updated && !IsRectEmpty(&m_rcTrackUpdate)) {
        InvalidateRect(m_hWnd, &m_rcTrackUpdate, FALSE);
        SetRectEmpty(&m_rcTrackUpdate);
    }
}

void TrackControl::DrawHeaders(HDC hdc) {
    RECT headerRect = { 0, 0, m_width, HEADER_HEIGHT };
    FillRect(hdc, &headerRect, m_hHeaderBrush);

    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(190, 190, 200));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, 0, HEADER_HEIGHT - 1, nullptr);
    LineTo(hdc, m_width, HEADER_HEIGHT - 1);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(60, 60, 80));
    HFONT hOldFont = (HFONT)SelectObject(hdc, m_hBoldFont);

    const char* headers[] = { "#", "Track Name", "Channel", "Instrument", "Notes", "Mute", "Solo" };
    UINT alignments[] = { DT_CENTER, DT_LEFT, DT_CENTER, DT_LEFT, DT_CENTER, DT_CENTER, DT_CENTER };
    const int HEADER_PADDING = 10;

    for (int i = 0; i < 7; ++i) {
        int x = GetColumnPosition(static_cast<Column>(i));
        RECT textRect = { x + HEADER_PADDING, 0, x + GetColumnWidth(static_cast<Column>(i)) - HEADER_PADDING, HEADER_HEIGHT };
        DrawTextA(hdc, headers[i], -1, &textRect, alignments[i] | DT_VCENTER | DT_SINGLELINE);
    }

    HPEN hDividerPen = CreatePen(PS_SOLID, 1, RGB(210, 210, 215));
    SelectObject(hdc, hDividerPen);
    for (int i = 0; i < 6; ++i) {
        int x = GetColumnPosition(static_cast<Column>(i)) + GetColumnWidth(static_cast<Column>(i)) - 1;
        MoveToEx(hdc, x, 5, nullptr);
        LineTo(hdc, x, HEADER_HEIGHT - 5);
    }
    SelectObject(hdc, hOldFont);
    DeleteObject(hDividerPen);
}

void TrackControl::DrawButton(HDC hdc, const RECT& rect, bool isPressed, bool isToggled, const char* text) {
    COLORREF fillColor, borderColor, textColor;

    textColor = RGB(60, 60, 60);

    if (isToggled) {
        textColor = RGB(255, 255, 255);
        if (strcmp(text, "Solo") == 0) {
            fillColor = RGB(100, 149, 237);  
            borderColor = RGB(65, 105, 225);    
        }
        else {
            fillColor = RGB(100, 180, 100);     
            borderColor = RGB(70, 140, 70);
        }
    }
    else {
        fillColor = RGB(245, 245, 245);
        borderColor = RGB(180, 180, 180);
    }

    if (isPressed) {
        fillColor = RGB(210, 210, 230);
        borderColor = RGB(150, 150, 180);
    }

    HBRUSH hBrush = CreateSolidBrush(fillColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);

    RECT textRect = rect;
    if (isPressed) {
        textRect.left += 1;
        textRect.top += 1;
    }
    textRect.left += 2;
    textRect.right -= 2;

    DrawTextA(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void TrackControl::DrawTrack(HDC hdc, int index, int yPos) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size()))
        return;

    const TrackInfo& track = m_tracks[index];
    bool isSelected = (index == m_selectedTrack);
    bool isHovered = (index == m_hoveredTrack);

    RECT trackRect = { 0, yPos, m_width, yPos + TRACK_HEIGHT };
    if (isSelected) {
        FillRect(hdc, &trackRect, m_hSelectedTrackBrush);
    }
    else if (isHovered) {
        HBRUSH hHoverBrush = CreateSolidBrush(RGB(245, 245, 250));
        FillRect(hdc, &trackRect, hHoverBrush);
        DeleteObject(hHoverBrush);
    }
    else {
        FillRect(hdc, &trackRect, m_hTrackBrush);
    }

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(230, 230, 235));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, 0, yPos + TRACK_HEIGHT - 1, nullptr);
    LineTo(hdc, m_width, yPos + TRACK_HEIGHT - 1);

    for (int i = 0; i < 6; ++i) {
        int x = GetColumnPosition(static_cast<Column>(i)) + GetColumnWidth(static_cast<Column>(i)) - 1;
        MoveToEx(hdc, x, yPos + 4, nullptr);
        LineTo(hdc, x, yPos + TRACK_HEIGHT - 5);
    }
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    COLORREF textColor = isSelected ? RGB(0, 0, 120) : RGB(40, 40, 40);
    SetTextColor(hdc, textColor);
    HFONT hOldFont = (HFONT)SelectObject(hdc, m_hFont);
    const int TEXT_PADDING = 8;

    char buffer[32];
    sprintf_s(buffer, "%d", index + 1);
    RECT textRect = {
        GetColumnPosition(Column::TrackNumber) + TEXT_PADDING,
        yPos,
        GetColumnPosition(Column::TrackNumber) + GetColumnWidth(Column::TrackNumber) - TEXT_PADDING,
        yPos + TRACK_HEIGHT
    };
    DrawTextA(hdc, buffer, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    textRect.left = GetColumnPosition(Column::TrackName) + TEXT_PADDING;
    textRect.right = GetColumnPosition(Column::TrackName) + GetColumnWidth(Column::TrackName) - TEXT_PADDING;
    DrawTextA(hdc, track.trackName.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (track.channel >= 0) {
        sprintf_s(buffer, "Ch %d", track.channel + 1);
        textRect.left = GetColumnPosition(Column::Channel) + TEXT_PADDING;
        textRect.right = GetColumnPosition(Column::Channel) + GetColumnWidth(Column::Channel) - TEXT_PADDING;
        DrawTextA(hdc, buffer, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    textRect.left = GetColumnPosition(Column::Program) + TEXT_PADDING;
    textRect.right = GetColumnPosition(Column::Program) + GetColumnWidth(Column::Program) - TEXT_PADDING;
    if (track.isDrums) {
        RECT drumRect = textRect;
        drumRect.left -= 6;
        drumRect.right += 6;
        drumRect.top = yPos + 4;
        drumRect.bottom = yPos + TRACK_HEIGHT - 4;
        HBRUSH hDrumBrush = CreateSolidBrush(RGB(255, 222, 173));
        HPEN hDrumPen = CreatePen(PS_SOLID, 1, RGB(210, 105, 30));
        HPEN hOldDrumPen = (HPEN)SelectObject(hdc, hDrumPen);
        HBRUSH hOldDrumBrush = (HBRUSH)SelectObject(hdc, hDrumBrush);
        RoundRect(hdc, drumRect.left, drumRect.top, drumRect.right, drumRect.bottom, 10, 10);
        SelectObject(hdc, hOldDrumBrush);
        SelectObject(hdc, hOldDrumPen);
        DeleteObject(hDrumBrush);
        DeleteObject(hDrumPen);

        std::string drumLabel = "DRUMS: " + track.instrumentName;
        DrawTextA(hdc, drumLabel.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else {
        DrawTextA(hdc, track.instrumentName.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    sprintf_s(buffer, "%d", track.noteCount);
    textRect.left = GetColumnPosition(Column::NoteCount) + TEXT_PADDING;
    textRect.right = GetColumnPosition(Column::NoteCount) + GetColumnWidth(Column::NoteCount) - TEXT_PADDING;
    DrawTextA(hdc, buffer, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int buttonX = GetColumnPosition(Column::Mute) + ((GetColumnWidth(Column::Mute) - BUTTON_WIDTH) / 2);
    int buttonY = yPos + (TRACK_HEIGHT - BUTTON_HEIGHT) / 2;
    RECT buttonRect = { buttonX, buttonY, buttonX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT };
    DrawButton(hdc, buttonRect, m_muteButtonStates[index].isPressed, track.isMuted, "Mute");

    buttonX = GetColumnPosition(Column::Solo) + ((GetColumnWidth(Column::Solo) - BUTTON_WIDTH) / 2);
    buttonRect = { buttonX, buttonY, buttonX + BUTTON_WIDTH, buttonY + BUTTON_HEIGHT };
    DrawButton(hdc, buttonRect, m_soloButtonStates[index].isPressed, track.isSoloed, "Solo");

    SelectObject(hdc, hOldFont);
}

void TrackControl::DrawControl(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    FillRect(hdc, &clientRect, m_hBackgroundBrush);

    DrawHeaders(hdc);

    int visibleTracks = std::min(m_maxVisibleTracks, static_cast<int>(m_tracks.size()) - m_scrollPos);
    for (int i = 0; i < visibleTracks; ++i) {
        int trackIndex = i + m_scrollPos;
        int yPos = HEADER_HEIGHT + (i * TRACK_HEIGHT);
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
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
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
    case WM_LBUTTONDOWN: {
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
        m_maxVisibleTracks = (m_height - HEADER_HEIGHT) / TRACK_HEIGHT;
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
