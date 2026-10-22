/* Stand-in for the game as it sized its window on a 4K monitor at 150%
 * scaling (what 0.1.1 could not fill): besides the frame, it keeps its window
 * inside the work area - the monitor less the taskbar - asking by all three
 * calls the game imports, and keeps the monitor's shape (16:9) while it does. It does all of that
 * again whenever the window is resized. Without the plugin it draws smaller
 * than the monitor (3712x2088 on 3840x2160) whenever a taskbar is showing.
 * With it, it must draw at exactly the monitor's size, borderless, covering
 * the monitor, and stay that way. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static RECT g_screen;
static int g_sizing;

static LONG smallest(LONG a, LONG b)
{
    return a < b ? a : b;
}

/* The game's own resize: the monitor's size, kept inside the work area by
 * each answer it gets, at 16:9, framed, cut down to fit. */
static void game_resize(HWND w)
{
    MONITORINFO mi = { sizeof mi };
    RECT work, r;
    LONG mw = g_screen.right - g_screen.left, mh = g_screen.bottom - g_screen.top, cw = mw, ch = mh;
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

    g_sizing = 1;
    GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
    cw = smallest(cw, mi.rcWork.right - mi.rcWork.left);
    ch = smallest(ch, mi.rcWork.bottom - mi.rcWork.top);
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        cw = smallest(cw, work.right - work.left);
        ch = smallest(ch, work.bottom - work.top);
    }
    cw = smallest(cw, GetSystemMetrics(SM_CXFULLSCREEN));
    ch = smallest(ch, GetSystemMetrics(SM_CYFULLSCREEN));
    if (cw * mh > ch * mw)                          /* the monitor's own shape (16:9 on the */
        cw = ch * mw / mh;                          /* tester's 4K screen) */
    else
        ch = cw * mh / mw;

    r.left = 0; r.top = 0; r.right = cw; r.bottom = ch;
    AdjustWindowRectEx(&r, style, FALSE, 0);
    SetWindowLongPtrW(w, GWL_STYLE, style);
    SetWindowPos(w, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER);
    g_sizing = 0;
}

static LRESULT CALLBACK game_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SIZE && !g_sizing)
        PostMessageW(w, WM_APP, 0, 0);              /* resized from outside: size it again */
    if (msg == WM_APP) {
        game_resize(w);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static void pump_for(DWORD ms)
{
    DWORD until = GetTickCount() + ms;
    MSG m;
    while (GetTickCount() < until) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
}

static int covers(HWND w)
{
    RECT r, c;
    LONG_PTR style = GetWindowLongPtrW(w, GWL_STYLE);
    GetWindowRect(w, &r);
    GetClientRect(w, &c);
    return !(style & (WS_CAPTION | WS_THICKFRAME)) && EqualRect(&r, &g_screen)
        && c.right == g_screen.right - g_screen.left && c.bottom == g_screen.bottom - g_screen.top;
}

int main(void)
{
    WNDCLASSW wc = {0};
    MONITORINFO mi = { sizeof mi };
    HWND w;
    char log[8192] = {0};
    FILE *f;
    int fails = 0, i, ok = 0;

    if (!LoadLibraryW(L"lid-borderless.dll")) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    wc.lpfnWndProc = game_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"LaunchUnrealUWindowsClient";
    RegisterClassW(&wc);
    w = CreateWindowExW(0, wc.lpszClassName, L"LET IT DIE", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        0, 0, 400, 300, NULL, NULL, wc.hInstance, NULL);
    if (!w) { printf("no window %lu\n", GetLastError()); return 2; }
    GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
    g_screen = mi.rcMonitor;
    {   /* the game imports these too: all four of its window calls are there to redirect */
        RECT r = { 0, 0, 1, 1 };
        AdjustWindowRect(&r, WS_POPUP, FALSE);
        SetWindowLongW(w, GWLP_USERDATA, 0);
    }
    game_resize(w);                                 /* the game applies the monitor's resolution */

    for (i = 0; i < 40 && !ok; i++) {
        pump_for(100);
        ok = covers(w);
    }
    if (!ok) { puts("FAIL: the game does not draw at the monitor's size, borderless"); fails++; }
    else puts("ok: the game draws at the monitor's size, borderless, covering the taskbar too");
    pump_for(2000);
    if (!covers(w)) { puts("FAIL: it did not stay that way"); fails++; }
    else puts("ok: and stays that way");

    f = fopen("lid-borderless.log", "rb");
    if (f) { fread(log, 1, sizeof log - 1, f); fclose(f); }
    printf("%s", log);
    if (!strstr(log, "whole screen (3 of 3")) { puts("FAIL: the game's screen calls were not all redirected"); fails++; }
    if (strstr(log, "will not draw") || strstr(log, "keeps setting")) { puts("FAIL: it had to fight the game"); fails++; }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
