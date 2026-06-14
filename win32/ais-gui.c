/* ais-gui.c -- native Win32 GUI for AIS. ISOLATED wrapper: the core engine in
 * c/ stays pure ANSI C and never sees <windows.h>; this front-end only calls the
 * public FFI seam (embed.h) plus the index-resolution policy (locate.h), exactly
 * as serve.c / the Flutter app do. Pure C, no C++, no .NET, no Cygwin -- it links
 * only user32/gdi32/comctl32, present on every Windows (XP -> 11), so the exe is
 * tiny and self-contained. Build: see win32/Makefile (MinGW cross-compile).
 *
 * UI: a keys box + Recall (with an OR toggle) over a results list (double-click
 * an http(s) value to open it), and a value+keys row with Add. One window, the
 * common controls only -- no resource (.rc) file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

#include "embed.h"
#include "locate.h"

enum { ID_KEYS = 1001, ID_RECALL, ID_OR, ID_LIST, ID_VALUE, ID_VKEYS, ID_ADD };

static void *g_ais;                 /* engine handle (ais_embed_open) */
static HWND  g_keys, g_or, g_list, g_value, g_vkeys;
static HFONT g_font;

/* GetWindowText into a freshly malloc'd buffer (caller frees); "" on failure. */
static char *get_text(HWND h)
{
    int n = GetWindowTextLengthA(h);
    char *s = (char *)malloc((size_t)n + 1);
    if (s == NULL)
        return NULL;
    GetWindowTextA(h, s, n + 1);
    return s;
}

static HWND mk(HWND parent, const char *cls, const char *text, DWORD style, int id)
{
    HWND h = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                             GetModuleHandle(NULL), NULL);
    if (h != NULL)
        SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static void do_recall(void)
{
    char *keys = get_text(g_keys);
    int or_mode = (SendMessage(g_or, BM_GETCHECK, 0, 0) == BST_CHECKED);

    SendMessage(g_list, LB_RESETCONTENT, 0, 0);
    if (g_ais != NULL && keys != NULL && keys[0] != '\0') {
        char *res = ais_embed_recall(g_ais, keys, or_mode);   /* "id|value\n"... */
        if (res != NULL) {
            char *line = res;
            while (line != NULL && *line != '\0') {
                char *nl = strchr(line, '\n');
                char *bar;
                if (nl != NULL)
                    *nl = '\0';
                bar = strchr(line, '|');             /* drop the leading "id|" */
                {
                    const char *val = (bar != NULL) ? bar + 1 : line;
                    if (*val != '\0')
                        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)val);
                }
                if (nl == NULL)
                    break;
                line = nl + 1;
            }
            ais_embed_free(res);
        }
    }
    free(keys);
}

static void do_add(void)
{
    char *val = get_text(g_value);
    char *vk  = get_text(g_vkeys);

    if (g_ais != NULL && val != NULL && val[0] != '\0') {
        ais_embed_store(g_ais, (vk != NULL) ? vk : "", val);
        SetWindowTextA(g_value, "");
        if (vk != NULL && vk[0] != '\0')                 /* show what was added */
            SetWindowTextA(g_keys, vk);
        do_recall();
    }
    free(val);
    free(vk);
}

/* Double-click a result: if the value is an http(s) link, open it. */
static void open_selected(void)
{
    LRESULT i = SendMessage(g_list, LB_GETCURSEL, 0, 0);
    int n;
    char *s;
    if (i == LB_ERR)
        return;
    n = (int)SendMessage(g_list, LB_GETTEXTLEN, (WPARAM)i, 0);
    s = (char *)malloc((size_t)n + 1);
    if (s == NULL)
        return;
    SendMessageA(g_list, LB_GETTEXT, (WPARAM)i, (LPARAM)s);
    if (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0)
        ShellExecuteA(NULL, "open", s, NULL, NULL, SW_SHOWNORMAL);
    free(s);
}

static void layout(HWND hwnd)
{
    RECT r;
    int w, pad = 8, bh = 26, btn = 88, orw = 56;
    int row3;
    GetClientRect(hwnd, &r);
    w = r.right;
    row3 = r.bottom - pad - bh;

    /* row 1: keys edit | Recall | OR */
    MoveWindow(g_keys,   pad, pad, w - pad * 4 - btn - orw, bh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_RECALL), w - pad * 2 - btn - orw, pad, btn, bh, TRUE);
    MoveWindow(g_or,     w - pad - orw, pad, orw, bh, TRUE);

    /* middle: results list fills the gap between row 1 and row 3 */
    MoveWindow(g_list, pad, pad * 2 + bh, w - pad * 2,
               row3 - (pad * 3 + bh), TRUE);

    /* row 3: value edit | keys edit | Add */
    MoveWindow(g_value, pad, row3, (w - pad * 4 - btn) * 3 / 5, bh, TRUE);
    MoveWindow(g_vkeys, pad * 2 + (w - pad * 4 - btn) * 3 / 5, row3,
               (w - pad * 4 - btn) * 2 / 5, bh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_ADD), w - pad - btn, row3, btn, bh, TRUE);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        char dir[4096];
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_keys  = mk(hwnd, "EDIT",   "", WS_BORDER | ES_AUTOHSCROLL, ID_KEYS);
        mk(hwnd, "BUTTON", "Recall", BS_DEFPUSHBUTTON, ID_RECALL);
        g_or    = mk(hwnd, "BUTTON", "OR", BS_AUTOCHECKBOX, ID_OR);
        g_list  = mk(hwnd, "LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, ID_LIST);
        g_value = mk(hwnd, "EDIT",   "", WS_BORDER | ES_AUTOHSCROLL, ID_VALUE);
        g_vkeys = mk(hwnd, "EDIT",   "", WS_BORDER | ES_AUTOHSCROLL, ID_VKEYS);
        mk(hwnd, "BUTTON", "Add", BS_PUSHBUTTON, ID_ADD);
        SendMessageA(g_value, EM_SETCUEBANNER, TRUE, (LPARAM)L"");   /* no-op cue */
        if (ais_locate(NULL, dir, sizeof dir) == 0)
            g_ais = ais_embed_open(dir);
        if (g_ais == NULL)
            MessageBoxA(hwnd, "Could not open the AIS index.", "AIS", MB_ICONWARNING);
        return 0;
    }
    case WM_SIZE:
        layout(hwnd);
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_keys);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_RECALL: do_recall(); return 0;
        case ID_ADD:    do_add();    return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_DBLCLK)
                open_selected();
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if (g_ais != NULL)
            ais_embed_close(g_ais);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    HACCEL acc;
    ACCEL a = { FVIRTKEY, VK_RETURN, ID_RECALL };   /* Enter = Recall */

    (void)prev; (void)cmd;
    InitCommonControls();

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "AisMainWnd";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassA(&wc))
        return 1;

    hwnd = CreateWindowA("AisMainWnd", "AIS", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                         NULL, NULL, inst, NULL);
    if (hwnd == NULL)
        return 1;
    ShowWindow(hwnd, show);

    acc = CreateAcceleratorTable(&a, 1);
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAccelerator(hwnd, acc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}
