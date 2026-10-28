#pragma once

// wxWidgets 2.4.2 stand-in for the native POSIX build (FAF_POSIX).
//
// The Windows build links the real library (a static Unicode MSW build, see
// platform/WxWidgets.h). It is a Windows-only dependency and is not part of the
// POSIX build. These headers declare the part of its API the engine uses, with
// the same class names, hierarchy, signatures and constants, so the engine's
// translation units compile unchanged:
//
//   - Plumbing behaves as in wx: wxString and its conversions, geometry types,
//     event tables and event dispatch (static tables, Connect, handler chains,
//     pending events), window parent/child ownership, sizers' ownership,
//     wxApp creation (IMPLEMENT_APP_NO_MAIN / wxEntry), delayed destruction of
//     top-level windows, tree/list/text control contents.
//   - Nothing is ever put on screen: there is no native window (GetHandle() is
//     0), dialogs return "cancelled" from ShowModal, message boxes print to
//     stderr. The native shell (window, input) comes with the SDL3 port; until
//     then, the paths that would show UI log once with FAF_POSIX_STUB.
//
// Only the wx 2.4.2 API the engine uses is here; add to it when a translation
// unit needs more, keeping wx's signatures. Everything is header-only.

#if !defined(FAF_POSIX)
#error "This wx stand-in is for the POSIX build only"
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

// --- Configuration (wxmswu: Unicode, FINAL, static) ---------------------------------------
#define wxMAJOR_VERSION 2
#define wxMINOR_VERSION 4
#define wxRELEASE_NUMBER 2
#define wxSUBRELEASE_NUMBER 0
#define wxCHECK_VERSION(major, minor, release)                                                       \
  (wxMAJOR_VERSION > (major) || (wxMAJOR_VERSION == (major) && wxMINOR_VERSION > (minor)) ||         \
   (wxMAJOR_VERSION == (major) && wxMINOR_VERSION == (minor) && wxRELEASE_NUMBER >= (release)))
#define wxUSE_UNICODE 1
#define wxUSE_WCHAR_T 1
#define wxUSE_GUI 1
#define wxUSE_TREELISTCTRL 1
// Marks the stand-in. __WXMSW__ is deliberately not defined.
#define __WXFAF_POSIX__ 1

#define WXDLLEXPORT
#define WXDLLIMPORT
#define WXDLLEXPORT_DATA(decl) decl
#define WXDLLIMPEXP_BASE
#define WXDLLIMPEXP_CORE
#define WXDLLIMPEXP_ADV
#define WXDLLIMPEXP_GIZMOS
#define WXUNUSED(identifier)
#define wxUnusedVar(identifier) ((void)(identifier))

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// FINAL build: wx's debug checks compile to nothing (see platform/WxWidgets.h).
#define wxASSERT(cond) ((void)0)
#define wxASSERT_MSG(cond, msg) ((void)0)
#define wxFAIL ((void)0)
#define wxFAIL_MSG(msg) ((void)0)
#define wxCHECK(cond, rc) \
  if (!(cond)) {          \
    return rc;            \
  }
#define wxCHECK_MSG(cond, rc, msg) wxCHECK(cond, rc)
#define wxCHECK_RET(cond, msg) \
  if (!(cond)) {               \
    return;                    \
  }
#define wxCHECK2(cond, op) \
  if (!(cond)) {           \
    op;                    \
  }
#define wxCHECK2_MSG(cond, op, msg) wxCHECK2(cond, op)

// --- Characters ---------------------------------------------------------------------------
// Unicode build: wxChar is wchar_t. On macOS that is 32 bits (16 on Windows); wide text
// that leaves the process has to be converted explicitly (platform/posix/README.md).
typedef wchar_t wxChar;
typedef wchar_t wxSChar;
typedef wchar_t wxUChar;
#define wxT(x) L##x
#define wxTRANSLATE(str) wxT(str)
#define wxSTRING_MAXLEN (static_cast<size_t>(-1))

// --- Integer and handle types -------------------------------------------------------------
typedef int wxWindowID;
typedef int wxEventType;
typedef int wxCoord;
typedef std::uint8_t wxByte;
typedef std::uint16_t wxWord;
typedef std::uint32_t wxUint32;
typedef std::int32_t wxInt32;
typedef std::int64_t wxLongLong_t;

typedef unsigned int WXUINT;
typedef std::uint32_t WXDWORD;
typedef std::uint16_t WXWORD;
typedef std::uintptr_t WXWPARAM;
typedef std::intptr_t WXLPARAM;
typedef std::intptr_t WXLRESULT;
typedef std::uintptr_t WXHANDLE;
typedef WXHANDLE WXHWND;
typedef WXHANDLE WXHINSTANCE;
typedef WXHANDLE WXHICON;
typedef WXHANDLE WXHMENU;
typedef WXHANDLE WXHCURSOR;
typedef WXHANDLE WXHBRUSH;
typedef WXHANDLE WXHPEN;
typedef WXHANDLE WXHFONT;
typedef WXHANDLE WXHDC;
typedef WXHANDLE WXHBITMAP;
typedef WXHANDLE WXHIMAGELIST;
typedef WXHANDLE WXHACCEL;
typedef WXHANDLE WXHGLOBAL;
typedef WXHWND WXWidget;
typedef void* WXFARPROC;
typedef struct tagMSG WXMSG;

// --- Window styles (wx/defs.h) -------------------------------------------------------------
#define wxVSCROLL 0x80000000
#define wxHSCROLL 0x40000000
#define wxCAPTION 0x20000000
#define wxDOUBLE_BORDER 0x10000000
#define wxSUNKEN_BORDER 0x08000000
#define wxRAISED_BORDER 0x04000000
#define wxBORDER 0x02000000
#define wxSIMPLE_BORDER wxBORDER
#define wxSTATIC_BORDER 0x01000000
#define wxTRANSPARENT_WINDOW 0x00100000
#define wxNO_BORDER 0x00200000
#define wxUSER_COLOURS 0x00800000
#define wxNO_3D 0x00800000
#define wxCLIP_CHILDREN 0x00400000
#define wxCLIP_SIBLINGS 0x20000000
#define wxTAB_TRAVERSAL 0x00080000
#define wxWANTS_CHARS 0x00040000
#define wxRETAINED 0x00020000
#define wxBACKINGSTORE wxRETAINED
#define wxNO_FULL_REPAINT_ON_RESIZE 0x00010000
#define wxWS_EX_VALIDATE_RECURSIVELY 0x00000001
#define wxWS_EX_BLOCK_EVENTS 0x00000002
#define wxWS_EX_TRANSIENT 0x00000004

enum wxBorder
{
  wxBORDER_DEFAULT = 0,
  wxBORDER_NONE = 0x00200000,
  wxBORDER_STATIC = 0x01000000,
  wxBORDER_SIMPLE = 0x02000000,
  wxBORDER_RAISED = 0x04000000,
  wxBORDER_SUNKEN = 0x08000000,
  wxBORDER_DOUBLE = 0x10000000,
  wxBORDER_MASK = 0x1f200000
};

#define wxSTAY_ON_TOP 0x8000
#define wxICONIZE 0x4000
#define wxMINIMIZE wxICONIZE
#define wxMAXIMIZE 0x2000
#define wxSYSTEM_MENU 0x0800
#define wxMINIMIZE_BOX 0x0400
#define wxMAXIMIZE_BOX 0x0200
#define wxTINY_CAPTION_HORIZ 0x0100
#define wxTINY_CAPTION_VERT 0x0080
#define wxRESIZE_BORDER 0x0040
#define wxTHICK_FRAME wxRESIZE_BORDER
#define wxRESIZE_BOX wxMAXIMIZE_BOX
#define wxFRAME_TOOL_WINDOW 0x0004
#define wxFRAME_FLOAT_ON_PARENT 0x0008
#define wxFRAME_NO_WINDOW_MENU 0x0100
#define wxFRAME_NO_TASKBAR 0x0002
#define wxFRAME_SHAPED 0x0010
#define wxDEFAULT_FRAME_STYLE \
  (wxSYSTEM_MENU | wxRESIZE_BORDER | wxMINIMIZE_BOX | wxMAXIMIZE_BOX | wxCAPTION | wxCLIP_CHILDREN)
#define wxDEFAULT_DIALOG_STYLE (wxSYSTEM_MENU | wxCAPTION)
#define wxDIALOG_MODAL 0x0020
#define wxDIALOG_MODELESS 0x0000
#define wxDIALOG_NO_PARENT 0x0001

// --- Sizer and layout flags ----------------------------------------------------------------
enum wxOrientation
{
  wxHORIZONTAL = 0x0004,
  wxVERTICAL = 0x0008,
  wxBOTH = (wxVERTICAL | wxHORIZONTAL)
};

enum wxDirection
{
  wxLEFT = 0x0010,
  wxRIGHT = 0x0020,
  wxUP = 0x0040,
  wxDOWN = 0x0080,
  wxTOP = wxUP,
  wxBOTTOM = wxDOWN,
  wxNORTH = wxUP,
  wxSOUTH = wxDOWN,
  wxWEST = wxLEFT,
  wxEAST = wxRIGHT,
  wxALL = (wxUP | wxDOWN | wxRIGHT | wxLEFT)
};

enum wxAlignment
{
  wxALIGN_NOT = 0x0000,
  wxALIGN_CENTER_HORIZONTAL = 0x0100,
  wxALIGN_CENTRE_HORIZONTAL = wxALIGN_CENTER_HORIZONTAL,
  wxALIGN_LEFT = wxALIGN_NOT,
  wxALIGN_TOP = wxALIGN_NOT,
  wxALIGN_RIGHT = 0x0200,
  wxALIGN_BOTTOM = 0x0400,
  wxALIGN_CENTER_VERTICAL = 0x0800,
  wxALIGN_CENTRE_VERTICAL = wxALIGN_CENTER_VERTICAL,
  wxALIGN_CENTER = (wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL),
  wxALIGN_CENTRE = wxALIGN_CENTER,
  wxALIGN_MASK = 0x0f00
};

enum wxStretch
{
  wxSTRETCH_NOT = 0x0000,
  wxSHRINK = 0x1000,
  wxGROW = 0x2000,
  wxEXPAND = wxGROW,
  wxSHAPED = 0x4000,
  wxADJUST_MINSIZE = 0x8000,
  wxTILE = 0xc000
};

#define wxSIZE_AUTO_WIDTH 0x0001
#define wxSIZE_AUTO_HEIGHT 0x0002
#define wxSIZE_AUTO (wxSIZE_AUTO_WIDTH | wxSIZE_AUTO_HEIGHT)
#define wxSIZE_USE_EXISTING 0x0000
#define wxSIZE_ALLOW_MINUS_ONE 0x0004
#define wxSIZE_NO_ADJUSTMENTS 0x0008

// --- Standard identifiers ------------------------------------------------------------------
enum
{
  wxID_ANY = -1,
  wxID_LOWEST = 4999,
  wxID_OPEN,
  wxID_CLOSE,
  wxID_NEW,
  wxID_SAVE,
  wxID_SAVEAS,
  wxID_REVERT,
  wxID_EXIT,
  wxID_UNDO,
  wxID_REDO,
  wxID_HELP,
  wxID_PRINT,
  wxID_PRINT_SETUP,
  wxID_PREVIEW,
  wxID_ABOUT,
  wxID_HELP_CONTENTS,
  wxID_HELP_COMMANDS,
  wxID_HELP_PROCEDURES,
  wxID_HELP_CONTEXT,
  wxID_CLOSE_ALL,
  wxID_CUT = 5030,
  wxID_COPY,
  wxID_PASTE,
  wxID_CLEAR,
  wxID_FIND,
  wxID_DUPLICATE,
  wxID_SELECTALL,
  wxID_DELETE,
  wxID_REPLACE,
  wxID_REPLACE_ALL,
  wxID_PROPERTIES,
  wxID_VIEW_DETAILS,
  wxID_VIEW_LARGEICONS,
  wxID_VIEW_SMALLICONS,
  wxID_VIEW_LIST,
  wxID_VIEW_SORTDATE,
  wxID_VIEW_SORTNAME,
  wxID_VIEW_SORTSIZE,
  wxID_VIEW_SORTTYPE,
  wxID_FILE1 = 5050,
  wxID_FILE2,
  wxID_FILE3,
  wxID_FILE4,
  wxID_FILE5,
  wxID_FILE6,
  wxID_FILE7,
  wxID_FILE8,
  wxID_FILE9,
  wxID_OK = 5100,
  wxID_CANCEL,
  wxID_APPLY,
  wxID_YES,
  wxID_NO,
  wxID_STATIC,
  wxID_FORWARD,
  wxID_BACKWARD,
  wxID_DEFAULT,
  wxID_MORE,
  wxID_SETUP,
  wxID_RESET,
  wxID_CONTEXT_HELP,
  wxID_YESTOALL,
  wxID_NOTOALL,
  wxID_ABORT,
  wxID_RETRY,
  wxID_IGNORE,
  wxID_FILEDLGG = 5900,
  wxID_HIGHEST = 5999
};

// Message box / dialog styles.
#define wxYES_NO 0x0000000A
#define wxCANCEL 0x00000010
#define wxYES 0x00000002
#define wxOK 0x00000004
#define wxNO 0x00000008
#define wxNO_DEFAULT 0x00000080
#define wxYES_DEFAULT 0x00000000
#define wxICON_EXCLAMATION 0x00000100
#define wxICON_HAND 0x00000200
#define wxICON_WARNING wxICON_EXCLAMATION
#define wxICON_ERROR wxICON_HAND
#define wxICON_QUESTION 0x00000400
#define wxICON_INFORMATION 0x00000800
#define wxICON_STOP wxICON_HAND
#define wxICON_ASTERISK wxICON_INFORMATION
#define wxICON_MASK (0x00000100 | 0x00000200 | 0x00000400 | 0x00000800)
#define wxFORWARD 0x00001000
#define wxBACKWARD 0x00002000
#define wxRESET 0x00004000
#define wxHELP 0x00008000
#define wxMORE 0x00010000
#define wxSETUP 0x00020000
#define wxCENTRE 0x00000001
#define wxCENTER wxCENTRE
#define wxCENTRE_ON_SCREEN 0x00000002
#define wxCENTER_ON_SCREEN wxCENTRE_ON_SCREEN
#define wxCENTER_FRAME 0x00000000

// File and directory dialogs.
#define wxOPEN 0x0001
#define wxSAVE 0x0002
#define wxOVERWRITE_PROMPT 0x0004
#define wxHIDE_READONLY 0x0008
#define wxFILE_MUST_EXIST 0x0010
#define wxMULTIPLE 0x0020
#define wxCHANGE_DIR 0x0040
#define wxDD_NEW_DIR_BUTTON 0x0080
#define wxDD_DEFAULT_STYLE (wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)

// Controls.
#define wxBU_LEFT 0x0040
#define wxBU_TOP 0x0080
#define wxBU_RIGHT 0x0100
#define wxBU_BOTTOM 0x0200
#define wxBU_EXACTFIT 0x0001
#define wxBU_AUTODRAW 0x0004
#define wxCHK_2STATE 0x0000
#define wxCHK_3STATE 0x1000
#define wxCB_SIMPLE 0x0004
#define wxCB_SORT 0x0008
#define wxCB_READONLY 0x0010
#define wxCB_DROPDOWN 0x0020
#define wxLB_SORT 0x0010
#define wxLB_SINGLE 0x0020
#define wxLB_MULTIPLE 0x0040
#define wxLB_EXTENDED 0x0080
#define wxLB_HSCROLL wxHSCROLL
#define wxST_NO_AUTORESIZE 0x0001
#define wxTE_NO_VSCROLL 0x0002
#define wxTE_AUTO_SCROLL 0x0008
#define wxTE_READONLY 0x0010
#define wxTE_MULTILINE 0x0020
#define wxTE_PROCESS_TAB 0x0040
#define wxTE_LEFT 0x0000
#define wxTE_CENTER wxALIGN_CENTER_HORIZONTAL
#define wxTE_CENTRE wxTE_CENTER
#define wxTE_RIGHT wxALIGN_RIGHT
#define wxTE_RICH 0x0080
#define wxTE_PROCESS_ENTER 0x0400
#define wxTE_PASSWORD 0x0800
#define wxTE_AUTO_URL 0x1000
#define wxTE_NOHIDESEL 0x2000
#define wxTE_DONTWRAP wxHSCROLL
#define wxTE_LINEWRAP 0x4000
#define wxTE_WORDWRAP 0x0000
#define wxTE_RICH2 0x8000
#define wxSL_HORIZONTAL wxHORIZONTAL
#define wxSL_VERTICAL wxVERTICAL
#define wxSL_NOTIFY_DRAG 0x0000
#define wxSL_TICKS 0x0010
#define wxSL_AUTOTICKS wxSL_TICKS
#define wxSL_LABELS 0x0020
#define wxSL_LEFT 0x0040
#define wxSL_TOP 0x0080
#define wxSL_RIGHT 0x0100
#define wxSL_BOTTOM 0x0200
#define wxSL_BOTH 0x0400
#define wxSL_SELRANGE 0x0800
#define wxSB_HORIZONTAL wxHORIZONTAL
#define wxSB_VERTICAL wxVERTICAL
#define wxNB_FIXEDWIDTH 0x0010
#define wxNB_TOP 0x0000
#define wxNB_LEFT 0x0020
#define wxNB_RIGHT 0x0040
#define wxNB_BOTTOM 0x0080
#define wxNB_MULTILINE 0x0100
#define wxSP_NOBORDER 0x0000
#define wxSP_NOSASH 0x0010
#define wxSP_BORDER 0x0020
#define wxSP_PERMIT_UNSPLIT 0x0040
#define wxSP_LIVE_UPDATE 0x0080
#define wxSP_3DSASH 0x0100
#define wxSP_3DBORDER 0x0200
#define wxSP_FULLSASH 0x0400
#define wxSP_3D (wxSP_3DBORDER | wxSP_3DSASH)
#define wxTB_HORIZONTAL wxHORIZONTAL
#define wxTB_VERTICAL wxVERTICAL
#define wxTB_3DBUTTONS 0x0010
#define wxTB_FLAT 0x0020
#define wxTB_DOCKABLE 0x0040
#define wxTB_NOICONS 0x0080
#define wxTB_TEXT 0x0100
#define wxTB_NODIVIDER 0x0200
#define wxTB_NOALIGN 0x0400
#define wxST_SIZEGRIP 0x0010
#define wxSPLASH_CENTRE_ON_PARENT 0x01
#define wxSPLASH_CENTRE_ON_SCREEN 0x02
#define wxSPLASH_NO_CENTRE 0x00
#define wxSPLASH_TIMEOUT 0x04
#define wxSPLASH_NO_TIMEOUT 0x00
#define wxDIRCTRL_DIR_ONLY 0x0010
#define wxDIRCTRL_SELECT_FIRST 0x0020
#define wxDIRCTRL_SHOW_FILTERS 0x0040
#define wxDIRCTRL_3D_INTERNAL 0x0080
#define wxDIRCTRL_EDIT_LABELS 0x0100

// Tree control styles.
#define wxTR_NO_BUTTONS 0x0000
#define wxTR_HAS_BUTTONS 0x0001
#define wxTR_TWIST_BUTTONS 0x0002
#define wxTR_NO_LINES 0x0004
#define wxTR_LINES_AT_ROOT 0x0008
#define wxTR_MAC_BUTTONS wxTR_TWIST_BUTTONS
#define wxTR_AQUA_BUTTONS 0x0010
#define wxTR_SINGLE 0x0000
#define wxTR_MULTIPLE 0x0020
#define wxTR_EXTENDED 0x0040
#define wxTR_FULL_ROW_HIGHLIGHT 0x2000
#define wxTR_EDIT_LABELS 0x0200
#define wxTR_ROW_LINES 0x0400
#define wxTR_HIDE_ROOT 0x0800
#define wxTR_HAS_VARIABLE_ROW_HEIGHT 0x0080
#define wxTR_DEFAULT_STYLE (wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT)

// List control styles and flags.
#define wxLC_VRULES 0x0001
#define wxLC_HRULES 0x0002
#define wxLC_ICON 0x0004
#define wxLC_SMALL_ICON 0x0008
#define wxLC_LIST 0x0010
#define wxLC_REPORT 0x0020
#define wxLC_ALIGN_TOP 0x0040
#define wxLC_ALIGN_LEFT 0x0080
#define wxLC_AUTOARRANGE 0x0100
#define wxLC_VIRTUAL 0x0200
#define wxLC_EDIT_LABELS 0x0400
#define wxLC_NO_HEADER 0x0800
#define wxLC_NO_SORT_HEADER 0x1000
#define wxLC_SINGLE_SEL 0x2000
#define wxLC_SORT_ASCENDING 0x4000
#define wxLC_SORT_DESCENDING 0x8000
#define wxLIST_MASK_STATE 0x0001
#define wxLIST_MASK_TEXT 0x0002
#define wxLIST_MASK_IMAGE 0x0004
#define wxLIST_MASK_DATA 0x0008
#define wxLIST_SET_ITEM 0x0010
#define wxLIST_MASK_WIDTH 0x0020
#define wxLIST_MASK_FORMAT 0x0040
#define wxLIST_STATE_DONTCARE 0x0000
#define wxLIST_STATE_DROPHILITED 0x0001
#define wxLIST_STATE_FOCUSED 0x0002
#define wxLIST_STATE_SELECTED 0x0004
#define wxLIST_STATE_CUT 0x0008
#define wxLIST_NEXT_ABOVE 0
#define wxLIST_NEXT_ALL 1
#define wxLIST_NEXT_BELOW 2
#define wxLIST_NEXT_LEFT 3
#define wxLIST_NEXT_RIGHT 4
#define wxLIST_AUTOSIZE -1
#define wxLIST_AUTOSIZE_USEHEADER -2
#define wxLIST_HITTEST_NOWHERE 0x0004
#define wxLIST_HITTEST_ONITEM 0x00A0

enum wxListColumnFormat
{
  wxLIST_FORMAT_LEFT,
  wxLIST_FORMAT_RIGHT,
  wxLIST_FORMAT_CENTRE,
  wxLIST_FORMAT_CENTER = wxLIST_FORMAT_CENTRE
};

enum
{
  wxIMAGE_LIST_NORMAL,
  wxIMAGE_LIST_SMALL,
  wxIMAGE_LIST_STATE
};

// Accelerators and menu items.
#define wxACCEL_NORMAL 0x00
#define wxACCEL_ALT 0x01
#define wxACCEL_CTRL 0x02
#define wxACCEL_SHIFT 0x04

enum wxItemKind
{
  wxITEM_SEPARATOR = -1,
  wxITEM_NORMAL,
  wxITEM_CHECK,
  wxITEM_RADIO,
  wxITEM_MAX
};

// --- GDI ----------------------------------------------------------------------------------
enum wxBitmapType
{
  wxBITMAP_TYPE_INVALID,
  wxBITMAP_TYPE_BMP,
  wxBITMAP_TYPE_BMP_RESOURCE,
  wxBITMAP_TYPE_RESOURCE = wxBITMAP_TYPE_BMP_RESOURCE,
  wxBITMAP_TYPE_ICO,
  wxBITMAP_TYPE_ICO_RESOURCE,
  wxBITMAP_TYPE_CUR,
  wxBITMAP_TYPE_CUR_RESOURCE,
  wxBITMAP_TYPE_XBM,
  wxBITMAP_TYPE_XBM_DATA,
  wxBITMAP_TYPE_XPM,
  wxBITMAP_TYPE_XPM_DATA,
  wxBITMAP_TYPE_TIF,
  wxBITMAP_TYPE_TIF_RESOURCE,
  wxBITMAP_TYPE_GIF,
  wxBITMAP_TYPE_GIF_RESOURCE,
  wxBITMAP_TYPE_PNG,
  wxBITMAP_TYPE_PNG_RESOURCE,
  wxBITMAP_TYPE_JPEG,
  wxBITMAP_TYPE_JPEG_RESOURCE,
  wxBITMAP_TYPE_PNM,
  wxBITMAP_TYPE_PNM_RESOURCE,
  wxBITMAP_TYPE_PCX,
  wxBITMAP_TYPE_PCX_RESOURCE,
  wxBITMAP_TYPE_PICT,
  wxBITMAP_TYPE_PICT_RESOURCE,
  wxBITMAP_TYPE_ICON,
  wxBITMAP_TYPE_ICON_RESOURCE,
  wxBITMAP_TYPE_ANI,
  wxBITMAP_TYPE_IFF,
  wxBITMAP_TYPE_MACCURSOR,
  wxBITMAP_TYPE_MACCURSOR_RESOURCE,
  wxBITMAP_TYPE_ANY = 50
};

enum
{
  wxDEFAULT = 70,
  wxDECORATIVE,
  wxROMAN,
  wxSCRIPT,
  wxSWISS,
  wxMODERN,
  wxTELETYPE,
  wxVARIABLE = 80,
  wxFIXED,
  wxNORMAL = 90,
  wxLIGHT,
  wxBOLD,
  wxITALIC,
  wxSLANT,
  wxSOLID = 100,
  wxDOT,
  wxLONG_DASH,
  wxSHORT_DASH,
  wxDOT_DASH,
  wxUSER_DASH,
  wxTRANSPARENT,
  wxSTIPPLE_MASK_OPAQUE,
  wxSTIPPLE_MASK,
  wxSTIPPLE = 110,
  wxBDIAGONAL_HATCH,
  wxCROSSDIAG_HATCH,
  wxFDIAGONAL_HATCH,
  wxCROSS_HATCH,
  wxHORIZONTAL_HATCH,
  wxVERTICAL_HATCH,
  wxJOIN_BEVEL = 120,
  wxJOIN_MITER,
  wxJOIN_ROUND,
  wxCAP_ROUND = 130,
  wxCAP_PROJECTING,
  wxCAP_BUTT
};

enum
{
  wxODDEVEN_RULE = 1,
  wxWINDING_RULE
};

enum wxStockCursor
{
  wxCURSOR_NONE,
  wxCURSOR_ARROW,
  wxCURSOR_RIGHT_ARROW,
  wxCURSOR_BULLSEYE,
  wxCURSOR_CHAR,
  wxCURSOR_CROSS,
  wxCURSOR_HAND,
  wxCURSOR_IBEAM,
  wxCURSOR_LEFT_BUTTON,
  wxCURSOR_MAGNIFIER,
  wxCURSOR_MIDDLE_BUTTON,
  wxCURSOR_NO_ENTRY,
  wxCURSOR_PAINT_BRUSH,
  wxCURSOR_PENCIL,
  wxCURSOR_POINT_LEFT,
  wxCURSOR_POINT_RIGHT,
  wxCURSOR_QUESTION_ARROW,
  wxCURSOR_RIGHT_BUTTON,
  wxCURSOR_SIZENESW,
  wxCURSOR_SIZENS,
  wxCURSOR_SIZENWSE,
  wxCURSOR_SIZEWE,
  wxCURSOR_SIZING,
  wxCURSOR_SPRAYCAN,
  wxCURSOR_WAIT,
  wxCURSOR_WATCH,
  wxCURSOR_BLANK,
  wxCURSOR_DEFAULT,
  wxCURSOR_ARROWWAIT,
  wxCURSOR_MAX
};

// --- Keyboard (wx/defs.h wxKeyCode; the Maui key codes, EMauiKeyCode, are these values) ---
enum wxKeyCode
{
  WXK_BACK = 8,
  WXK_TAB = 9,
  WXK_RETURN = 13,
  WXK_ESCAPE = 27,
  WXK_SPACE = 32,
  WXK_DELETE = 127,

  WXK_START = 300,
  WXK_LBUTTON,
  WXK_RBUTTON,
  WXK_CANCEL,
  WXK_MBUTTON,
  WXK_CLEAR,
  WXK_SHIFT,
  WXK_ALT,
  WXK_CONTROL,
  WXK_MENU,
  WXK_PAUSE,
  WXK_CAPITAL,
  WXK_PRIOR,
  WXK_NEXT,
  WXK_END,
  WXK_HOME,
  WXK_LEFT,
  WXK_UP,
  WXK_RIGHT,
  WXK_DOWN,
  WXK_SELECT,
  WXK_PRINT,
  WXK_EXECUTE,
  WXK_SNAPSHOT,
  WXK_INSERT,
  WXK_HELP,
  WXK_NUMPAD0,
  WXK_NUMPAD1,
  WXK_NUMPAD2,
  WXK_NUMPAD3,
  WXK_NUMPAD4,
  WXK_NUMPAD5,
  WXK_NUMPAD6,
  WXK_NUMPAD7,
  WXK_NUMPAD8,
  WXK_NUMPAD9,
  WXK_MULTIPLY,
  WXK_ADD,
  WXK_SEPARATOR,
  WXK_SUBTRACT,
  WXK_DECIMAL,
  WXK_DIVIDE,
  WXK_F1,
  WXK_F2,
  WXK_F3,
  WXK_F4,
  WXK_F5,
  WXK_F6,
  WXK_F7,
  WXK_F8,
  WXK_F9,
  WXK_F10,
  WXK_F11,
  WXK_F12,
  WXK_F13,
  WXK_F14,
  WXK_F15,
  WXK_F16,
  WXK_F17,
  WXK_F18,
  WXK_F19,
  WXK_F20,
  WXK_F21,
  WXK_F22,
  WXK_F23,
  WXK_F24,
  WXK_NUMLOCK,
  WXK_SCROLL,
  WXK_PAGEUP,
  WXK_PAGEDOWN,

  WXK_NUMPAD_SPACE,
  WXK_NUMPAD_TAB,
  WXK_NUMPAD_ENTER,
  WXK_NUMPAD_F1,
  WXK_NUMPAD_F2,
  WXK_NUMPAD_F3,
  WXK_NUMPAD_F4,
  WXK_NUMPAD_HOME,
  WXK_NUMPAD_LEFT,
  WXK_NUMPAD_UP,
  WXK_NUMPAD_RIGHT,
  WXK_NUMPAD_DOWN,
  WXK_NUMPAD_PRIOR,
  WXK_NUMPAD_PAGEUP,
  WXK_NUMPAD_NEXT,
  WXK_NUMPAD_PAGEDOWN,
  WXK_NUMPAD_END,
  WXK_NUMPAD_BEGIN,
  WXK_NUMPAD_INSERT,
  WXK_NUMPAD_DELETE,
  WXK_NUMPAD_EQUAL,
  WXK_NUMPAD_MULTIPLY,
  WXK_NUMPAD_ADD,
  WXK_NUMPAD_SEPARATOR,
  WXK_NUMPAD_SUBTRACT,
  WXK_NUMPAD_DECIMAL,
  WXK_NUMPAD_DIVIDE,

  WXK_WINDOWS_LEFT,
  WXK_WINDOWS_RIGHT,
  WXK_WINDOWS_MENU
};

// --- Forward declarations -------------------------------------------------------------------
class wxObject;
class wxClassInfo;
class wxString;
class wxMBConv;
class wxEvent;
class wxEvtHandler;
class wxWindow;
class wxWindowBase;
class wxApp;
class wxAppBase;
class wxSizer;
class wxDC;
class wxMenu;
class wxMenuBar;
class wxToolBar;
class wxStatusBar;
class wxValidator;
class wxToolTip;
class wxCaret;
class wxDropTarget;
class wxLayoutConstraints;
class wxAcceleratorTable;
class wxBitmap;
class wxIcon;
class wxImage;
class wxImageList;
class wxFont;
class wxColour;
class wxBrush;
class wxPen;
class wxCursor;
class wxRegion;
class wxPalette;
class wxTreeItemData;
class wxListItemAttr;
class wxTopLevelWindow;
class wxFrame;
class wxDialog;
class wxPanel;
class wxControl;
