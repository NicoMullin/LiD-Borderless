/*
 * lid-borderless.dll for Let It Die - a plugin for the LiD Plugin Loader that
 * shows the game borderless: the game runs in its own Windowed mode, and its
 * window loses the title bar and edges. With the game's resolution set to the
 * monitor's, that is borderless fullscreen: it looks like fullscreen, but
 * Alt+Tab is instant and other windows and monitors work as they do on the
 * desktop. At a lower resolution the borderless window sits in the middle of
 * the monitor.
 *
 * The window keeps exactly the size the game draws at. The game holds its
 * window to its resolution: stretched to the monitor, it puts its own size and
 * frame straight back, and the two would flash between each other (0.1.0 did
 * that). So only the frame goes, and the window is placed on the monitor.
 *
 * The game sets its window up again whenever the resolution or display mode
 * changes, so a thread of the plugin's own looks at the window four times a
 * second and makes it borderless again when it has a frame again. Should the
 * game undo every change at once anyway, the plugin leaves the window as the
 * game wants it for a while instead of fighting it. In the game's Fullscreen
 * mode the window already covers the monitor with no edges, so there is
 * nothing to do and the plugin leaves it alone.
 *
 * The Borderless Fullscreen mod in LET IT DIE DB Mod Manager switches the game
 * to Windowed (mbFullScreen=False in BrgGraphicsConfig.ini) and this plugin on
 * together; unticked, both go back.
 *
 * Safety rules:
 *   - It only does anything inside BrgGame-Steam.exe. Loaded by any other
 *     program, it changes nothing.
 *   - Only the game's own top-level windows are touched, and only their style
 *     and position. The window is moved with SWP_ASYNCWINDOWPOS, so the plugin
 *     never waits on the game.
 *   - In the game, seven of its calls to Windows are pointed at the plugin (in
 *     its import table, not its code): AdjustWindowRect(Ex) answer "no frame",
 *     SetWindowLong(Ptr) leave the title bar and edges off its top-level
 *     windows, and GetMonitorInfoW, SystemParametersInfoW and GetSystemMetrics
 *     answer that the taskbar leaves it the whole screen (each passes every
 *     other question straight to Windows). Nothing else in the game is changed.
 *   - No network, no registry, no files but its own log, lid-borderless.log,
 *     next to itself.
 *   - Delete lid-borderless.dll (or untick the mod) and the game is exactly
 *     stock again.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "version.h"

#define LOOK_EVERY_MS   250
#define LOG_AT_MOST     20      /* re-applies logged, so a fight with the game cannot fill the disk */
#define FIGHT_LIMIT     6       /* this many changes ...                                   */
#define FIGHT_WINDOW_MS 5000    /* ... within this long means the game is undoing each one: */
#define BACK_OFF_MS     30000   /* leave the window as the game wants it for this long       */
#define FILL_TRIES      3       /* times stretched to the monitor and shrunk back: give up on it */

/* The parts of a window's frame a borderless window does without */
#define FRAME_STYLE    (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)
#define FRAME_EX_STYLE (WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE)

/* ------------------------------------------------------------------ log */

static wchar_t g_log_path[MAX_PATH];
static int g_log_started;

static void log_line(const char *fmt, ...)
{
    char msg[512];
    SYSTEMTIME t;
    va_list ap;
    int n;
    HANDLE f;
    DWORD written;

    if (!g_log_path[0])
        return;
    GetLocalTime(&t);
    n = snprintf(msg, sizeof msg, "%02u:%02u:%02u  ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    n += vsnprintf(msg + n, sizeof msg - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof msg - 3)
        n = (int)sizeof msg - 3;
    msg[n++] = '\r';
    msg[n++] = '\n';

    /* a fresh log each time the game starts */
    f = CreateFileW(g_log_path, g_log_started ? FILE_APPEND_DATA : GENERIC_WRITE,
                    FILE_SHARE_READ, NULL, g_log_started ? OPEN_ALWAYS : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    g_log_started = 1;
    WriteFile(f, msg, (DWORD)n, &written, NULL);
    CloseHandle(f);
}

/* ------------------------------------------------------- the game's window */

struct search {
    DWORD pid;
    HWND best;
    LONG best_area;
};

static BOOL CALLBACK consider(HWND w, LPARAM data)
{
    struct search *s = (struct search *)data;
    DWORD pid = 0;
    RECT r;
    LONG area;

    GetWindowThreadProcessId(w, &pid);
    if (pid != s->pid || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) != NULL)
        return TRUE;
    if (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD)
        return TRUE;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    if (!GetWindowRect(w, &r))
        return TRUE;
    area = (r.right - r.left) * (r.bottom - r.top);
    if (area > s->best_area) {      /* the game's main window is its biggest */
        s->best = w;
        s->best_area = area;
    }
    return TRUE;
}

static HWND game_window(void)
{
    struct search s = { GetCurrentProcessId(), NULL, 0 };
    EnumWindows(consider, (LPARAM)&s);
    return s.best;
}

/* Filling the monitor is tried while this is set; a game that will not draw at
 * the monitor's size (it puts its own size back each time) clears it. */
static int g_fill = 1;

/* Where the borderless window goes, and whether that is bigger than the game
 * made it (1) or exactly its own size (0).
 *
 * The size is the one the game draws at - its client area - in the middle of
 * the monitor: the game holds its window to its resolution and puts its own
 * size back if the window is stretched. Except when that is nearly the whole
 * monitor: set to the monitor's own resolution, the game's framed window does
 * not fit on the screen and Windows trims it (2560x1440 came out 2490x1401),
 * so a game drawing at 90% of the monitor or more each way gets all of it. */
static int target_rect(HWND w, const RECT *monitor, RECT *out)
{
    RECT client;
    LONG cw, ch, mw = monitor->right - monitor->left, mh = monitor->bottom - monitor->top;
    int bigger = 0;

    GetClientRect(w, &client);
    cw = client.right - client.left;
    ch = client.bottom - client.top;
    if (g_fill && cw * 10 >= mw * 9 && ch * 10 >= mh * 9 && cw <= mw && ch <= mh) {
        bigger = cw != mw || ch != mh;
        cw = mw;
        ch = mh;
    }
    out->left = monitor->left + (cw < mw ? (mw - cw) / 2 : 0);
    out->top = monitor->top + (ch < mh ? (mh - ch) / 2 : 0);
    out->right = out->left + cw;
    out->bottom = out->top + ch;
    return bigger;
}

/* What a window needs changing to be borderless; 0 when nothing. */
static int needs_borderless(HWND w, LONG_PTR *style, LONG_PTR *ex, RECT *target, RECT *monitor, int *bigger)
{
    MONITORINFO mi;
    RECT now;

    if (IsIconic(w))
        return 0;                   /* minimised: leave it be until it is back */
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi) || !GetWindowRect(w, &now))
        return 0;
    *monitor = mi.rcMonitor;
    *style = GetWindowLongPtrW(w, GWL_STYLE);
    *ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
    *bigger = target_rect(w, monitor, target);
    if (target->right <= target->left || target->bottom <= target->top)
        return 0;                   /* no drawing area (yet) */
    return (*style & FRAME_STYLE) || (*ex & FRAME_EX_STYLE) || !EqualRect(&now, target);
}

static void make_borderless(HWND w, LONG_PTR style, LONG_PTR ex, const RECT *target)
{
    SetWindowLongPtrW(w, GWL_STYLE, (style & ~(LONG_PTR)FRAME_STYLE) | WS_POPUP);
    SetWindowLongPtrW(w, GWL_EXSTYLE, ex & ~(LONG_PTR)FRAME_EX_STYLE);
    SetWindowPos(w, NULL, target->left, target->top,
                 target->right - target->left, target->bottom - target->top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

static DWORD WINAPI watch(LPVOID unused)
{
    HWND seen = NULL;
    int applied = 0;
    DWORD recent[FIGHT_LIMIT] = {0};    /* when the last few changes were made */
    int next = 0;
    DWORD resting_until = 0;
    DWORD filled[FILL_TRIES] = {0};     /* when the window was last stretched to the monitor */
    int next_fill = 0;

    (void)unused;
    for (;;) {
        HWND w = game_window();
        LONG_PTR style, ex;
        RECT target, monitor;
        DWORD now = GetTickCount();
        int bigger = 0;

        if (w && w != seen) {
            seen = w;
            log_line("found the game's window");
        }
        if (resting_until && (LONG)(now - resting_until) < 0) {
            Sleep(LOOK_EVERY_MS);
            continue;
        }
        resting_until = 0;
        if (w && needs_borderless(w, &style, &ex, &target, &monitor, &bigger)) {
            /* Stretched to the monitor and shrunk back by the game, again and
             * again: it will not draw at that size, so keep to its own. */
            if (bigger && filled[next_fill] && now - filled[next_fill] < FIGHT_WINDOW_MS) {
                g_fill = 0;
                log_line("the game will not draw at the monitor's size - borderless at its own size instead");
                memset(recent, 0, sizeof recent);
                Sleep(LOOK_EVERY_MS);
                continue;
            }
            if (bigger) {
                filled[next_fill] = now ? now : 1;
                next_fill = (next_fill + 1) % FILL_TRIES;
            }
            /* The game undoing every change straight away would make the
             * window flash between the two: never keep that up. */
            if (recent[next] && now - recent[next] < FIGHT_WINDOW_MS) {
                log_line("the game keeps setting its window up again - left as it is for %d seconds",
                         BACK_OFF_MS / 1000);
                memset(recent, 0, sizeof recent);
                resting_until = now + BACK_OFF_MS;
                if (!resting_until)
                    resting_until = 1;
                Sleep(LOOK_EVERY_MS);
                continue;
            }
            recent[next] = now ? now : 1;
            next = (next + 1) % FIGHT_LIMIT;
            make_borderless(w, style, ex, &target);
            if (applied < LOG_AT_MOST)
                log_line("borderless: %ldx%ld at %ld,%ld on a %ldx%ld monitor%s",
                         target.right - target.left, target.bottom - target.top, target.left, target.top,
                         monitor.right - monitor.left, monitor.bottom - monitor.top,
                         applied ? " (the game had set it up again)" : "");
            else if (applied == LOG_AT_MOST)
                log_line("... and again - further times are not logged");
            applied++;
        }
        Sleep(LOOK_EVERY_MS);
    }
    return 0;
}

/* --------------------------------------- the game's own window sizing */
/*
 * Windowed, the game works its window out itself: the size it draws at, plus
 * the title bar and edges Windows says that needs (AdjustWindowRect), cut down
 * to fit the screen. At the monitor's own resolution the frame does not fit,
 * so the game draws smaller (2490x1401 on 2560x1440) and puts that size back
 * whenever the window is stretched. So the game is told the truth about the
 * window it will have: no frame. Its own sums then come to the monitor's size,
 * and it never asks for a frame again. Only the game's calls are changed (its
 * import table), and only for its top-level windows.
 */

typedef BOOL (WINAPI *adjust_fn)(LPRECT, DWORD, BOOL);
typedef BOOL (WINAPI *adjust_ex_fn)(LPRECT, DWORD, BOOL, DWORD);
typedef LONG (WINAPI *set_long_fn)(HWND, int, LONG);
typedef LONG_PTR (WINAPI *set_long_ptr_fn)(HWND, int, LONG_PTR);

static set_long_fn g_set_long;
static set_long_ptr_fn g_set_long_ptr;

static BOOL WINAPI no_frame(LPRECT r, DWORD style, BOOL menu)
{
    (void)style; (void)menu;
    return r != NULL;               /* a borderless window is its client area */
}

static BOOL WINAPI no_frame_ex(LPRECT r, DWORD style, BOOL menu, DWORD ex)
{
    (void)style; (void)menu; (void)ex;
    return r != NULL;
}

static int top_level(HWND w)
{
    return w && !(GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD) && GetParent(w) == NULL;
}

static LONG_PTR without_frame(HWND w, int index, LONG_PTR value)
{
    if (!top_level(w) || (value & WS_CHILD && index == GWL_STYLE))
        return value;
    if (index == GWL_STYLE && (value & FRAME_STYLE))
        return (value & ~(LONG_PTR)FRAME_STYLE) | WS_POPUP;
    if (index == GWL_EXSTYLE)
        return value & ~(LONG_PTR)FRAME_EX_STYLE;
    return value;
}

static LONG WINAPI set_long(HWND w, int index, LONG value)
{
    return g_set_long(w, index, (LONG)without_frame(w, index, value));
}

static LONG_PTR WINAPI set_long_ptr(HWND w, int index, LONG_PTR value)
{
    return g_set_long_ptr(w, index, without_frame(w, index, value));
}

/* Point the game's import of user32!name at ``to``; the old address, or NULL. */
static void *redirect(unsigned char *base, const char *name, void *to)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *d;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE || !dir.VirtualAddress)
        return NULL;
    for (d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        IMAGE_THUNK_DATA64 *names, *slots;
        if (_stricmp((const char *)(base + d->Name), "user32.dll") != 0 || !d->OriginalFirstThunk)
            continue;
        names = (IMAGE_THUNK_DATA64 *)(base + d->OriginalFirstThunk);
        slots = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            IMAGE_IMPORT_BY_NAME *by;
            DWORD old;
            void *was;
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                continue;
            by = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)by->Name, name) != 0)
                continue;
            if (!VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, PAGE_READWRITE, &old))
                return NULL;
            was = (void *)(ULONG_PTR)slots->u1.Function;
            slots->u1.Function = (ULONGLONG)(ULONG_PTR)to;
            VirtualProtect(&slots->u1.Function, sizeof slots->u1.Function, old, &old);
            return was;
        }
    }
    return NULL;
}

/*
 * The game also keeps its window inside the screen's work area - the monitor
 * less the taskbar - and keeps 16:9 while it does: on a 3840x2160 monitor with
 * the taskbar 72 pixels high (150% scaling) it drew at 3712x2088, and put that
 * back each time the window was stretched, leaving a taskbar-wide gap all
 * round (0.1.1). A borderless fullscreen window covers the taskbar anyway, so
 * the game is told the work area is the whole monitor, by each of the three
 * calls it can ask through.
 */
typedef BOOL (WINAPI *monitor_info_fn)(HMONITOR, LPMONITORINFO);
typedef BOOL (WINAPI *spi_fn)(UINT, UINT, PVOID, UINT);
typedef int (WINAPI *metrics_fn)(int);

static monitor_info_fn g_monitor_info;
static spi_fn g_spi;
static metrics_fn g_metrics;

static BOOL WINAPI whole_monitor_info(HMONITOR m, LPMONITORINFO mi)
{
    BOOL ok = g_monitor_info(m, mi);

    if (ok && mi && mi->cbSize >= sizeof(MONITORINFO))
        mi->rcWork = mi->rcMonitor;
    return ok;
}

static BOOL WINAPI whole_screen_parameters(UINT action, UINT ui, PVOID pv, UINT win_ini)
{
    BOOL ok = g_spi(action, ui, pv, win_ini);

    if (ok && action == SPI_GETWORKAREA && pv) {
        RECT *r = (RECT *)pv;       /* the main monitor's, which starts at 0,0 */
        r->left = 0;
        r->top = 0;
        r->right = g_metrics(SM_CXSCREEN);
        r->bottom = g_metrics(SM_CYSCREEN);
    }
    return ok;
}

static int WINAPI whole_screen_metrics(int index)
{
    switch (index) {
    case SM_CXFULLSCREEN:           /* the work area, less a title bar */
    case SM_CXMAXIMIZED:            /* a maximised window: the work area and its frame */
        return g_metrics(SM_CXSCREEN);
    case SM_CYFULLSCREEN:
    case SM_CYMAXIMIZED:
        return g_metrics(SM_CYSCREEN);
    }
    return g_metrics(index);
}

static void redirect_window_sizing(void)
{
    unsigned char *game = (unsigned char *)GetModuleHandleW(NULL);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    int done = 0;

    g_set_long = (set_long_fn)GetProcAddress(user32, "SetWindowLongW");
    g_set_long_ptr = (set_long_ptr_fn)GetProcAddress(user32, "SetWindowLongPtrW");
    if (!g_set_long || !g_set_long_ptr) {
        log_line("user32 has no SetWindowLong - the game's own window sizing is left as it is");
        return;
    }
    done += redirect(game, "AdjustWindowRect", (void *)no_frame) != NULL;
    done += redirect(game, "AdjustWindowRectEx", (void *)no_frame_ex) != NULL;
    done += redirect(game, "SetWindowLongW", (void *)set_long) != NULL;
    done += redirect(game, "SetWindowLongPtrW", (void *)set_long_ptr) != NULL;
    log_line("the game is told its window has no frame (%d of 4 of its window calls)", done);

    g_monitor_info = (monitor_info_fn)GetProcAddress(user32, "GetMonitorInfoW");
    g_spi = (spi_fn)GetProcAddress(user32, "SystemParametersInfoW");
    g_metrics = (metrics_fn)GetProcAddress(user32, "GetSystemMetrics");
    if (!g_monitor_info || !g_spi || !g_metrics) {
        log_line("user32 is missing a screen call - the game keeps its window clear of the taskbar");
        return;
    }
    done = 0;
    done += redirect(game, "GetMonitorInfoW", (void *)whole_monitor_info) != NULL;
    done += redirect(game, "SystemParametersInfoW", (void *)whole_screen_parameters) != NULL;
    done += redirect(game, "GetSystemMetrics", (void *)whole_screen_metrics) != NULL;
    log_line("the game is told the taskbar leaves it the whole screen (%d of 3 of its screen calls)", done);
}

/* ------------------------------------------------------------ start */

static int running_in_the_game(void)
{
    wchar_t exe[MAX_PATH];
    const wchar_t *name;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    return _wcsicmp(name, L"BrgGame-Steam.exe") == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t *slash;
        DWORD n;
        HANDLE thread;

        DisableThreadLibraryCalls(inst);
        n = GetModuleFileNameW(inst, g_log_path, MAX_PATH);
        slash = (n && n < MAX_PATH - 32) ? wcsrchr(g_log_path, L'\\') : NULL;
        if (slash)
            wcscpy(slash + 1, L"lid-borderless.log");
        else
            g_log_path[0] = 0;

        if (!running_in_the_game())
            return TRUE;    /* some other program - change nothing */
        log_line("LiD Borderless lid-borderless.dll " LID_DLL_VERSION " loaded");
        redirect_window_sizing();
        thread = CreateThread(NULL, 0, watch, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
        else
            log_line("could not start its thread (%lu) - nothing changed", GetLastError());
    }
    return TRUE;
}
