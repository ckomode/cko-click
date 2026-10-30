// click: clicks the left mouse button over and over at the interval you
// set (a number plus ms / seconds / minutes). F6 starts and stops clicking;
// F8 quits.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#define ID_INTERVAL 1
#define ID_UNIT     2
#define HK_TOGGLE   1 // F6
#define HK_QUIT     2 // F8
#define WHITE       RGB(255, 255, 255)
#define BLACK       RGB(0, 0, 0)

// Layout at 96 DPI; scaled by px().
#define CLIENT_W  280
#define CLIENT_H  166
#define ROW_EVERY 64
#define FIELD_H   24

static HWND   hwnd, editBox, unitBox;
static HBRUSH blackBrush, whiteBrush;
static HFONT  titleFont, bodyFont, statusFont, hintFont;
static int    dpi;
static BOOL   enabled, hotkeysTaken;

// The click thread sleeps on runEvent while off. Clearing it and setting
// wakeEvent stops clicking at once, even in the middle of a long wait.
static HANDLE runEvent, wakeEvent, timer;
static volatile LONG64 interval100ns;

static const WCHAR *const unitNames[] = {L"ms", L"seconds", L"minutes"};
static const LONG64 unit100ns[] = {10000, 10000000, 600000000};

static int px(int v) { return v * dpi / 96; }

static HFONT newFont(int size, int weight) {
    return CreateFontW(-px(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static RECT editRect(void) { return (RECT){px(96), px(ROW_EVERY), px(160), px(ROW_EVERY + FIELD_H)}; }
static RECT unitRect(void) { return (RECT){px(170), px(ROW_EVERY), px(252), px(ROW_EVERY + FIELD_H)}; }

static DWORD WINAPI clickLoop(LPVOID unused) {
    INPUT in[2] = {{.type = INPUT_MOUSE, .mi.dwFlags = MOUSEEVENTF_LEFTDOWN},
                   {.type = INPUT_MOUSE, .mi.dwFlags = MOUSEEVENTF_LEFTUP}};
    HANDLE waits[2] = {wakeEvent, timer};
    for (;;) {
        WaitForMultipleObjects(1, &runEvent, FALSE, INFINITE);
        SendInput(2, in, sizeof(INPUT));
        LARGE_INTEGER due = {.QuadPart = -interval100ns};
        SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
        WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    }
    return 0;
}

// readInterval updates the click interval from the number box and unit list.
// A change cuts short the current wait, so it applies right away even in the
// middle of a long interval.
static void readInterval(void) {
    WCHAR buf[16];
    int n = GetWindowTextW(editBox, buf, 16);
    LONG64 v = 0;
    for (int i = 0; i < n; i++) v = v * 10 + (buf[i] - L'0');
    LRESULT sel = SendMessageW(unitBox, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel > 2) sel = 0;
    v *= unit100ns[sel];
    if (v < 10000) v = 10000; // 1ms minimum
    if (v != interval100ns) {
        interval100ns = v;
        if (enabled) SetEvent(wakeEvent);
    }
}

static void setEnabled(BOOL on) {
    enabled = on;
    if (on) {
        SetEvent(runEvent);
    } else {
        ResetEvent(runEvent);
        SetEvent(wakeEvent);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void text(HDC dc, const WCHAR *s, HFONT font, RECT r, UINT align) {
    SelectObject(dc, font);
    DrawTextW(dc, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE);
}

static void paint(void) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT c = {0, 0, px(CLIENT_W), px(CLIENT_H)};
    FillRect(dc, &c, blackBrush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, WHITE);

    text(dc, L"made by cko", titleFont, (RECT){0, px(18), c.right, px(48)}, DT_CENTER);
    RECT e = editRect();
    text(dc, L"every", bodyFont, (RECT){px(28), e.top, e.left, e.bottom}, DT_LEFT);
    // White frame around the number box (the box itself has no border).
    int o = px(3);
    FrameRect(dc, &(RECT){e.left - o, e.top - o, e.right + o, e.bottom + o}, whiteBrush);
    text(dc, hotkeysTaken ? L"F6 / F8 TAKEN" : enabled ? L"ON" : L"OFF", statusFont, (RECT){0, px(100), c.right, px(124)}, DT_CENTER);
    text(dc, L"F6  on / off   ·   F8  quit", hintFont, (RECT){0, px(132), c.right, px(152)}, DT_CENTER);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1; // paint() fills the background; avoids flicker
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, WHITE);
        SetBkColor((HDC)wp, BLACK);
        return (LRESULT)blackBrush;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_INTERVAL || LOWORD(wp) == ID_UNIT) readInterval();
        return 0;
    case WM_HOTKEY: // Windows tells us only about F6 and F8, nothing else
        if (wp == HK_TOGGLE) setEnabled(!enabled);
        if (wp == HK_QUIT) {
            ResetEvent(runEvent);
            DestroyWindow(h);
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HWND child(const WCHAR *cls, DWORD style, RECT r, int id) {
    return CreateWindowExW(0, cls, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, r.left, r.top,
                           r.right - r.left, r.bottom - r.top, hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    SetProcessDPIAware();
    HDC sdc = GetDC(NULL);
    dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(NULL, sdc);

    blackBrush = CreateSolidBrush(BLACK);
    whiteBrush = CreateSolidBrush(WHITE);
    titleFont = newFont(14, FW_SEMIBOLD);
    bodyFont = newFont(13, FW_NORMAL);
    statusFont = newFont(13, FW_SEMIBOLD);
    hintFont = newFont(11, FW_NORMAL);

    WNDCLASSW wc = {.lpfnWndProc = wndProc, .hCursor = LoadCursorW(NULL, IDC_ARROW), .lpszClassName = L"cko"};
    RegisterClassW(&wc);
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, px(CLIENT_W), px(CLIENT_H)};
    AdjustWindowRect(&r, style, FALSE);
    hwnd = CreateWindowExW(0, L"cko", L"cko", style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, NULL, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark); // dark title bar (Windows 10+)

    RECT e = editRect();
    e.top += px(3); // vertically center the digits in the framed box
    editBox = child(L"EDIT", ES_NUMBER | ES_CENTER, e, ID_INTERVAL);
    RECT u = unitRect();
    u.bottom += px(100); // room for the open dropdown list
    unitBox = child(L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, u, ID_UNIT);
    SendMessageW(editBox, WM_SETFONT, (WPARAM)bodyFont, TRUE);
    SendMessageW(unitBox, WM_SETFONT, (WPARAM)bodyFont, TRUE);
    SendMessageW(editBox, EM_LIMITTEXT, 9, 0);
    for (int i = 0; i < 3; i++) SendMessageW(unitBox, CB_ADDSTRING, 0, (LPARAM)unitNames[i]);
    SendMessageW(unitBox, CB_SETCURSEL, 0, 0);
    SetWindowTextW(editBox, L"1");

    runEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    // High-resolution timer so 1ms really means about 1ms (Windows 10 1803+).
    timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    CreateThread(NULL, 64 * 1024, clickLoop, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);

    // MOD_NOREPEAT: holding F6 toggles once. Hotkeys never reach other apps.
    // If another program already owns F6 or F8, say so where ON/OFF would be.
    hotkeysTaken = !RegisterHotKey(hwnd, HK_TOGGLE, MOD_NOREPEAT, VK_F6) |
                   !RegisterHotKey(hwnd, HK_QUIT, MOD_NOREPEAT, VK_F8);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
