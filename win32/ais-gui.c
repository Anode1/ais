/* ais-gui.c -- native Win32 GUI for AIS. ISOLATED wrapper: the core engine in
 * c/ stays pure ANSI C and never sees <windows.h>; this front-end only calls the
 * public FFI seam (embed.h) plus the index-resolution policy (locate.h), exactly
 * as serve.c / the Flutter app do. Pure C, no C++, no .NET, no Cygwin -- it links
 * only user32/gdi32/comctl32, present on every Windows (XP -> 11), so the exe is
 * tiny and self-contained. Build: see win32/Makefile (MinGW cross-compile).
 *
 * UI: a labelled keys box + Get (with an OR toggle) over a results list
 * (double-click an http(s) value to open it), and a labelled value+keys row with
 * Add. One window, common controls only -- no resource (.rc) file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embed.h"
#include "locate.h"

enum { ID_KEYS = 1001, ID_GET, ID_OR, ID_LIST, ID_VALUE, ID_VKEYS, ID_ADD };
/* context-menu command ids for a result row (kept clear of the control ids) */
enum { ID_EDIT = 1101, ID_DELETE };

static void *g_ais;                 /* engine handle (ais_embed_open) */
static HWND  g_keys, g_or, g_list, g_value, g_vkeys;
static HWND  g_lkeys, g_lval, g_lvk;   /* inline field labels: Keys:/Value:/Keys: */
static HFONT g_font;
static int   g_dpi = 96;            /* system DPI; the layout scales from 96 */

/* scale a value expressed in 96-DPI ("logical") pixels to the actual DPI */
static int dp(int px) { return MulDiv(px, g_dpi, 96); }

/* ---- error log -----------------------------------------------------------
 * Written ONLY on error and created lazily, so the file's mere existence means
 * the app hit a real error -- not a user mistake ("send me the log if it
 * exists"). It lives at %LOCALAPPDATA%\AIS\ais-error.log: a per-user path that
 * is always writable and findable, unlike the working dir of a double-clicked
 * app. Lines are timestamped and appended, so a lingering file stays readable
 * and repeated errors are not lost. No "started/stopped" noise -- errors only. */
static void log_error(const char *fmt, ...)
{
    char base[MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
    FILE *f;
    SYSTEMTIME t;
    va_list ap;

    /* per-user local app-data dir via the shell API -- NO environment variables.
     * Fall back to the OS temp dir (also an API, not an env read), then "." */
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base) != S_OK
        && GetTempPathA(sizeof base, base) == 0)
        snprintf(base, sizeof base, ".");
    snprintf(dir, sizeof dir, "%s\\AIS", base);
    CreateDirectoryA(dir, NULL);     /* one level under an always-present parent */
    snprintf(path, sizeof path, "%s\\ais-error.log", dir);

    f = fopen(path, "a");          /* lazily created on the first error */
    if (f == NULL)
        return;
    GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  ",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* A hard crash bypasses log_error(), so capture it here too -- the one gap of
 * "log only errors". Records the exception, then lets the app terminate. */
static LONG WINAPI on_crash(EXCEPTION_POINTERS *ep)
{
    log_error("crash: exception 0x%08lx at %p",
              (unsigned long)ep->ExceptionRecord->ExceptionCode,
              (void *)ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

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

static void do_get(void)
{
    char *keys = get_text(g_keys);
    /* default is OR (any key); the "Match all keys" checkbox switches to AND. */
    int or_mode = (SendMessage(g_or, BM_GETCHECK, 0, 0) != BST_CHECKED);

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
                bar = strchr(line, '|');             /* split the leading "id|" */
                {
                    const char *val = (bar != NULL) ? bar + 1 : line;
                    /* keep the id with the row so Edit/Delete can target it */
                    long id = (bar != NULL) ? strtol(line, NULL, 10) : 0;
                    if (*val != '\0') {
                        LRESULT ix = SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)val);
                        if (ix != LB_ERR && ix != LB_ERRSPACE)
                            SendMessage(g_list, LB_SETITEMDATA, (WPARAM)ix, (LPARAM)id);
                    }
                }
                if (nl == NULL)
                    break;
                line = nl + 1;
            }
            ais_embed_free(res);
        } else {
            log_error("get failed (keys='%s')", keys);
        }
    }
    free(keys);
}

static void do_add(void)
{
    char *val = get_text(g_value);
    char *vk  = get_text(g_vkeys);

    if (g_ais != NULL && val != NULL && val[0] != '\0') {
        if (ais_embed_store(g_ais, (vk != NULL) ? vk : "", val) < 0)
            log_error("save failed (keys='%s')", (vk != NULL) ? vk : "");
        if (vk != NULL && vk[0] != '\0')                 /* echo the save into the Get box */
            SetWindowTextA(g_keys, vk);
        SetWindowTextA(g_value, "");                     /* reset the Add form for the next entry */
        SetWindowTextA(g_vkeys, "");
        do_get();                                        /* the saved item shows in the results */
        SetFocus(g_vkeys);
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

/* ---- a minimal modal "input box" (Win32 has none built in) --------------
 * Built from an in-memory dialog template so we still need no .rc file. It
 * shows a prompt static, a single edit, and OK/Cancel. On OK the edit text is
 * copied into OUT (size OUTSZ); returns 1 if OK was pressed, else 0. */
enum { IB_PROMPT = 200, IB_EDIT = 201 };

struct ib_ctx { const char *prompt; char *out; int outsz; int ok; };

static INT_PTR CALLBACK ib_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    struct ib_ctx *c;
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR)lp);
        c = (struct ib_ctx *)lp;
        SetDlgItemTextA(dlg, IB_PROMPT, c->prompt);
        SetDlgItemTextA(dlg, IB_EDIT, c->out);   /* seed with current text */
        SetFocus(GetDlgItem(dlg, IB_EDIT));
        SendMessage(GetDlgItem(dlg, IB_EDIT), EM_SETSEL, 0, (LPARAM)-1);
        return FALSE;       /* we set the focus ourselves */
    case WM_CLOSE:                 /* the title-bar X: same as Cancel, never stuck */
        EndDialog(dlg, 0);
        return TRUE;
    case WM_COMMAND:
        c = (struct ib_ctx *)GetWindowLongPtr(dlg, GWLP_USERDATA);
        if (LOWORD(wp) == IDOK) {
            if (c != NULL)
                GetDlgItemTextA(dlg, IB_EDIT, c->out, c->outsz);
            if (c != NULL)
                c->ok = 1;
            EndDialog(dlg, 1);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 0);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static int input_box(HWND owner, const char *title, const char *prompt,
                     char *buf, int bufsz)
{
    /* DLGTEMPLATE + four DLGITEMTEMPLATEs assembled by hand, WORD-aligned.
     * Sizes are in dialog units. Controls: prompt static, edit, OK, Cancel. */
    struct ib_ctx ctx;
    WORD *tpl, *p;
    static const WCHAR cls_static[] = { 0xFFFF, 0x0082, 0 };  /* STATIC */
    static const WCHAR cls_edit[]   = { 0xFFFF, 0x0081, 0 };  /* EDIT   */
    static const WCHAR cls_button[] = { 0xFFFF, 0x0080, 0 };  /* BUTTON */
    static const WCHAR empty[]      = { 0 };
    char *raw;
    INT_PTR rc;
    int i;

    ctx.prompt = prompt;
    ctx.out    = buf;
    ctx.outsz  = bufsz;
    ctx.ok     = 0;

    raw = (char *)calloc(1, 2048);
    if (raw == NULL)
        return 0;
    p = (WORD *)raw;

    /* DLGTEMPLATE header */
    *(DWORD *)p = WS_POPUP | WS_BORDER | WS_SYSMENU | WS_CAPTION | DS_MODALFRAME
                  | DS_SETFONT; p += 2;
    *(DWORD *)p = 0; p += 2;     /* dwExtendedStyle */
    *p++ = 4;                    /* cdit: number of controls */
    *p++ = 30; *p++ = 30;        /* x, y */
    *p++ = 220; *p++ = 80;       /* cx, cy */
    *p++ = 0;                    /* no menu */
    *p++ = 0;                    /* default class */
    /* title (wide) */
    for (i = 0; title[i] != '\0'; i++)
        *p++ = (WORD)(unsigned char)title[i];
    *p++ = 0;
    /* font (DS_SETFONT): point size + face name */
    *p++ = 9;
    for (i = 0; "Segoe UI"[i] != '\0'; i++)
        *p++ = (WORD)(unsigned char)"Segoe UI"[i];
    *p++ = 0;

#define IB_ALIGN(pp) do { \
        if (((UINT_PTR)(pp) & 2) != 0) (pp)++; \
    } while (0)

    /* control 1: prompt static */
    IB_ALIGN(p);
    *(DWORD *)p = WS_CHILD | WS_VISIBLE | SS_LEFT; p += 2;
    *(DWORD *)p = 0; p += 2;
    *p++ = 8; *p++ = 8; *p++ = 204; *p++ = 24;     /* x y cx cy */
    *p++ = IB_PROMPT;
    for (i = 0; cls_static[i] != 0; i++) *p++ = cls_static[i];
    *p++ = 0;
    for (i = 0; empty[i] != 0; i++) *p++ = empty[i];
    *p++ = 0;
    *p++ = 0;                                      /* no creation data */

    /* control 2: edit */
    IB_ALIGN(p);
    *(DWORD *)p = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    p += 2;
    *(DWORD *)p = 0; p += 2;
    *p++ = 8; *p++ = 36; *p++ = 204; *p++ = 14;
    *p++ = IB_EDIT;
    for (i = 0; cls_edit[i] != 0; i++) *p++ = cls_edit[i];
    *p++ = 0;
    for (i = 0; empty[i] != 0; i++) *p++ = empty[i];
    *p++ = 0;
    *p++ = 0;

    /* control 3: OK */
    IB_ALIGN(p);
    *(DWORD *)p = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON; p += 2;
    *(DWORD *)p = 0; p += 2;
    *p++ = 108; *p++ = 58; *p++ = 50; *p++ = 14;
    *p++ = IDOK;
    for (i = 0; cls_button[i] != 0; i++) *p++ = cls_button[i];
    *p++ = 0;
    { const char *t = "OK"; for (i = 0; t[i] != '\0'; i++) *p++ = (WORD)(unsigned char)t[i]; }
    *p++ = 0;
    *p++ = 0;

    /* control 4: Cancel */
    IB_ALIGN(p);
    *(DWORD *)p = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON; p += 2;
    *(DWORD *)p = 0; p += 2;
    *p++ = 162; *p++ = 58; *p++ = 50; *p++ = 14;
    *p++ = IDCANCEL;
    for (i = 0; cls_button[i] != 0; i++) *p++ = cls_button[i];
    *p++ = 0;
    { const char *t = "Cancel"; for (i = 0; t[i] != '\0'; i++) *p++ = (WORD)(unsigned char)t[i]; }
    *p++ = 0;
    *p++ = 0;

#undef IB_ALIGN

    tpl = (WORD *)raw;
    rc = DialogBoxIndirectParamA(GetModuleHandle(NULL), (LPCDLGTEMPLATEA)tpl,
                                 owner, ib_proc, (LPARAM)&ctx);
    free(raw);
    return (rc == 1 && ctx.ok) ? 1 : 0;
}

/* The id stored with the currently selected result row, or 0 if none. */
static long selected_id(void)
{
    LRESULT i = SendMessage(g_list, LB_GETCURSEL, 0, 0);
    if (i == LB_ERR)
        return 0;
    return (long)SendMessage(g_list, LB_GETITEMDATA, (WPARAM)i, 0);
}

/* Delete the selected record after confirmation, then refresh the list. */
static void delete_selected(HWND hwnd)
{
    long id = selected_id();
    if (id <= 0 || g_ais == NULL)
        return;
    if (MessageBoxA(hwnd, "Delete this record?", "AIS",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    if (ais_embed_del(g_ais, id) < 0)
        log_error("delete failed (id=%ld)", id);
    do_get();
}

/* Edit the selected record's keys (a KEY adds it, -KEY removes it), refresh. */
static void edit_selected(HWND hwnd)
{
    long id = selected_id();
    char buf[1024];
    if (id <= 0 || g_ais == NULL)
        return;
    buf[0] = '\0';
    if (!input_box(hwnd, "Edit keys",
                   "Keys to change (a KEY adds it, -KEY removes it):",
                   buf, (int)sizeof buf))
        return;
    if (buf[0] == '\0')
        return;
    if (ais_embed_update(g_ais, id, buf) < 0)
        log_error("update failed (id=%ld, keys='%s')", id, buf);
    do_get();
}

static void layout(HWND hwnd)
{
    RECT r;
    int w, pad = dp(8), gap = dp(6), bh = dp(26);
    int btn = dp(80), cbw = dp(130), lblk = dp(40), lblv = dp(48);
    int y, by, getx, cbx, x, fields, valw, vkw;
    GetClientRect(hwnd, &r);
    w = r.right;

    /* TOP row: [Keys:] [keys ......] [Get] [Match all keys] */
    y    = pad;
    cbx  = w - pad - cbw;
    getx = cbx - gap - btn;
    MoveWindow(g_lkeys, pad, y, lblk, bh, TRUE);
    MoveWindow(g_keys,  pad + lblk + gap, y, getx - gap - (pad + lblk + gap), bh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_GET), getx, y, btn, bh, TRUE);
    MoveWindow(g_or,    cbx, y, cbw, bh, TRUE);

    /* BOTTOM row: [Keys:] [vkeys ...] [Value:] [value ...] [Add] (keys first) */
    by = r.bottom - pad - bh;
    fields = w - pad * 2 - lblk - lblv - btn - gap * 5;   /* width left for the two edits */
    if (fields < dp(120))
        fields = dp(120);
    vkw  = fields * 2 / 5;       /* keys are short; the value gets the rest */
    valw = fields - vkw;
    x = pad;
    MoveWindow(g_lvk,   x, by, lblk, bh, TRUE); x += lblk + gap;
    MoveWindow(g_vkeys, x, by, vkw,  bh, TRUE); x += vkw + gap;
    MoveWindow(g_lval,  x, by, lblv, bh, TRUE); x += lblv + gap;
    MoveWindow(g_value, x, by, valw, bh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_ADD), w - pad - btn, by, btn, bh, TRUE);

    /* MIDDLE: results list fills the gap between the two rows */
    MoveWindow(g_list, pad, y + bh + pad, w - pad * 2,
               by - pad - (y + bh + pad), TRUE);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        char dir[4096];
        NONCLIENTMETRICSA ncm;
        HDC hdc = GetDC(hwnd);
        if (hdc != NULL) {
            g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);   /* real DPI (we are DPI-aware) */
            ReleaseDC(hwnd, hdc);
        }
        /* Use the actual Windows UI font (Segoe UI on modern Windows), already
         * DPI-scaled -- not the tiny legacy DEFAULT_GUI_FONT bitmap font. */
        ncm.cbSize = sizeof ncm;
        if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0))
            g_font = CreateFontIndirectA(&ncm.lfMessageFont);
        if (g_font == NULL)
            g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        /* WS_TABSTOP on every interactive control so Tab/Shift+Tab cycle them
         * (handled by IsDialogMessage in the loop); statics are labels, no stop. */
        g_lkeys = mk(hwnd, "STATIC", "Keys:",  SS_LEFT, 0);
        g_keys  = mk(hwnd, "EDIT",   "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_KEYS);
        mk(hwnd, "BUTTON", "Get", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_GET);
        g_or    = mk(hwnd, "BUTTON", "Match all keys", WS_TABSTOP | BS_AUTOCHECKBOX, ID_OR);
        g_list  = mk(hwnd, "LISTBOX", "", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, ID_LIST);
        /* Add row, keys before value (consistent with the Get row above). */
        g_lvk   = mk(hwnd, "STATIC", "Keys:",  SS_LEFT, 0);
        g_vkeys = mk(hwnd, "EDIT",   "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_VKEYS);
        g_lval  = mk(hwnd, "STATIC", "Value:", SS_LEFT, 0);
        g_value = mk(hwnd, "EDIT",   "", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, ID_VALUE);
        mk(hwnd, "BUTTON", "Add", WS_TABSTOP | BS_PUSHBUTTON, ID_ADD);
        if (ais_locate(NULL, dir, sizeof dir) == 0)
            g_ais = ais_embed_open(dir);
        if (g_ais == NULL) {
            log_error("could not open the index (dir='%s')", dir);
            MessageBoxA(hwnd, "Could not open the AIS index.", "AIS", MB_ICONWARNING);
        }
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
        case ID_GET: do_get(); return 0;
        case ID_ADD:    do_add();    return 0;
        case ID_EDIT:   edit_selected(hwnd);   return 0;
        case ID_DELETE: delete_selected(hwnd); return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_DBLCLK)
                open_selected();
            return 0;
        }
        return 0;
    case WM_CONTEXTMENU:
        /* right-click (or the menu key) on a result row -> Edit keys / Delete */
        if ((HWND)wp == g_list) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            if (x == -1 && y == -1) {
                /* menu-key (Shift+F10) passes -1,-1: anchor on the selection */
                RECT wr;
                GetWindowRect(g_list, &wr);
                x = wr.left + 16;
                y = wr.top + 16;
            } else {
                /* right-click selects the row under the cursor so the action
                 * targets it (a LISTBOX does not auto-select on right-click) */
                POINT pt;
                LRESULT hit;
                pt.x = x; pt.y = y;
                ScreenToClient(g_list, &pt);
                hit = SendMessage(g_list, LB_ITEMFROMPOINT, 0,
                                  MAKELPARAM(pt.x, pt.y));
                if (HIWORD(hit) == 0)        /* low word valid only if in-client */
                    SendMessage(g_list, LB_SETCURSEL, (WPARAM)LOWORD(hit), 0);
            }
            if (SendMessage(g_list, LB_GETCURSEL, 0, 0) != LB_ERR) {
                HMENU m = CreatePopupMenu();
                if (m != NULL) {
                    AppendMenuA(m, MF_STRING, ID_EDIT, "Edit keys");
                    AppendMenuA(m, MF_STRING, ID_DELETE, "Delete");
                    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, NULL);
                    DestroyMenu(m);
                }
            }
            return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    case WM_DESTROY:
        if (g_ais != NULL)
            ais_embed_close(g_ais);
        if (g_font != NULL)
            DeleteObject(g_font);
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
    int dpi = 96;
    ACCEL a = { FVIRTKEY, VK_RETURN, ID_GET };   /* Enter = Get */

    (void)prev; (void)cmd;
    SetUnhandledExceptionFilter(on_crash);   /* capture hard crashes to the log */

    /* Become DPI-aware so text and controls are crisp (not blurry-stretched) on
     * high-DPI displays. Loaded dynamically so the exe still launches on pre-Vista
     * Windows that lacks the API. Must precede any DPI query / window creation. */
    {
        union { FARPROC p; BOOL (WINAPI *fn)(void); } u;
        u.p = GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
        if (u.fn != NULL)
            u.fn();
    }
    {
        HDC dc = GetDC(NULL);
        if (dc != NULL) { dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); }
    }
    {
        /* register the standard control classes with comctl32 v6 so the manifest's
         * visual styles theme them (the modern, native look). */
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof icc;
        icc.dwICC  = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icc);
    }

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
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         MulDiv(640, dpi, 96), MulDiv(480, dpi, 96),
                         NULL, NULL, inst, NULL);
    if (hwnd == NULL)
        return 1;
    ShowWindow(hwnd, show);

    acc = CreateAcceleratorTable(&a, 1);
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        /* IsDialogMessage gives Tab/Shift+Tab/arrow focus navigation across the
         * WS_TABSTOP controls (it skips hidden/disabled ones automatically). */
        if (!TranslateAccelerator(hwnd, acc, &msg) && !IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}
