/* ais-gui.c -- the Windows double-click launcher for the AIS web GUI.
 *
 * A GUI-subsystem program, so Explorer and a shortcut open no console. It runs
 * the ais.exe beside it as `ais --serve` with no window, waits for the port to
 * answer, opens the page in the default browser, and sits in the notification
 * area with Open and Quit. Quit ends the server, as closing the console does
 * on the other platforms. It is the Windows member of gui/ (gui/README.md) and
 * the target of the installer's shortcuts (installer/ais.iss).
 *
 *   ais-gui [PORT]    start the server on PORT (default 8765) and open the page
 *   ais-gui --quit    end a running launcher and its server (exit 1 if none)
 *
 * A second launch while one runs opens the page again and exits: the mutex
 * below is held by the first, and installer/ais.iss names it so Setup asks
 * for Quit before replacing the files. A server started by hand (`ais --serve`
 * in a terminal) is left alone: the page opens, nothing is started or owned.
 * AIS_NO_OPEN set in the environment keeps the browser closed, as it does for
 * `ais --serve` itself, so a test can drive the launcher headless.
 *
 * The server's stdout and stderr go to ais-serve.log in the user's temp
 * folder; when ais.exe exits before the port answers, that text is the error
 * shown. Win32 only, no toolkit, no heap: the launcher is meant to stay thin.
 *
 * Built by the Makefile here with MinGW-w64, cross-compiled from Linux like
 * ais.exe; scripts/dist.sh win puts it in the zip. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_DEFAULT  8765
#define CLASS_NAME    "AISGuiLauncher"
#define MUTEX_NAME    "ais-gui"         /* named in installer/ais.iss (AppMutex) */
#define QUIT_EVENT    "ais-gui-quit"    /* set by --quit; a named object, found
                                           where a hidden window may not be */
#define IDI_AIS       1                 /* the icon in ais-gui.rc */
#define WM_TRAY       (WM_APP + 1)
#define ID_OPEN       1
#define ID_QUIT       2
#define TIMER_CHILD   1
#define START_WAIT_MS 15000             /* the port must answer within this */

static int port = PORT_DEFAULT;
static char url[48];
static HANDLE child;                    /* ais.exe --serve, NULL once gone */
static char logpath[MAX_PATH];
static NOTIFYICONDATAA nid;

/* 1 when 127.0.0.1:port accepts a connection: a server is up. */
static int listening(void)
{
    SOCKET s;
    struct sockaddr_in a;
    int ok;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
        return 0;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((u_short)port);
    ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
    closesocket(s);
    return ok;
}

static void open_page(void)
{
    if (getenv("AIS_NO_OPEN") != NULL)
        return;
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

/* The last bytes of the server's log, for the error box. */
static void read_log_tail(char *buf, size_t n)
{
    HANDLE f;
    LARGE_INTEGER size, pos;
    DWORD got = 0;

    buf[0] = 0;
    f = CreateFileA(logpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    if (GetFileSizeEx(f, &size) && size.QuadPart > (LONGLONG)(n - 1)) {
        pos.QuadPart = size.QuadPart - (LONGLONG)(n - 1);
        SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
    }
    if (ReadFile(f, buf, (DWORD)(n - 1), &got, NULL))
        buf[got] = 0;
    CloseHandle(f);
}

/* A line of the launcher's own in the server's log, for a failure no one saw. */
static void note(const char *s)
{
    HANDLE f = CreateFileA(logpath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    char line[256];
    DWORD n;

    if (f == INVALID_HANDLE_VALUE)
        return;
    n = (DWORD)snprintf(line, sizeof line, "ais-gui: %s (error %lu)\r\n",
                        s, (unsigned long)GetLastError());
    WriteFile(f, line, n, &n, NULL);
    CloseHandle(f);
}

/* Start `ais.exe --serve PORT` from the launcher's own folder, windowless, and
 * wait for the port. 0 when the server answers; -1 with ERR filled otherwise. */
static int start_server(char *err, size_t errsz)
{
    char dir[MAX_PATH], exe[MAX_PATH + 16], cmd[MAX_PATH + 48], tail[1536];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HANDLE log;
    DWORD n, waited;
    char *slash;

    n = GetModuleFileNameA(NULL, dir, sizeof dir);
    if (n == 0 || n >= sizeof dir || (slash = strrchr(dir, '\\')) == NULL) {
        snprintf(err, errsz, "cannot find the folder ais-gui.exe runs from");
        return -1;
    }
    *slash = 0;
    /* cd here so a .ais beside the launcher is what the git-style walk finds,
     * as the .command launcher does on macOS (gui/README.md) */
    SetCurrentDirectoryA(dir);
    snprintf(exe, sizeof exe, "%s\\ais.exe", dir);
    snprintf(cmd, sizeof cmd, "\"%s\" --serve %d", exe, port);

    n = GetTempPathA(sizeof logpath, logpath);
    if (n == 0 || n + 16 >= sizeof logpath)
        strcpy(logpath, ".\\");
    strcat(logpath, "ais-serve.log");
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    log = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    /* the launcher opens the page itself once the port answers */
    SetEnvironmentVariableA("AIS_NO_OPEN", "1");
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError = log;
    }
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, dir, &si, &pi)) {
        DWORD e = GetLastError();
        if (log != INVALID_HANDLE_VALUE)
            CloseHandle(log);
        if (e == ERROR_FILE_NOT_FOUND)
            snprintf(err, errsz, "ais.exe is not beside ais-gui.exe:\n%s", exe);
        else
            snprintf(err, errsz, "cannot start %s (error %lu)", exe, (unsigned long)e);
        return -1;
    }
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
    CloseHandle(pi.hThread);
    child = pi.hProcess;

    for (waited = 0; waited < START_WAIT_MS; waited += 100) {
        if (listening())
            return 0;
        if (WaitForSingleObject(child, 100) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(child, &code);
            CloseHandle(child);
            child = NULL;
            read_log_tail(tail, sizeof tail);
            snprintf(err, errsz, "ais.exe --serve %d exited (code %lu).\n\n%s",
                     port, (unsigned long)code, tail[0] ? tail : "(no output)");
            return -1;
        }
    }
    TerminateProcess(child, 1);
    CloseHandle(child);
    child = NULL;
    snprintf(err, errsz, "ais.exe started but port %d did not answer in %d s",
             port, START_WAIT_MS / 1000);
    return -1;
}

/* End the server. As abrupt as closing the console it runs in elsewhere: every
 * write of the index is an atomic rename, and the index rebuilds from the store. */
static void stop_server(void)
{
    if (child == NULL)
        return;
    TerminateProcess(child, 0);
    WaitForSingleObject(child, 3000);
    CloseHandle(child);
    child = NULL;
}

static void show_menu(HWND h)
{
    HMENU m = CreatePopupMenu();
    POINT p;

    AppendMenuA(m, MF_STRING, ID_OPEN, "Open AIS");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, ID_QUIT, "Quit AIS");
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    GetCursorPos(&p);
    SetForegroundWindow(h);             /* so the menu closes on a click elsewhere */
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, h, NULL);
    PostMessageA(h, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_TRAY:
        if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_LBUTTONDBLCLK)
            open_page();
        else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU)
            show_menu(h);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == ID_OPEN)
            open_page();
        else if (LOWORD(w) == ID_QUIT)
            DestroyWindow(h);
        return 0;
    case WM_TIMER:                      /* the server died on its own: leave with it */
        if (child != NULL && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
            CloseHandle(child);
            child = NULL;
            DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE:                      /* --quit, and the session ending */
        DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w)
            stop_server();
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_CHILD);
        Shell_NotifyIconA(NIM_DELETE, &nid);
        stop_server();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WSADATA wsa;
    WNDCLASSA wc;
    HWND h;
    HANDLE quit;
    MSG m;
    char err[2048];

    (void)prev;
    (void)show;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (strstr(cmdline, "--quit") != NULL) {
        quit = OpenEventA(EVENT_MODIFY_STATE, FALSE, QUIT_EVENT);
        if (quit == NULL)
            return 1;
        SetEvent(quit);
        return 0;
    }
    if (cmdline[0] >= '1' && cmdline[0] <= '9') {
        long p = strtol(cmdline, NULL, 10);
        if (p < 1 || p > 65535) {
            MessageBoxA(NULL, "PORT must be 1..65535", "AIS", MB_ICONERROR);
            return 2;
        }
        port = (int)p;
    }
    snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);

    /* a second double-click, or a server someone started by hand: the page */
    CreateMutexA(NULL, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS || listening()) {
        open_page();
        return 0;
    }

    if (start_server(err, sizeof err) != 0) {
        MessageBoxA(NULL, err, "AIS could not start", MB_ICONERROR);
        return 1;
    }

    quit = CreateEventA(NULL, TRUE, FALSE, QUIT_EVENT);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassA(&wc))
        note("RegisterClass failed");
    h = CreateWindowExA(0, CLASS_NAME, "AIS", 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (h == NULL) {
        note("CreateWindow failed; stopping the server");
        stop_server();
        return 1;
    }

    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = h;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(IDI_AIS), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    snprintf(nid.szTip, sizeof nid.szTip, "AIS  %s", url);
    if (!Shell_NotifyIconA(NIM_ADD, &nid))
        note("no notification area icon (no shell here?)");

    open_page();
    SetTimer(h, TIMER_CHILD, 2000, NULL);
    /* the message loop, which also wakes when --quit sets the event */
    for (;;) {
        DWORD n = quit != NULL ? 1 : 0;
        if (MsgWaitForMultipleObjects(n, &quit, FALSE, INFINITE, QS_ALLINPUT)
                == WAIT_OBJECT_0 && n == 1) {
            ResetEvent(quit);
            DestroyWindow(h);           /* WM_DESTROY stops the server, posts WM_QUIT */
        }
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                goto done;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
done:
    stop_server();                      /* no launcher, no server: nothing is left behind */
    return 0;
}
