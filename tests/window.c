/* Stand-in for BrgGame-Steam.exe: an ordinary window with a title bar and
 * edges, as the game's Windowed mode makes. Once the plugin is loaded:
 *   - the window loses its frame but keeps the size the game draws at, in the
 *     middle of the monitor (the game holds its window to its resolution);
 *   - at nearly the monitor's size (a framed window at the monitor's
 *     resolution, trimmed by Windows - 2490x1401 on 2560x1440 in the game) it
 *     covers the monitor exactly;
 *   - when "the game" sets it up again it goes borderless again;
 *   - once borderless it is left alone;
 *   - a game that will not draw at the monitor's size gets its own size;
 *   - a game that undoes every change at once is left be, not fought - the
 *     window must not flash between the two (what 0.1.0 did in the game). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define WM_PUT_FRAME_BACK (WM_APP + 1)
#define WM_PUT_SIZE_BACK  (WM_APP + 2)

static HWND g_window;
static int g_fights_back;      /* undo every change, as a stubborn game would */
static int g_undone;
static LONG g_keep_w, g_keep_h; /* a game that puts this client size back whenever it changes */

static LRESULT CALLBACK game_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_WINDOWPOSCHANGED && g_fights_back && !(GetWindowLongPtrW(w, GWL_STYLE) & WS_CAPTION))
        PostMessageW(w, WM_PUT_FRAME_BACK, 0, 0);
    if (msg == WM_WINDOWPOSCHANGED && g_keep_w) {
        RECT c;
        GetClientRect(w, &c);
        if (c.right != g_keep_w || c.bottom != g_keep_h)
            PostMessageW(w, WM_PUT_SIZE_BACK, 0, 0);
    }
    if (msg == WM_PUT_SIZE_BACK) {
        RECT r = { 0, 0, g_keep_w, g_keep_h };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(w, GWL_STYLE), FALSE, 0);
        SetWindowPos(w, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        g_undone++;
        return 0;
    }
    if (msg == WM_PUT_FRAME_BACK) {
        /* straight to user32, past the plugin's redirect of the game's own
         * calls: a game putting its frame back some other way */
        typedef LONG_PTR (WINAPI *set_fn)(HWND, int, LONG_PTR);
        set_fn set = (set_fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowLongPtrW");
        set(w, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(w, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        g_undone++;
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

static RECT monitor_rect(void)
{
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}

/* Borderless, drawing at cw x ch, in the middle of the monitor (or at its corner when as big). */
static int borderless_at(LONG cw, LONG ch)
{
    RECT r, c, m = monitor_rect();
    LONG mw = m.right - m.left, mh = m.bottom - m.top;
    LONG left = m.left + (cw < mw ? (mw - cw) / 2 : 0), top = m.top + (ch < mh ? (mh - ch) / 2 : 0);
    LONG_PTR style = GetWindowLongPtrW(g_window, GWL_STYLE);
    GetWindowRect(g_window, &r);
    GetClientRect(g_window, &c);
    return !(style & (WS_CAPTION | WS_THICKFRAME)) && c.right == cw && c.bottom == ch
        && r.left == left && r.top == top && r.right - r.left == cw && r.bottom - r.top == ch;
}

static int wait_borderless_at(LONG cw, LONG ch)
{
    int i;
    for (i = 0; i < 40; i++) {      /* up to 4 seconds */
        if (borderless_at(cw, ch))
            return 1;
        pump_for(100);
    }
    return 0;
}

/* The window as the game's Windowed mode makes it: framed, drawing at cw x ch. */
static void game_sets_up(LONG cw, LONG ch)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    SetWindowLongPtrW(g_window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPos(g_window, NULL, 50, 50, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOZORDER);
    pump_for(50);
}

static long log_size(void)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    return GetFileAttributesExW(L"lid-borderless.log", GetFileExInfoStandard, &a) ? (long)a.nFileSizeLow : -1;
}

int main(void)
{
    WNDCLASSW wc = {0};
    char log[8192] = {0};
    FILE *f;
    int fails = 0;
    long before;
    RECT m;
    int undone;

    wc.lpfnWndProc = game_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"LaunchUnrealUWindowsClient";
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, L"LET IT DIE", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               100, 100, 200, 200, NULL, NULL, wc.hInstance, NULL);
    if (!g_window) { printf("no window %lu\n", GetLastError()); return 2; }
    game_sets_up(640, 480);
    m = monitor_rect();

    if (!LoadLibraryW(L"lid-borderless.dll")) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    if (!wait_borderless_at(640, 480)) { puts("FAIL: not borderless at the game's own size"); fails++; }
    else puts("ok: borderless at the game's own size, in the middle of the monitor");

    game_sets_up(800, 600);         /* a resolution change */
    if (!wait_borderless_at(800, 600)) { puts("FAIL: not borderless again after the game set it up again"); fails++; }
    else puts("ok: borderless again after the game set it up again, at its new size");

    /* the monitor's own resolution, trimmed by Windows as the game's was */
    game_sets_up((m.right - m.left) * 97 / 100, (m.bottom - m.top) * 97 / 100);
    if (!wait_borderless_at(m.right - m.left, m.bottom - m.top)) { puts("FAIL: does not cover the monitor"); fails++; }
    else puts("ok: at nearly the monitor's size it covers the monitor");

    pump_for(500);
    before = log_size();
    pump_for(1500);
    if (log_size() != before) { puts("FAIL: the plugin kept changing a window already borderless"); fails++; }
    else puts("ok: left alone once borderless");

    /* a game that will not draw at the monitor's size: borderless at its own */
    g_keep_w = (m.right - m.left) * 95 / 100;
    g_keep_h = (m.bottom - m.top) * 95 / 100;
    game_sets_up(g_keep_w, g_keep_h);
    if (!wait_borderless_at(g_keep_w, g_keep_h)) { puts("FAIL: a game keeping its own size was fought"); fails++; }
    else puts("ok: a game that will not draw at the monitor's size is borderless at its own");
    g_keep_w = g_keep_h = 0;

    /* a game that undoes each change at once: no flashing between the two */
    g_fights_back = 1;
    game_sets_up(640, 480);
    pump_for(6000);
    undone = g_undone;
    pump_for(3000);
    if (g_undone - undone > 1) { printf("FAIL: still fighting the game (%d more changes)\n", g_undone - undone); fails++; }
    else printf("ok: backed off from a game undoing every change (%d undone in all)\n", g_undone);

    f = fopen("lid-borderless.log", "rb");
    if (f) { fread(log, 1, sizeof log - 1, f); fclose(f); }
    printf("%s", log);
    if (!strstr(log, "loaded") || !strstr(log, "the game had set it up again")
            || !strstr(log, "keeps setting its window up again")
            || !strstr(log, "will not draw at the monitor's size")) {
        puts("FAIL: the log does not say what was done");
        fails++;
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
