/* Some program other than the game loads the plugin: its windows must be left
 * exactly as they are, and the plugin must write nothing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int main(void)
{
    WNDCLASSW wc = {0};
    HWND w;
    DWORD until;
    MSG m;
    int fails = 0;

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SomeOtherProgram";
    RegisterClassW(&wc);
    w = CreateWindowExW(0, wc.lpszClassName, L"Not the game", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        100, 100, 640, 480, NULL, NULL, wc.hInstance, NULL);
    if (!w || !LoadLibraryW(L"lid-borderless.dll")) { printf("setup failed %lu\n", GetLastError()); return 2; }
    for (until = GetTickCount() + 1500; GetTickCount() < until; Sleep(10))
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&m);
    if (!(GetWindowLongPtrW(w, GWL_STYLE) & WS_CAPTION)) { puts("FAIL: another program's window was changed"); fails++; }
    if (GetFileAttributesW(L"lid-borderless.log") != INVALID_FILE_ATTRIBUTES) { puts("FAIL: it wrote a log"); fails++; }
    printf(fails ? "\n%d FAILED\n" : "ok: another program is left alone\n\nall passed\n", fails);
    return fails != 0;
}
