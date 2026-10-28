#pragma once

// Win32 on POSIX: Window. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatWindow.cpp.
//
// user32 windowing, input, messages, hooks, dialogs and the clipboard.
//
// Two modes, chosen by the window system (../shell/Shell.h):
// - Shell active (the game, after faf_posix::shell::Init): real windows. Top-level HWNDs are
//   SDL3 windows, child HWNDs are rectangles inside them; every coordinate is in drawable
//   pixels. A message queue on the main thread carries the input SDL delivers (WM_KEY*,
//   WM_CHAR, WM_MOUSE*) and posted messages; window-state messages (WM_SIZE, WM_ACTIVATE,
//   WM_SETFOCUS, WM_CLOSE, ...) are sent to the window procedure as Windows sends them. Key
//   state, cursor, monitors and the clipboard are the real ones (SDL).
// - Shell inactive (headless runs): no window exists, and these answer for a process without
//   one, in the Windows return conventions callers check: no window (null HWND, FALSE), no
//   message pending, every key up, the cursor at (0, 0), a single 1920x1080 monitor, an empty
//   clipboard. Calls that would show something log once with FAF_POSIX_STUB; message boxes
//   print their text to stderr.

FAF_DECLARE_HANDLE(HHOOK);
typedef HANDLE HDWP;

// --- Messages -------------------------------------------------------------------------------
typedef struct tagMSG {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
} MSG, *PMSG, *LPMSG;

// WM_GETMINMAXINFO's lParam.
typedef struct tagMINMAXINFO {
  POINT ptReserved;
  POINT ptMaxSize;
  POINT ptMaxPosition;
  POINT ptMinTrackSize;
  POINT ptMaxTrackSize;
} MINMAXINFO, *PMINMAXINFO, *LPMINMAXINFO;

typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR(CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT(CALLBACK* HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETREDRAW 0x000B
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SYSCOLORCHANGE 0x0015
#define WM_ENDSESSION 0x0016
#define WM_SHOWWINDOW 0x0018
#define WM_SETTINGCHANGE 0x001A
#define WM_ACTIVATEAPP 0x001C
#define WM_CANCELMODE 0x001F
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NOTIFY 0x004E
#define WM_DISPLAYCHANGE 0x007E
#define WM_GETICON 0x007F
#define WM_SETICON 0x0080
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCPAINT 0x0085
#define WM_NCACTIVATE 0x0086
#define WM_GETDLGCODE 0x0087
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_KEYLAST 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_HSCROLL 0x0114
#define WM_VSCROLL 0x0115
#define WM_INITMENU 0x0116
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011F
#define WM_MENUCHAR 0x0120
#define WM_ENTERIDLE 0x0121
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_PARENTNOTIFY 0x0210
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define WM_SIZING 0x0214
#define WM_CAPTURECHANGED 0x0215
#define WM_MOVING 0x0216
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE 0x0219
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_DROPFILES 0x0233
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_MOUSEHOVER 0x02A1
#define WM_MOUSELEAVE 0x02A3
#define WM_CUT 0x0300
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
#define WM_UNDO 0x0304
#define WM_PRINT 0x0317
#define WM_PRINTCLIENT 0x0318
#define WM_APP 0x8000
#define WM_USER 0x0400

// Rich edit / edit control messages the log window sends.
#define EM_GETSEL 0x00B0
#define EM_SETSEL 0x00B1
#define EM_SCROLL 0x00B5
#define EM_LINESCROLL 0x00B6
#define EM_SCROLLCARET 0x00B7
#define EM_GETLINECOUNT 0x00BA
#define EM_LINEINDEX 0x00BB
#define EM_REPLACESEL 0x00C2
#define EM_LIMITTEXT 0x00C5
#define EM_SETREADONLY 0x00CF
#define EM_GETFIRSTVISIBLELINE 0x00CE
#define EM_EXLIMITTEXT (WM_USER + 53)
#define EM_SETCHARFORMAT (WM_USER + 68)
#define EM_SETEVENTMASK (WM_USER + 69)
#define EM_HIDESELECTION (WM_USER + 63)
#define EM_GETTEXTLENGTHEX (WM_USER + 95)

// Scroll bar commands and bars (WM_HSCROLL / WM_VSCROLL, Get/SetScrollInfo).
#define SB_HORZ 0
#define SB_VERT 1
#define SB_CTL 2
#define SB_BOTH 3
#define SB_LINEUP 0
#define SB_LINELEFT 0
#define SB_LINEDOWN 1
#define SB_LINERIGHT 1
#define SB_PAGEUP 2
#define SB_PAGELEFT 2
#define SB_PAGEDOWN 3
#define SB_PAGERIGHT 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_LEFT 6
#define SB_BOTTOM 7
#define SB_RIGHT 7
#define SB_ENDSCROLL 8

#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_NEXTWINDOW 0xF040
#define SC_PREVWINDOW 0xF050
#define SC_CLOSE 0xF060
#define SC_VSCROLL 0xF070
#define SC_HSCROLL 0xF080
#define SC_MOUSEMENU 0xF090
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SC_TASKLIST 0xF130
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170

#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define SIZE_MAXSHOW 3
#define SIZE_MAXHIDE 4
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2

#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#define GET_KEYSTATE_WPARAM(wParam) (LOWORD(wParam))
#define GET_XBUTTON_WPARAM(wParam) (HIWORD(wParam))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))

// Hit-test codes (WM_NCHITTEST / WM_SETCURSOR).
#define HTERROR (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2
#define HTSYSMENU 3
#define HTGROWBOX 4
#define HTMENU 5
#define HTHSCROLL 6
#define HTVSCROLL 7
#define HTMINBUTTON 8
#define HTMAXBUTTON 9
#define HTLEFT 10
#define HTRIGHT 11
#define HTTOP 12
#define HTTOPLEFT 13
#define HTTOPRIGHT 14
#define HTBOTTOM 15
#define HTBOTTOMLEFT 16
#define HTBOTTOMRIGHT 17
#define HTBORDER 18
#define HTCLOSE 20

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002

#define QS_KEY 0x0001
#define QS_MOUSEMOVE 0x0002
#define QS_MOUSEBUTTON 0x0004
#define QS_POSTMESSAGE 0x0008
#define QS_TIMER 0x0010
#define QS_PAINT 0x0020
#define QS_SENDMESSAGE 0x0040
#define QS_HOTKEY 0x0080
#define QS_ALLPOSTMESSAGE 0x0100
#define QS_RAWINPUT 0x0400
#define QS_MOUSE (QS_MOUSEMOVE | QS_MOUSEBUTTON)
#define QS_INPUT (QS_MOUSE | QS_KEY | QS_RAWINPUT)
#define QS_ALLEVENTS (QS_INPUT | QS_POSTMESSAGE | QS_TIMER | QS_PAINT | QS_HOTKEY)
#define QS_ALLINPUT (QS_INPUT | QS_POSTMESSAGE | QS_TIMER | QS_PAINT | QS_HOTKEY | QS_SENDMESSAGE)

// --- Window styles, positions, show commands ------------------------------------------------
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW (WS_POPUP | WS_BORDER | WS_SYSMENU)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_APPWINDOW 0x00040000L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_WINDOWEDGE 0x00000100L
#define WS_EX_LAYERED 0x00080000

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define GWL_ID (-12)
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_HWNDPARENT (-8)
#define GWLP_USERDATA (-21)
#define GWLP_ID (-12)
#define DWLP_MSGRESULT 0

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOCOPYBITS 0x0100
#define SWP_NOOWNERZORDER 0x0200
#define SWP_NOSENDCHANGING 0x0400
#define SWP_ASYNCWINDOWPOS 0x4000
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_DESKTOP ((HWND)0)
#define HWND_MESSAGE ((HWND)-3)

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SW_FORCEMINIMIZE 11

#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3

#define GW_HWNDFIRST 0
#define GW_HWNDLAST 1
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5

#define CW_USEDEFAULT ((int)0x80000000)

// --- Window classes ---------------------------------------------------------------------------
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_OWNDC 0x0020
#define CS_CLASSDC 0x0040
#define CS_PARENTDC 0x0080
#define CS_NOCLOSE 0x0200
#define CS_SAVEBITS 0x0800

typedef struct tagWNDCLASSW {
  UINT style;
  WNDPROC lpfnWndProc;
  int cbClsExtra;
  int cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCWSTR lpszMenuName;
  LPCWSTR lpszClassName;
} WNDCLASSW, *PWNDCLASSW, *LPWNDCLASSW;

typedef struct tagWNDCLASSEXW {
  UINT cbSize;
  UINT style;
  WNDPROC lpfnWndProc;
  int cbClsExtra;
  int cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  LPCWSTR lpszMenuName;
  LPCWSTR lpszClassName;
  HICON hIconSm;
} WNDCLASSEXW, *PWNDCLASSEXW, *LPWNDCLASSEXW;

// --- Menus and dialogs ------------------------------------------------------------------------
#define MF_INSERT 0x00000000L
#define MF_CHANGE 0x00000080L
#define MF_APPEND 0x00000100L
#define MF_DELETE 0x00000200L
#define MF_REMOVE 0x00001000L
#define MF_BYCOMMAND 0x00000000L
#define MF_BYPOSITION 0x00000400L
#define MF_SEPARATOR 0x00000800L
#define MF_ENABLED 0x00000000L
#define MF_GRAYED 0x00000001L
#define MF_DISABLED 0x00000002L
#define MF_UNCHECKED 0x00000000L
#define MF_CHECKED 0x00000008L
#define MF_STRING 0x00000000L
#define MF_POPUP 0x00000010L

#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL 0x00000003L
#define MB_YESNO 0x00000004L
#define MB_RETRYCANCEL 0x00000005L
#define MB_ICONHAND 0x00000010L
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONASTERISK 0x00000040L
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_ICONSTOP MB_ICONHAND
#define MB_DEFBUTTON1 0x00000000L
#define MB_DEFBUTTON2 0x00000100L
#define MB_APPLMODAL 0x00000000L
#define MB_SYSTEMMODAL 0x00001000L
#define MB_TASKMODAL 0x00002000L
#define MB_SETFOREGROUND 0x00010000L
#define MB_TOPMOST 0x00040000L
#define MB_TYPEMASK 0x0000000FL
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

// --- Cursors and icons ------------------------------------------------------------------------
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_IBEAM MAKEINTRESOURCEW(32513)
#define IDC_WAIT MAKEINTRESOURCEW(32514)
#define IDC_CROSS MAKEINTRESOURCEW(32515)
#define IDC_UPARROW MAKEINTRESOURCEW(32516)
#define IDC_SIZEALL MAKEINTRESOURCEW(32646)
#define IDC_NO MAKEINTRESOURCEW(32648)
#define IDC_HAND MAKEINTRESOURCEW(32649)
#define IDI_APPLICATION MAKEINTRESOURCEW(32512)

typedef struct _ICONINFO {
  BOOL fIcon;
  DWORD xHotspot;
  DWORD yHotspot;
  HBITMAP hbmMask;
  HBITMAP hbmColor;
} ICONINFO, *PICONINFO;

typedef struct tagCURSORINFO {
  DWORD cbSize;
  DWORD flags;
  HCURSOR hCursor;
  POINT ptScreenPos;
} CURSORINFO, *PCURSORINFO, *LPCURSORINFO;
#define CURSOR_SHOWING 0x00000001

// --- System parameters and metrics ------------------------------------------------------------
#define SPI_GETBEEP 0x0001
#define SPI_GETMOUSE 0x0003
#define SPI_SETMOUSE 0x0004
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011
#define SPI_GETWORKAREA 0x0030
#define SPI_GETFILTERKEYS 0x0032
#define SPI_SETFILTERKEYS 0x0033
#define SPI_GETTOGGLEKEYS 0x0034
#define SPI_SETTOGGLEKEYS 0x0035
#define SPI_GETSTICKYKEYS 0x003A
#define SPI_SETSTICKYKEYS 0x003B
#define SPI_GETSCREENREADER 0x0046
#define SPI_GETWHEELSCROLLLINES 0x0068
#define SPI_GETMOUSESPEED 0x0070
#define SPI_SETMOUSESPEED 0x0071
#define SPI_GETFOREGROUNDLOCKTIMEOUT 0x2000
#define SPI_SETFOREGROUNDLOCKTIMEOUT 0x2001
#define SPIF_UPDATEINIFILE 0x0001
#define SPIF_SENDCHANGE 0x0002

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVSCROLL 2
#define SM_CYHSCROLL 3
#define SM_CYCAPTION 4
#define SM_CXBORDER 5
#define SM_CYBORDER 6
#define SM_CXDLGFRAME 7
#define SM_CYDLGFRAME 8
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXCURSOR 13
#define SM_CYCURSOR 14
#define SM_CYMENU 15
#define SM_CXFULLSCREEN 16
#define SM_CYFULLSCREEN 17
#define SM_MOUSEPRESENT 19
#define SM_SWAPBUTTON 23
#define SM_CXMIN 28
#define SM_CYMIN 29
#define SM_CXSIZE 30
#define SM_CYSIZE 31
#define SM_CXFRAME 32
#define SM_CYFRAME 33
#define SM_CXSIZEFRAME SM_CXFRAME
#define SM_CYSIZEFRAME SM_CYFRAME
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXFIXEDFRAME SM_CXDLGFRAME
#define SM_CYFIXEDFRAME SM_CYDLGFRAME
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define SM_CXDRAG 68
#define SM_CYDRAG 69
#define SM_CMOUSEBUTTONS 43
#define SM_CXMAXIMIZED 61
#define SM_CYMAXIMIZED 62
#define SM_XVIRTUALSCREEN 76
#define SM_YVIRTUALSCREEN 77
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_CMONITORS 80
#define SM_MEDIACENTER 87
#define SM_REMOTESESSION 0x1000

// --- Monitors ---------------------------------------------------------------------------------
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define MONITORINFOF_PRIMARY 0x00000001
#define CCHDEVICENAME 32

typedef struct tagMONITORINFO {
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
} MONITORINFO, *LPMONITORINFO;

typedef struct tagMONITORINFOEXW {
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
  WCHAR szDevice[CCHDEVICENAME];
} MONITORINFOEXW, *LPMONITORINFOEXW;

// --- Hooks ------------------------------------------------------------------------------------
#define WH_KEYBOARD 2
#define WH_GETMESSAGE 3
#define WH_CALLWNDPROC 4
#define WH_MOUSE 7
#define WH_KEYBOARD_LL 13
#define WH_MOUSE_LL 14
#define HC_ACTION 0

typedef struct tagKBDLLHOOKSTRUCT {
  DWORD vkCode;
  DWORD scanCode;
  DWORD flags;
  DWORD time;
  ULONG_PTR dwExtraInfo;
} KBDLLHOOKSTRUCT, *LPKBDLLHOOKSTRUCT, *PKBDLLHOOKSTRUCT;

// --- Keyboard mapping -------------------------------------------------------------------------
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define KF_EXTENDED 0x0100
#define KF_ALTDOWN 0x2000
#define KF_REPEAT 0x4000
#define KF_UP 0x8000

// --- Clipboard ----------------------------------------------------------------------------------
#define CF_TEXT 1
#define CF_BITMAP 2
#define CF_OEMTEXT 7
#define CF_DIB 8
#define CF_UNICODETEXT 13
#define CF_HDROP 15

// --- Functions ----------------------------------------------------------------------------------
#if defined(__cplusplus)
// Message boxes: the text goes to stderr; the answer is IDOK for MB_OK boxes and the
// negative choice (IDNO / IDCANCEL) for boxes that ask a question.
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) noexcept;
int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) noexcept;
BOOL MessageBeep(UINT uType) noexcept;

// Messages: the main thread's queue while the shell is active; none otherwise (PeekMessage
// finds nothing, GetMessage returns WM_QUIT rather than block forever, PostMessage fails).
BOOL PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept;
BOOL PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) noexcept;
BOOL GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) noexcept;
BOOL TranslateMessage(const MSG* lpMsg) noexcept;
LRESULT DispatchMessageW(const MSG* lpMsg) noexcept;
LRESULT DispatchMessageA(const MSG* lpMsg) noexcept;
void PostQuitMessage(int nExitCode) noexcept;
BOOL PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
BOOL PostThreadMessageW(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT DefWindowProcW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
LRESULT CallWindowProcW(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) noexcept;
DWORD GetQueueStatus(UINT flags) noexcept;
DWORD GetMessageTime() noexcept;
DWORD GetMessagePos() noexcept;
BOOL WaitMessage() noexcept;
// MsgWaitForMultipleObjects(Ex) are declared by the Sync area.

// Window classes (the class's procedure, cursor and CS_DBLCLKS are honoured).
ATOM RegisterClassW(const WNDCLASSW* lpWndClass) noexcept;
ATOM RegisterClassExW(const WNDCLASSEXW* lpWndClass) noexcept;
BOOL UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance) noexcept;

// Windows: real while the shell is active; otherwise none exist and every query fails or
// reports "not there".
HWND CreateWindowExW(
  DWORD dwExStyle,
  LPCWSTR lpClassName,
  LPCWSTR lpWindowName,
  DWORD dwStyle,
  int X,
  int Y,
  int nWidth,
  int nHeight,
  HWND hWndParent,
  HMENU hMenu,
  HINSTANCE hInstance,
  LPVOID lpParam
) noexcept;
BOOL DestroyWindow(HWND hWnd) noexcept;
BOOL IsWindow(HWND hWnd) noexcept;
BOOL IsWindowVisible(HWND hWnd) noexcept;
BOOL IsWindowEnabled(HWND hWnd) noexcept;
BOOL IsIconic(HWND hWnd) noexcept;
BOOL IsZoomed(HWND hWnd) noexcept;
BOOL ShowWindow(HWND hWnd, int nCmdShow) noexcept;
BOOL UpdateWindow(HWND hWnd) noexcept;
BOOL EnableWindow(HWND hWnd, BOOL bEnable) noexcept;
BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) noexcept;
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) noexcept;
BOOL GetWindowRect(HWND hWnd, LPRECT lpRect) noexcept;
BOOL GetClientRect(HWND hWnd, LPRECT lpRect) noexcept;
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu) noexcept;
BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle) noexcept;
BOOL ScreenToClient(HWND hWnd, LPPOINT lpPoint) noexcept;
BOOL ClientToScreen(HWND hWnd, LPPOINT lpPoint) noexcept;
BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) noexcept;
BOOL ValidateRect(HWND hWnd, const RECT* lpRect) noexcept;
BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString) noexcept;
BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString) noexcept;
int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) noexcept;
LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex) noexcept;
LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR dwNewLong) noexcept;
LONG GetWindowLongW(HWND hWnd, int nIndex) noexcept;
LONG SetWindowLongW(HWND hWnd, int nIndex, LONG dwNewLong) noexcept;
HWND GetParent(HWND hWnd) noexcept;
HWND SetParent(HWND hWndChild, HWND hWndNewParent) noexcept;
HWND GetAncestor(HWND hwnd, UINT gaFlags) noexcept;
HWND GetWindow(HWND hWnd, UINT uCmd) noexcept;
BOOL IsChild(HWND hWndParent, HWND hWnd) noexcept;
BOOL BringWindowToTop(HWND hWnd) noexcept;
HWND ChildWindowFromPoint(HWND hWndParent, POINT Point) noexcept;
HWND GetDesktopWindow() noexcept;
HWND GetForegroundWindow() noexcept;
BOOL SetForegroundWindow(HWND hWnd) noexcept;
HWND GetActiveWindow() noexcept;
HWND SetActiveWindow(HWND hWnd) noexcept;
HWND GetFocus() noexcept;
HWND SetFocus(HWND hWnd) noexcept;
HWND GetCapture() noexcept;
HWND SetCapture(HWND hWnd) noexcept;
BOOL ReleaseCapture() noexcept;
HWND WindowFromPoint(POINT Point) noexcept;
BOOL FlashWindow(HWND hWnd, BOOL bInvert) noexcept;
DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) noexcept;

// Menus and dialogs.
HMENU GetSystemMenu(HWND hWnd, BOOL bRevert) noexcept;
BOOL AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem) noexcept;
BOOL DrawMenuBar(HWND hWnd) noexcept;
INT_PTR DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) noexcept;
BOOL EndDialog(HWND hDlg, INT_PTR nResult) noexcept;
HWND GetDlgItem(HWND hDlg, int nIDDlgItem) noexcept;
BOOL SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString) noexcept;
void InitCommonControls() noexcept;

// Input: the shell's keyboard and mouse (GetKeyState follows the messages taken from the
// queue, GetAsyncKeyState the device); headless, every key is up and the cursor sits at (0, 0).
SHORT GetKeyState(int nVirtKey) noexcept;
SHORT GetAsyncKeyState(int vKey) noexcept;
BOOL GetKeyboardState(PBYTE lpKeyState) noexcept;
UINT MapVirtualKeyW(UINT uCode, UINT uMapType) noexcept;
UINT MapVirtualKeyA(UINT uCode, UINT uMapType) noexcept;
HCURSOR SetCursor(HCURSOR hCursor) noexcept;
HCURSOR GetCursor() noexcept;
int ShowCursor(BOOL bShow) noexcept;
BOOL GetCursorPos(LPPOINT lpPoint) noexcept;
BOOL SetCursorPos(int X, int Y) noexcept;
BOOL GetCursorInfo(PCURSORINFO pci) noexcept;
BOOL ClipCursor(const RECT* lpRect) noexcept;
BOOL GetClipCursor(LPRECT lpRect) noexcept;
HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) noexcept;
HICON LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) noexcept;
HICON CreateIconIndirect(PICONINFO piconinfo) noexcept;
BOOL DestroyCursor(HCURSOR hCursor) noexcept;
BOOL DestroyIcon(HICON hIcon) noexcept;
UINT GetDoubleClickTime() noexcept;

// Hooks are accepted and never called.
HHOOK SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hmod, DWORD dwThreadId) noexcept;
BOOL UnhookWindowsHookEx(HHOOK hhk) noexcept;
LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam) noexcept;

// System settings: the shell's displays (in pixels); headless, one primary 1920x1080
// monitor. Parameters read back as their defaults and settings changes are accepted
// without effect.
BOOL SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) noexcept;
BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) noexcept;
int GetSystemMetrics(int nIndex) noexcept;
HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags) noexcept;
HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags) noexcept;
HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags) noexcept;
BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept;
BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi) noexcept;

// Clipboard: the system clipboard's text (CF_UNICODETEXT / CF_TEXT in GlobalAlloc memory)
// while the shell is active; headless there is none to open.
BOOL OpenClipboard(HWND hWndNewOwner) noexcept;
BOOL CloseClipboard() noexcept;
BOOL EmptyClipboard() noexcept;
HANDLE SetClipboardData(UINT uFormat, HANDLE hMem) noexcept;
HANDLE GetClipboardData(UINT uFormat) noexcept;
BOOL IsClipboardFormatAvailable(UINT format) noexcept;

// ShellExecute(Ex) are declared by the Misc area.

#endif // __cplusplus

// --- Virtual keys (winuser.h) ------------------------------------------------------------------
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_KANA 0x15
#define VK_JUNJA 0x17
#define VK_FINAL 0x18
#define VK_KANJI 0x19
#define VK_ESCAPE 0x1B
#define VK_CONVERT 0x1C
#define VK_NONCONVERT 0x1D
#define VK_ACCEPT 0x1E
#define VK_MODECHANGE 0x1F
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_SLEEP 0x5F
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
#define VK_PROCESSKEY 0xE5
#define VK_PACKET 0xE7
#define VK_ATTN 0xF6
#define VK_CRSEL 0xF7
#define VK_EXSEL 0xF8
#define VK_EREOF 0xF9
#define VK_PLAY 0xFA
#define VK_ZOOM 0xFB
#define VK_NONAME 0xFC
#define VK_PA1 0xFD
#define VK_OEM_CLEAR 0xFE
