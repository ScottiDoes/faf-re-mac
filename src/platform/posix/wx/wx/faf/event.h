#pragma once

// wx stand-in: events, event tables and wxEvtHandler (see wx/defs.h). Dispatch works as in
// wx 2.4.2: static tables (DECLARE/BEGIN/END_EVENT_TABLE), Connect()ed handlers, handler
// chains (PushEventHandler) and pending events processed when the application idles.

#include "wx/faf/gdi.h"

#include <deque>
#include <mutex>

// --- Event types -----------------------------------------------------------------------------
#define wxEVT_FIRST 10000
#define wxEVT_USER_FIRST (wxEVT_FIRST + 2000)

inline wxEventType wxNewEventType()
{
  static std::atomic<wxEventType> sLastUsed{wxEVT_USER_FIRST};
  return sLastUsed.fetch_add(1);
}

#define DECLARE_EVENT_TYPE(name, value) extern const wxEventType name;
#define DECLARE_LOCAL_EVENT_TYPE(name, value) extern const wxEventType name;
#define DECLARE_EXPORTED_EVENT_TYPE(expdecl, name, value) extern const wxEventType name;
#define DEFINE_EVENT_TYPE(name) const wxEventType name = wxNewEventType();
#define DEFINE_LOCAL_EVENT_TYPE(name) const wxEventType name = wxNewEventType();
#define BEGIN_DECLARE_EVENT_TYPES()
#define END_DECLARE_EVENT_TYPES()

// The library's own event types. Fixed values here: they only have to be distinct from each
// other and from wxNewEventType's.
#define FAF_WX_EVENT_TYPE(name, value) inline constexpr wxEventType name = (value);
FAF_WX_EVENT_TYPE(wxEVT_NULL, 0)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_BUTTON_CLICKED, 1)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_CHECKBOX_CLICKED, 2)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_CHOICE_SELECTED, 3)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LISTBOX_SELECTED, 4)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LISTBOX_DOUBLECLICKED, 5)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_CHECKLISTBOX_TOGGLED, 6)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TEXT_UPDATED, 7)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TEXT_ENTER, 8)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_MENU_SELECTED, 9)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SLIDER_UPDATED, 10)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_RADIOBOX_SELECTED, 11)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_RADIOBUTTON_SELECTED, 12)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SCROLLBAR_UPDATED, 13)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_VLBOX_SELECTED, 14)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_COMBOBOX_SELECTED, 15)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TOOL_RCLICKED, 16)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TOOL_ENTER, 17)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SPINCTRL_UPDATED, 18)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TEXT_URL, 19)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TEXT_MAXLEN, 20)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, 21)
FAF_WX_EVENT_TYPE(wxEVT_SOCKET, 50)
FAF_WX_EVENT_TYPE(wxEVT_TIMER, 80)
FAF_WX_EVENT_TYPE(wxEVT_LEFT_DOWN, 100)
FAF_WX_EVENT_TYPE(wxEVT_LEFT_UP, 101)
FAF_WX_EVENT_TYPE(wxEVT_MIDDLE_DOWN, 102)
FAF_WX_EVENT_TYPE(wxEVT_MIDDLE_UP, 103)
FAF_WX_EVENT_TYPE(wxEVT_RIGHT_DOWN, 104)
FAF_WX_EVENT_TYPE(wxEVT_RIGHT_UP, 105)
FAF_WX_EVENT_TYPE(wxEVT_MOTION, 106)
FAF_WX_EVENT_TYPE(wxEVT_ENTER_WINDOW, 107)
FAF_WX_EVENT_TYPE(wxEVT_LEAVE_WINDOW, 108)
FAF_WX_EVENT_TYPE(wxEVT_LEFT_DCLICK, 109)
FAF_WX_EVENT_TYPE(wxEVT_MIDDLE_DCLICK, 110)
FAF_WX_EVENT_TYPE(wxEVT_RIGHT_DCLICK, 111)
FAF_WX_EVENT_TYPE(wxEVT_SET_FOCUS, 112)
FAF_WX_EVENT_TYPE(wxEVT_KILL_FOCUS, 113)
FAF_WX_EVENT_TYPE(wxEVT_MOUSEWHEEL, 114)
FAF_WX_EVENT_TYPE(wxEVT_NC_LEFT_DOWN, 200)
FAF_WX_EVENT_TYPE(wxEVT_NC_LEFT_UP, 201)
FAF_WX_EVENT_TYPE(wxEVT_NC_MIDDLE_DOWN, 202)
FAF_WX_EVENT_TYPE(wxEVT_NC_MIDDLE_UP, 203)
FAF_WX_EVENT_TYPE(wxEVT_NC_RIGHT_DOWN, 204)
FAF_WX_EVENT_TYPE(wxEVT_NC_RIGHT_UP, 205)
FAF_WX_EVENT_TYPE(wxEVT_NC_MOTION, 206)
FAF_WX_EVENT_TYPE(wxEVT_NC_ENTER_WINDOW, 207)
FAF_WX_EVENT_TYPE(wxEVT_NC_LEAVE_WINDOW, 208)
FAF_WX_EVENT_TYPE(wxEVT_NC_LEFT_DCLICK, 209)
FAF_WX_EVENT_TYPE(wxEVT_NC_MIDDLE_DCLICK, 210)
FAF_WX_EVENT_TYPE(wxEVT_NC_RIGHT_DCLICK, 211)
FAF_WX_EVENT_TYPE(wxEVT_CHAR, 212)
FAF_WX_EVENT_TYPE(wxEVT_CHAR_HOOK, 213)
FAF_WX_EVENT_TYPE(wxEVT_NAVIGATION_KEY, 214)
FAF_WX_EVENT_TYPE(wxEVT_KEY_DOWN, 215)
FAF_WX_EVENT_TYPE(wxEVT_KEY_UP, 216)
FAF_WX_EVENT_TYPE(wxEVT_SET_CURSOR, 230)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_TOP, 300)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_BOTTOM, 301)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_LINEUP, 302)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_LINEDOWN, 303)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_PAGEUP, 304)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_PAGEDOWN, 305)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_THUMBTRACK, 306)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_THUMBRELEASE, 307)
FAF_WX_EVENT_TYPE(wxEVT_SCROLL_ENDSCROLL, 308)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_TOP, 320)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_BOTTOM, 321)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_LINEUP, 322)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_LINEDOWN, 323)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_PAGEUP, 324)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_PAGEDOWN, 325)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_THUMBTRACK, 326)
FAF_WX_EVENT_TYPE(wxEVT_SCROLLWIN_THUMBRELEASE, 327)
FAF_WX_EVENT_TYPE(wxEVT_SIZE, 400)
FAF_WX_EVENT_TYPE(wxEVT_MOVE, 401)
FAF_WX_EVENT_TYPE(wxEVT_CLOSE_WINDOW, 402)
FAF_WX_EVENT_TYPE(wxEVT_END_SESSION, 403)
FAF_WX_EVENT_TYPE(wxEVT_QUERY_END_SESSION, 404)
FAF_WX_EVENT_TYPE(wxEVT_ACTIVATE_APP, 405)
FAF_WX_EVENT_TYPE(wxEVT_POWER, 406)
FAF_WX_EVENT_TYPE(wxEVT_ACTIVATE, 409)
FAF_WX_EVENT_TYPE(wxEVT_CREATE, 410)
FAF_WX_EVENT_TYPE(wxEVT_DESTROY, 411)
FAF_WX_EVENT_TYPE(wxEVT_SHOW, 412)
FAF_WX_EVENT_TYPE(wxEVT_ICONIZE, 413)
FAF_WX_EVENT_TYPE(wxEVT_MAXIMIZE, 414)
FAF_WX_EVENT_TYPE(wxEVT_MOUSE_CAPTURE_CHANGED, 415)
FAF_WX_EVENT_TYPE(wxEVT_PAINT, 416)
FAF_WX_EVENT_TYPE(wxEVT_ERASE_BACKGROUND, 417)
FAF_WX_EVENT_TYPE(wxEVT_NC_PAINT, 418)
FAF_WX_EVENT_TYPE(wxEVT_PAINT_ICON, 419)
FAF_WX_EVENT_TYPE(wxEVT_MENU_OPEN, 420)
FAF_WX_EVENT_TYPE(wxEVT_MENU_CLOSE, 421)
FAF_WX_EVENT_TYPE(wxEVT_MENU_HIGHLIGHT, 422)
FAF_WX_EVENT_TYPE(wxEVT_CONTEXT_MENU, 423)
FAF_WX_EVENT_TYPE(wxEVT_SYS_COLOUR_CHANGED, 424)
FAF_WX_EVENT_TYPE(wxEVT_DISPLAY_CHANGED, 425)
FAF_WX_EVENT_TYPE(wxEVT_SETTING_CHANGED, 426)
FAF_WX_EVENT_TYPE(wxEVT_QUERY_NEW_PALETTE, 427)
FAF_WX_EVENT_TYPE(wxEVT_PALETTE_CHANGED, 428)
FAF_WX_EVENT_TYPE(wxEVT_JOY_BUTTON_DOWN, 429)
FAF_WX_EVENT_TYPE(wxEVT_JOY_BUTTON_UP, 430)
FAF_WX_EVENT_TYPE(wxEVT_JOY_MOVE, 431)
FAF_WX_EVENT_TYPE(wxEVT_JOY_ZMOVE, 432)
FAF_WX_EVENT_TYPE(wxEVT_DROP_FILES, 433)
FAF_WX_EVENT_TYPE(wxEVT_DRAW_ITEM, 434)
FAF_WX_EVENT_TYPE(wxEVT_MEASURE_ITEM, 435)
FAF_WX_EVENT_TYPE(wxEVT_COMPARE_ITEM, 436)
FAF_WX_EVENT_TYPE(wxEVT_INIT_DIALOG, 437)
FAF_WX_EVENT_TYPE(wxEVT_IDLE, 438)
FAF_WX_EVENT_TYPE(wxEVT_UPDATE_UI, 439)
FAF_WX_EVENT_TYPE(wxEVT_SIZING, 440)
FAF_WX_EVENT_TYPE(wxEVT_MOVING, 441)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LEFT_CLICK, 500)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LEFT_DCLICK, 501)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_RIGHT_CLICK, 502)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_RIGHT_DCLICK, 503)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SET_FOCUS, 504)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_KILL_FOCUS, 505)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_ENTER, 506)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_BEGIN_DRAG, 600)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_BEGIN_RDRAG, 601)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_BEGIN_LABEL_EDIT, 602)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_END_LABEL_EDIT, 603)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_DELETE_ITEM, 604)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_GET_INFO, 605)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_SET_INFO, 606)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_EXPANDED, 607)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_EXPANDING, 608)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_COLLAPSED, 609)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_COLLAPSING, 610)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_SEL_CHANGED, 611)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_SEL_CHANGING, 612)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_KEY_DOWN, 613)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_ACTIVATED, 614)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_RIGHT_CLICK, 615)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_ITEM_MIDDLE_CLICK, 616)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_TREE_END_DRAG, 617)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_BEGIN_DRAG, 700)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_BEGIN_RDRAG, 701)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_BEGIN_LABEL_EDIT, 702)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_END_LABEL_EDIT, 703)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_DELETE_ITEM, 704)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_DELETE_ALL_ITEMS, 705)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_GET_INFO, 706)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_SET_INFO, 707)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_SELECTED, 708)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_DESELECTED, 709)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_KEY_DOWN, 710)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_INSERT_ITEM, 711)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_COL_CLICK, 712)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_RIGHT_CLICK, 713)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_MIDDLE_CLICK, 714)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_ACTIVATED, 715)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_CACHE_HINT, 716)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_COL_RIGHT_CLICK, 717)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_COL_BEGIN_DRAG, 718)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_COL_DRAGGING, 719)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_COL_END_DRAG, 720)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_LIST_ITEM_FOCUSED, 721)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, 800)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGING, 801)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGED, 850)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGING, 851)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SPLITTER_DOUBLECLICKED, 852)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_SPLITTER_UNSPLIT, 853)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_DIRCTRL_SELECTIONCHANGED, 900)
FAF_WX_EVENT_TYPE(wxEVT_COMMAND_DIRCTRL_FILEACTIVATED, 901)
#undef FAF_WX_EVENT_TYPE

// --- Events (wx/event.h) ---------------------------------------------------------------------
class wxEvent : public wxObject
{
public:
  wxEvent(int winid = 0, wxEventType commandType = wxEVT_NULL) : m_eventType(commandType), m_id(winid) {}
  wxEvent(const wxEvent&) = default;
  wxEvent& operator=(const wxEvent&) = default;

  void SetEventType(wxEventType type) { m_eventType = type; }
  wxEventType GetEventType() const { return m_eventType; }
  wxObject* GetEventObject() const { return m_eventObject; }
  void SetEventObject(wxObject* object) { m_eventObject = object; }
  long GetTimestamp() const { return m_timeStamp; }
  void SetTimestamp(long timestamp = 0) { m_timeStamp = timestamp; }
  int GetId() const { return m_id; }
  void SetId(int id) { m_id = id; }
  void Skip(bool skip = true) { m_skipped = skip; }
  bool GetSkipped() const { return m_skipped; }
  bool IsCommandEvent() const { return m_isCommandEvent; }

  virtual wxEvent* Clone() const = 0;

public:
  wxObject* m_eventObject = nullptr;
  wxEventType m_eventType;
  long m_timeStamp = 0;
  int m_id;
  wxObject* m_callbackUserData = nullptr;
  bool m_skipped = false;
  bool m_isCommandEvent = false;
};

class wxCommandEvent : public wxEvent
{
public:
  wxCommandEvent(wxEventType commandType = wxEVT_NULL, int winid = 0) : wxEvent(winid, commandType)
  {
    m_isCommandEvent = true;
  }

  void SetClientData(void* clientData) { m_clientData = clientData; }
  void* GetClientData() const { return m_clientData; }
  void SetClientObject(wxObject* clientObject) { m_clientObject = clientObject; }
  wxObject* GetClientObject() const { return m_clientObject; }
  int GetSelection() const { return m_commandInt; }
  void SetString(const wxString& s) { m_commandString = s; }
  wxString GetString() const { return m_commandString; }
  bool IsChecked() const { return m_commandInt != 0; }
  bool IsSelection() const { return m_extraLong != 0; }
  void SetExtraLong(long extraLong) { m_extraLong = extraLong; }
  long GetExtraLong() const { return m_extraLong; }
  void SetInt(int i) { m_commandInt = i; }
  long GetInt() const { return m_commandInt; }
  wxEvent* Clone() const override { return new wxCommandEvent(*this); }

public:
  wxString m_commandString;
  int m_commandInt = 0;
  long m_extraLong = 0;
  void* m_clientData = nullptr;
  wxObject* m_clientObject = nullptr;
};

class wxNotifyEvent : public wxCommandEvent
{
public:
  wxNotifyEvent(wxEventType commandType = wxEVT_NULL, int winid = 0) : wxCommandEvent(commandType, winid) {}
  void Veto() { m_bAllow = false; }
  void Allow() { m_bAllow = true; }
  bool IsAllowed() const { return m_bAllow; }
  wxEvent* Clone() const override { return new wxNotifyEvent(*this); }

private:
  bool m_bAllow = true;
};

class wxScrollEvent : public wxCommandEvent
{
public:
  wxScrollEvent(wxEventType commandType = wxEVT_NULL, int winid = 0, int pos = 0, int orient = 0)
    : wxCommandEvent(commandType, winid)
  {
    m_extraLong = orient;
    m_commandInt = pos;
  }
  int GetOrientation() const { return static_cast<int>(m_extraLong); }
  int GetPosition() const { return m_commandInt; }
  void SetOrientation(int orient) { m_extraLong = orient; }
  void SetPosition(int pos) { m_commandInt = pos; }
  wxEvent* Clone() const override { return new wxScrollEvent(*this); }
};

class wxScrollWinEvent : public wxEvent
{
public:
  wxScrollWinEvent(wxEventType commandType = wxEVT_NULL, int pos = 0, int orient = 0)
    : wxEvent(0, commandType), m_commandInt(pos), m_extraLong(orient)
  {}
  int GetOrientation() const { return static_cast<int>(m_extraLong); }
  int GetPosition() const { return m_commandInt; }
  wxEvent* Clone() const override { return new wxScrollWinEvent(*this); }

  int m_commandInt;
  long m_extraLong;
};

class wxMouseEvent : public wxEvent
{
public:
  wxMouseEvent(wxEventType mouseType = wxEVT_NULL) : wxEvent(0, mouseType) {}

  bool IsButton() const { return Button(-1); }
  bool ButtonDown(int but = -1) const
  {
    switch (but) {
      case -1:
        return LeftDown() || MiddleDown() || RightDown();
      case 1:
        return LeftDown();
      case 2:
        return MiddleDown();
      case 3:
        return RightDown();
      default:
        return false;
    }
  }
  bool ButtonDClick(int but = -1) const
  {
    switch (but) {
      case -1:
        return LeftDClick() || MiddleDClick() || RightDClick();
      case 1:
        return LeftDClick();
      case 2:
        return MiddleDClick();
      case 3:
        return RightDClick();
      default:
        return false;
    }
  }
  bool ButtonUp(int but = -1) const
  {
    switch (but) {
      case -1:
        return LeftUp() || MiddleUp() || RightUp();
      case 1:
        return LeftUp();
      case 2:
        return MiddleUp();
      case 3:
        return RightUp();
      default:
        return false;
    }
  }
  bool Button(int but) const { return ButtonDown(but) || ButtonUp(but) || ButtonDClick(but); }
  bool ButtonIsDown(int but) const
  {
    switch (but) {
      case -1:
        return LeftIsDown() || MiddleIsDown() || RightIsDown();
      case 1:
        return LeftIsDown();
      case 2:
        return MiddleIsDown();
      case 3:
        return RightIsDown();
      default:
        return false;
    }
  }
  int GetButton() const
  {
    if (LeftDown() || LeftUp() || LeftDClick()) {
      return 1;
    }
    if (MiddleDown() || MiddleUp() || MiddleDClick()) {
      return 2;
    }
    if (RightDown() || RightUp() || RightDClick()) {
      return 3;
    }
    return -1;
  }

  bool ControlDown() const { return m_controlDown; }
  bool MetaDown() const { return m_metaDown; }
  bool AltDown() const { return m_altDown; }
  bool ShiftDown() const { return m_shiftDown; }

  bool LeftDown() const { return m_eventType == wxEVT_LEFT_DOWN; }
  bool MiddleDown() const { return m_eventType == wxEVT_MIDDLE_DOWN; }
  bool RightDown() const { return m_eventType == wxEVT_RIGHT_DOWN; }
  bool LeftUp() const { return m_eventType == wxEVT_LEFT_UP; }
  bool MiddleUp() const { return m_eventType == wxEVT_MIDDLE_UP; }
  bool RightUp() const { return m_eventType == wxEVT_RIGHT_UP; }
  bool LeftDClick() const { return m_eventType == wxEVT_LEFT_DCLICK; }
  bool MiddleDClick() const { return m_eventType == wxEVT_MIDDLE_DCLICK; }
  bool RightDClick() const { return m_eventType == wxEVT_RIGHT_DCLICK; }
  bool LeftIsDown() const { return m_leftDown; }
  bool MiddleIsDown() const { return m_middleDown; }
  bool RightIsDown() const { return m_rightDown; }
  bool Dragging() const { return m_eventType == wxEVT_MOTION && ButtonIsDown(-1); }
  bool Moving() const { return m_eventType == wxEVT_MOTION; }
  bool Entering() const { return m_eventType == wxEVT_ENTER_WINDOW; }
  bool Leaving() const { return m_eventType == wxEVT_LEAVE_WINDOW; }

  void GetPosition(wxCoord* xpos, wxCoord* ypos) const
  {
    if (xpos != nullptr) {
      *xpos = m_x;
    }
    if (ypos != nullptr) {
      *ypos = m_y;
    }
  }
  void GetPosition(long* xpos, long* ypos) const
  {
    if (xpos != nullptr) {
      *xpos = m_x;
    }
    if (ypos != nullptr) {
      *ypos = m_y;
    }
  }
  wxPoint GetPosition() const { return wxPoint(m_x, m_y); }
  wxPoint GetLogicalPosition(const wxDC&) const { return GetPosition(); }
  wxCoord GetX() const { return m_x; }
  wxCoord GetY() const { return m_y; }
  int GetWheelRotation() const { return m_wheelRotation; }
  int GetWheelDelta() const { return m_wheelDelta; }
  int GetLinesPerAction() const { return m_linesPerAction; }
  bool IsPageScroll() const { return static_cast<unsigned int>(m_linesPerAction) == 0xFFFFFFFFu; }
  wxEvent* Clone() const override { return new wxMouseEvent(*this); }

public:
  wxCoord m_x = 0;
  wxCoord m_y = 0;
  bool m_leftDown = false;
  bool m_middleDown = false;
  bool m_rightDown = false;
  bool m_controlDown = false;
  bool m_shiftDown = false;
  bool m_altDown = false;
  bool m_metaDown = false;
  int m_wheelRotation = 0;
  int m_wheelDelta = 120;
  int m_linesPerAction = 3;
};

class wxSetCursorEvent : public wxEvent
{
public:
  wxSetCursorEvent(wxCoord x = 0, wxCoord y = 0) : wxEvent(0, wxEVT_SET_CURSOR), m_x(x), m_y(y) {}
  wxCoord GetX() const { return m_x; }
  wxCoord GetY() const { return m_y; }
  void SetCursor(const wxCursor& cursor) { m_cursor = cursor; }
  const wxCursor& GetCursor() const { return m_cursor; }
  bool HasCursor() const { return m_cursor.Ok(); }
  wxEvent* Clone() const override { return new wxSetCursorEvent(*this); }

private:
  wxCoord m_x;
  wxCoord m_y;
  wxCursor m_cursor;
};

class wxKeyEvent : public wxEvent
{
public:
  wxKeyEvent(wxEventType keyType = wxEVT_NULL) : wxEvent(0, keyType) {}

  bool ControlDown() const { return m_controlDown; }
  bool MetaDown() const { return m_metaDown; }
  bool AltDown() const { return m_altDown; }
  bool ShiftDown() const { return m_shiftDown; }
  bool HasModifiers() const { return ControlDown() || AltDown() || MetaDown(); }
  int GetKeyCode() const { return static_cast<int>(m_keyCode); }
  long KeyCode() const { return m_keyCode; }
  wxUint32 GetRawKeyCode() const { return m_rawCode; }
  wxUint32 GetRawKeyFlags() const { return m_rawFlags; }
  void GetPosition(wxCoord* xpos, wxCoord* ypos) const
  {
    if (xpos != nullptr) {
      *xpos = m_x;
    }
    if (ypos != nullptr) {
      *ypos = m_y;
    }
  }
  wxPoint GetPosition() const { return wxPoint(m_x, m_y); }
  wxCoord GetX() const { return m_x; }
  wxCoord GetY() const { return m_y; }
  wxEvent* Clone() const override { return new wxKeyEvent(*this); }

public:
  wxCoord m_x = 0;
  wxCoord m_y = 0;
  long m_keyCode = 0;
  bool m_controlDown = false;
  bool m_shiftDown = false;
  bool m_altDown = false;
  bool m_metaDown = false;
  bool m_scanCode = false;
  wxUint32 m_rawCode = 0;
  wxUint32 m_rawFlags = 0;
};

class wxSizeEvent : public wxEvent
{
public:
  wxSizeEvent() : wxEvent(0, wxEVT_SIZE) {}
  wxSizeEvent(const wxSize& sz, int winid = 0) : wxEvent(winid, wxEVT_SIZE), m_size(sz) {}
  wxSize GetSize() const { return m_size; }
  wxEvent* Clone() const override { return new wxSizeEvent(*this); }

  wxSize m_size;
};

class wxMoveEvent : public wxEvent
{
public:
  wxMoveEvent() : wxEvent(0, wxEVT_MOVE) {}
  wxMoveEvent(const wxPoint& pos, int winid = 0) : wxEvent(winid, wxEVT_MOVE), m_pos(pos) {}
  wxPoint GetPosition() const { return m_pos; }
  wxEvent* Clone() const override { return new wxMoveEvent(*this); }

  wxPoint m_pos;
};

class wxPaintEvent : public wxEvent
{
public:
  wxPaintEvent(int id = 0) : wxEvent(id, wxEVT_PAINT) {}
  wxEvent* Clone() const override { return new wxPaintEvent(*this); }
};

class wxEraseEvent : public wxEvent
{
public:
  wxEraseEvent(int id = 0, wxDC* dc = nullptr) : wxEvent(id, wxEVT_ERASE_BACKGROUND), m_dc(dc) {}
  wxDC* GetDC() const { return m_dc; }
  wxEvent* Clone() const override { return new wxEraseEvent(*this); }

  wxDC* m_dc;
};

class wxFocusEvent : public wxEvent
{
public:
  wxFocusEvent(wxEventType type = wxEVT_NULL, int winid = 0) : wxEvent(winid, type) {}
  wxWindow* GetWindow() const { return m_win; }
  void SetWindow(wxWindow* win) { m_win = win; }
  wxEvent* Clone() const override { return new wxFocusEvent(*this); }

private:
  wxWindow* m_win = nullptr;
};

class wxActivateEvent : public wxEvent
{
public:
  wxActivateEvent(wxEventType type = wxEVT_NULL, bool active = true, int id = 0) : wxEvent(id, type), m_active(active)
  {}
  bool GetActive() const { return m_active; }
  wxEvent* Clone() const override { return new wxActivateEvent(*this); }

private:
  bool m_active;
};

class wxInitDialogEvent : public wxEvent
{
public:
  wxInitDialogEvent(int id = 0) : wxEvent(id, wxEVT_INIT_DIALOG) {}
  wxEvent* Clone() const override { return new wxInitDialogEvent(*this); }
};

class wxMenuEvent : public wxEvent
{
public:
  wxMenuEvent(wxEventType type = wxEVT_NULL, int winid = 0) : wxEvent(winid, type), m_menuId(winid) {}
  int GetMenuId() const { return m_menuId; }
  wxEvent* Clone() const override { return new wxMenuEvent(*this); }

private:
  int m_menuId;
};

class wxCloseEvent : public wxEvent
{
public:
  wxCloseEvent(wxEventType type = wxEVT_NULL, int winid = 0) : wxEvent(winid, type) {}
  void SetLoggingOff(bool logOff) { m_loggingOff = logOff; }
  bool GetLoggingOff() const { return m_loggingOff; }
  void Veto(bool veto = true) { m_veto = veto; }
  void SetCanVeto(bool canVeto) { m_canVeto = canVeto; }
  bool CanVeto() const { return m_canVeto; }
  bool GetVeto() const { return m_canVeto && m_veto; }
  void SetForce(bool force) { m_force = force; }
  bool GetForce() const { return m_force; }
  wxEvent* Clone() const override { return new wxCloseEvent(*this); }

private:
  bool m_loggingOff = false;
  bool m_veto = false;
  bool m_canVeto = true;
  bool m_force = false;
};

class wxShowEvent : public wxEvent
{
public:
  wxShowEvent(int winid = 0, bool show = false) : wxEvent(winid, wxEVT_SHOW), m_show(show) {}
  bool GetShow() const { return m_show; }
  wxEvent* Clone() const override { return new wxShowEvent(*this); }

private:
  bool m_show;
};

class wxIconizeEvent : public wxEvent
{
public:
  wxIconizeEvent(int winid = 0, bool iconized = true) : wxEvent(winid, wxEVT_ICONIZE), m_iconized(iconized) {}
  bool Iconized() const { return m_iconized; }
  wxEvent* Clone() const override { return new wxIconizeEvent(*this); }

private:
  bool m_iconized;
};

class wxMaximizeEvent : public wxEvent
{
public:
  wxMaximizeEvent(int winid = 0) : wxEvent(winid, wxEVT_MAXIMIZE) {}
  wxEvent* Clone() const override { return new wxMaximizeEvent(*this); }
};

class wxIdleEvent : public wxEvent
{
public:
  wxIdleEvent() : wxEvent(0, wxEVT_IDLE) {}
  void RequestMore(bool needMore = true) { m_requestMore = needMore; }
  bool MoreRequested() const { return m_requestMore; }
  wxEvent* Clone() const override { return new wxIdleEvent(*this); }

private:
  bool m_requestMore = false;
};

class wxUpdateUIEvent : public wxCommandEvent
{
public:
  wxUpdateUIEvent(int commandId = 0) : wxCommandEvent(wxEVT_UPDATE_UI, commandId) {}
  bool GetChecked() const { return m_checked; }
  bool GetEnabled() const { return m_enabled; }
  wxString GetText() const { return m_text; }
  bool GetSetText() const { return m_setText; }
  bool GetSetChecked() const { return m_setChecked; }
  bool GetSetEnabled() const { return m_setEnabled; }
  void Check(bool check)
  {
    m_checked = check;
    m_setChecked = true;
  }
  void Enable(bool enable)
  {
    m_enabled = enable;
    m_setEnabled = true;
  }
  void SetText(const wxString& text)
  {
    m_text = text;
    m_setText = true;
  }
  wxEvent* Clone() const override { return new wxUpdateUIEvent(*this); }

private:
  bool m_checked = false;
  bool m_enabled = false;
  bool m_setEnabled = false;
  bool m_setText = false;
  bool m_setChecked = false;
  wxString m_text;
};

class wxSysColourChangedEvent : public wxEvent
{
public:
  wxSysColourChangedEvent() : wxEvent(0, wxEVT_SYS_COLOUR_CHANGED) {}
  wxEvent* Clone() const override { return new wxSysColourChangedEvent(*this); }
};

class wxContextMenuEvent : public wxCommandEvent
{
public:
  wxContextMenuEvent(wxEventType type = wxEVT_NULL, int winid = 0, const wxPoint& pt = wxDefaultPosition)
    : wxCommandEvent(type, winid), m_pos(pt)
  {}
  const wxPoint& GetPosition() const { return m_pos; }
  void SetPosition(const wxPoint& pos) { m_pos = pos; }
  wxEvent* Clone() const override { return new wxContextMenuEvent(*this); }

private:
  wxPoint m_pos;
};

// --- Handler function types and event tables -------------------------------------------------
typedef void (wxObject::*wxObjectEventFunction)(wxEvent&);
typedef void (wxEvtHandler::*wxEventFunction)(wxEvent&);
typedef void (wxEvtHandler::*wxCommandEventFunction)(wxCommandEvent&);
typedef void (wxEvtHandler::*wxNotifyEventFunction)(wxNotifyEvent&);
typedef void (wxEvtHandler::*wxScrollEventFunction)(wxScrollEvent&);
typedef void (wxEvtHandler::*wxScrollWinEventFunction)(wxScrollWinEvent&);
typedef void (wxEvtHandler::*wxSizeEventFunction)(wxSizeEvent&);
typedef void (wxEvtHandler::*wxMoveEventFunction)(wxMoveEvent&);
typedef void (wxEvtHandler::*wxPaintEventFunction)(wxPaintEvent&);
typedef void (wxEvtHandler::*wxEraseEventFunction)(wxEraseEvent&);
typedef void (wxEvtHandler::*wxMouseEventFunction)(wxMouseEvent&);
typedef void (wxEvtHandler::*wxCharEventFunction)(wxKeyEvent&);
typedef void (wxEvtHandler::*wxFocusEventFunction)(wxFocusEvent&);
typedef void (wxEvtHandler::*wxActivateEventFunction)(wxActivateEvent&);
typedef void (wxEvtHandler::*wxMenuEventFunction)(wxMenuEvent&);
typedef void (wxEvtHandler::*wxCloseEventFunction)(wxCloseEvent&);
typedef void (wxEvtHandler::*wxShowEventFunction)(wxShowEvent&);
typedef void (wxEvtHandler::*wxIconizeEventFunction)(wxIconizeEvent&);
typedef void (wxEvtHandler::*wxMaximizeEventFunction)(wxMaximizeEvent&);
typedef void (wxEvtHandler::*wxIdleEventFunction)(wxIdleEvent&);
typedef void (wxEvtHandler::*wxUpdateUIEventFunction)(wxUpdateUIEvent&);
typedef void (wxEvtHandler::*wxInitDialogEventFunction)(wxInitDialogEvent&);
typedef void (wxEvtHandler::*wxSysColourChangedFunction)(wxSysColourChangedEvent&);
typedef void (wxEvtHandler::*wxSetCursorEventFunction)(wxSetCursorEvent&);
typedef void (wxEvtHandler::*wxContextMenuEventFunction)(wxContextMenuEvent&);

struct wxEventTableEntryBase
{
  wxEventTableEntryBase(int winid, int idLast, wxObjectEventFunction fn, wxObject* data)
    : m_id(winid), m_lastId(idLast), m_fn(fn), m_callbackUserData(data)
  {}

  int m_id;
  int m_lastId;
  wxObjectEventFunction m_fn;
  wxObject* m_callbackUserData;
};

// The event type is held by reference, as in wx: a table entry can name an event type that a
// later static initialiser numbers (DEFINE_EVENT_TYPE).
struct wxEventTableEntry : public wxEventTableEntryBase
{
  wxEventTableEntry(const int& evType, int winid, int idLast, wxObjectEventFunction fn, wxObject* data)
    : wxEventTableEntryBase(winid, idLast, fn, data), m_eventType(evType)
  {}

  const int& m_eventType;
};

struct wxEventTable
{
  const wxEventTable* baseTable;
  const wxEventTableEntry* entries;
};

struct wxDynamicEventTableEntry : public wxEventTableEntryBase
{
  wxDynamicEventTableEntry(int evType, int winid, int idLast, wxObjectEventFunction fn, wxObject* data)
    : wxEventTableEntryBase(winid, idLast, fn, data), m_eventType(evType)
  {}

  int m_eventType;
};

class wxEvtHandler;

namespace faf_wx_detail
{
  // Events queued by AddPendingEvent, run by wxApp::ProcessPendingEvents (wxPendingEvents).
  struct PendingEvents
  {
    std::mutex lock;
    std::deque<wxEvtHandler*> handlers;
  };

  // Never destroyed: handlers with static storage (wxDefaultValidator, engine globals) unlink
  // themselves from it in their destructors, after function statics are gone.
  inline PendingEvents& GetPendingEvents()
  {
    static PendingEvents* const sPending = new PendingEvents();
    return *sPending;
  }
} // namespace faf_wx_detail

class wxEvtHandler : public wxObject
{
public:
  wxEvtHandler() = default;
  wxEvtHandler(const wxEvtHandler&) = delete;
  wxEvtHandler& operator=(const wxEvtHandler&) = delete;

  ~wxEvtHandler() override
  {
    if (m_previousHandler != nullptr) {
      m_previousHandler->m_nextHandler = m_nextHandler;
    }
    if (m_nextHandler != nullptr) {
      m_nextHandler->m_previousHandler = m_previousHandler;
    }
    for (wxDynamicEventTableEntry* entry : m_dynamicEvents) {
      delete entry->m_callbackUserData;
      delete entry;
    }
    faf_wx_detail::PendingEvents& pending = faf_wx_detail::GetPendingEvents();
    std::lock_guard<std::mutex> guard(pending.lock);
    for (auto it = pending.handlers.begin(); it != pending.handlers.end();) {
      it = (*it == this) ? pending.handlers.erase(it) : it + 1;
    }
    std::lock_guard<std::mutex> ownGuard(m_pendingLock);
    for (wxEvent* event : m_pendingEvents) {
      delete event;
    }
  }

  wxEvtHandler* GetNextHandler() const { return m_nextHandler; }
  wxEvtHandler* GetPreviousHandler() const { return m_previousHandler; }
  void SetNextHandler(wxEvtHandler* handler) { m_nextHandler = handler; }
  void SetPreviousHandler(wxEvtHandler* handler) { m_previousHandler = handler; }
  void SetEvtHandlerEnabled(bool enabled) { m_enabled = enabled; }
  bool GetEvtHandlerEnabled() const { return m_enabled; }

  virtual bool ProcessEvent(wxEvent& event)
  {
    if (GetEvtHandlerEnabled()) {
      if (SearchDynamicEventTable(event)) {
        return true;
      }
      for (const wxEventTable* table = GetEventTable(); table != nullptr; table = table->baseTable) {
        if (SearchEventTable(*table, event)) {
          return true;
        }
      }
    }
    if (GetNextHandler() != nullptr) {
      return GetNextHandler()->ProcessEvent(event);
    }
    return TryParent(event);
  }

  // Queues a copy of the event; wxApp::ProcessPendingEvents (run from the idle handling)
  // delivers it on the main thread.
  void AddPendingEvent(wxEvent& event)
  {
    wxEvent* const copy = event.Clone();
    {
      std::lock_guard<std::mutex> guard(m_pendingLock);
      m_pendingEvents.push_back(copy);
    }
    faf_wx_detail::PendingEvents& pending = faf_wx_detail::GetPendingEvents();
    std::lock_guard<std::mutex> guard(pending.lock);
    pending.handlers.push_back(this);
  }

  void ProcessPendingEvents()
  {
    for (;;) {
      wxEvent* event = nullptr;
      {
        std::lock_guard<std::mutex> guard(m_pendingLock);
        if (m_pendingEvents.empty()) {
          return;
        }
        event = m_pendingEvents.front();
        m_pendingEvents.pop_front();
      }
      ProcessEvent(*event);
      delete event;
    }
  }

  bool SearchEventTable(const wxEventTable& table, wxEvent& event)
  {
    const wxEventType eventType = event.GetEventType();
    const int eventId = event.GetId();
    for (int i = 0; table.entries[i].m_fn != nullptr; ++i) {
      const wxEventTableEntry& entry = table.entries[i];
      if (eventType == entry.m_eventType && MatchesId(entry, eventId)) {
        if (Dispatch(entry, event)) {
          return true;
        }
      }
    }
    return false;
  }

  void Connect(int winid, int lastId, wxEventType eventType, wxObjectEventFunction func, wxObject* userData = nullptr)
  {
    m_dynamicEvents.push_back(new wxDynamicEventTableEntry(eventType, winid, lastId, func, userData));
  }
  void Connect(int winid, wxEventType eventType, wxObjectEventFunction func, wxObject* userData = nullptr)
  {
    Connect(winid, -1, eventType, func, userData);
  }
  void Connect(wxEventType eventType, wxObjectEventFunction func, wxObject* userData = nullptr)
  {
    Connect(-1, -1, eventType, func, userData);
  }
  bool Disconnect(int winid, int lastId, wxEventType eventType, wxObjectEventFunction func = nullptr, wxObject* userData = nullptr)
  {
    for (auto it = m_dynamicEvents.begin(); it != m_dynamicEvents.end(); ++it) {
      wxDynamicEventTableEntry* const entry = *it;
      if (entry->m_id == winid && entry->m_lastId == lastId && entry->m_eventType == eventType &&
          (func == nullptr || entry->m_fn == func) &&
          (userData == nullptr || entry->m_callbackUserData == userData)) {
        delete entry->m_callbackUserData;
        delete entry;
        m_dynamicEvents.erase(it);
        return true;
      }
    }
    return false;
  }
  bool Disconnect(int winid = -1, wxEventType eventType = wxEVT_NULL, wxObjectEventFunction func = nullptr, wxObject* userData = nullptr)
  {
    return Disconnect(winid, -1, eventType, func, userData);
  }
  bool Disconnect(wxEventType eventType, wxObjectEventFunction func, wxObject* userData = nullptr)
  {
    return Disconnect(-1, -1, eventType, func, userData);
  }

  void SetClientObject(wxObject* data) { m_clientObject = data; }
  wxObject* GetClientObject() const { return m_clientObject; }
  void SetClientData(void* data) { m_clientData = data; }
  void* GetClientData() const { return m_clientData; }

  // Runs one application-wide round of pending events (wxApp::ProcessPendingEvents).
  static void ProcessAllPendingEvents()
  {
    faf_wx_detail::PendingEvents& pending = faf_wx_detail::GetPendingEvents();
    for (;;) {
      wxEvtHandler* handler = nullptr;
      {
        std::lock_guard<std::mutex> guard(pending.lock);
        if (pending.handlers.empty()) {
          return;
        }
        handler = pending.handlers.front();
        pending.handlers.pop_front();
      }
      handler->ProcessPendingEvents();
    }
  }

protected:
  // A command event a window does not handle goes to its parent (wxWindow overrides this).
  virtual bool TryParent(wxEvent& event)
  {
    (void)event;
    return false;
  }

  bool SearchDynamicEventTable(wxEvent& event)
  {
    const wxEventType eventType = event.GetEventType();
    const int eventId = event.GetId();
    for (size_t i = 0; i < m_dynamicEvents.size(); ++i) {
      wxDynamicEventTableEntry* const entry = m_dynamicEvents[i];
      if (entry->m_fn != nullptr && entry->m_eventType == eventType && MatchesId(*entry, eventId)) {
        if (Dispatch(*entry, event)) {
          return true;
        }
      }
    }
    return false;
  }

  static bool MatchesId(const wxEventTableEntryBase& entry, int eventId)
  {
    const int tableId1 = entry.m_id;
    const int tableId2 = entry.m_lastId;
    return tableId1 == -1 || (tableId2 == -1 && eventId == tableId1) ||
           (tableId2 != -1 && eventId >= tableId1 && eventId <= tableId2);
  }

  bool Dispatch(const wxEventTableEntryBase& entry, wxEvent& event)
  {
    event.Skip(false);
    event.m_callbackUserData = entry.m_callbackUserData;
    (this->*((wxEventFunction)(entry.m_fn)))(event);
    return !event.GetSkipped();
  }

  inline static const wxEventTableEntry sm_eventTableEntries[] = {
    wxEventTableEntry(wxEVT_NULL, 0, 0, nullptr, nullptr)
  };
  inline static const wxEventTable sm_eventTable = {nullptr, &sm_eventTableEntries[0]};
  virtual const wxEventTable* GetEventTable() const { return &sm_eventTable; }

  wxEvtHandler* m_nextHandler = nullptr;
  wxEvtHandler* m_previousHandler = nullptr;
  std::vector<wxDynamicEventTableEntry*> m_dynamicEvents;
  std::mutex m_pendingLock;
  std::deque<wxEvent*> m_pendingEvents;
  bool m_enabled = true;
  wxObject* m_clientObject = nullptr;
  void* m_clientData = nullptr;
};

// --- Event table macros (wx/event.h) ---------------------------------------------------------
#define DECLARE_EVENT_TABLE()                                     \
private:                                                          \
  static const wxEventTableEntry sm_eventTableEntries[];          \
                                                                  \
protected:                                                        \
  static const wxEventTable sm_eventTable;                        \
  virtual const wxEventTable* GetEventTable() const;

#define BEGIN_EVENT_TABLE(theClass, baseClass)                                                    \
  const wxEventTable* theClass::GetEventTable() const { return &theClass::sm_eventTable; }        \
  const wxEventTable theClass::sm_eventTable = {&baseClass::sm_eventTable, &theClass::sm_eventTableEntries[0]}; \
  const wxEventTableEntry theClass::sm_eventTableEntries[] = {

// As in wx, the entry macro has no trailing comma; the EVT_ macros add it.
#define DECLARE_EVENT_TABLE_ENTRY(type, winid, idLastRange, fn, obj) \
  wxEventTableEntry(type, winid, idLastRange, fn, obj)

#define END_EVENT_TABLE()                              \
  DECLARE_EVENT_TABLE_ENTRY(wxEVT_NULL, 0, 0, 0, 0) \
  };

#define EVT_CUSTOM(event, winid, func) \
  DECLARE_EVENT_TABLE_ENTRY(event, winid, -1, (wxObjectEventFunction)(wxEventFunction)&func, (wxObject*)NULL),
#define EVT_CUSTOM_RANGE(event, id1, id2, func) \
  DECLARE_EVENT_TABLE_ENTRY(event, id1, id2, (wxObjectEventFunction)(wxEventFunction)&func, (wxObject*)NULL),

#define FAF_WX_EVT_ENTRY(type, id1, id2, fnType, func) \
  DECLARE_EVENT_TABLE_ENTRY(type, id1, id2, (wxObjectEventFunction)(wxEventFunction)(fnType)&func, (wxObject*)NULL),

// Window and mouse events.
#define EVT_SIZE(func) FAF_WX_EVT_ENTRY(wxEVT_SIZE, -1, -1, wxSizeEventFunction, func)
#define EVT_MOVE(func) FAF_WX_EVT_ENTRY(wxEVT_MOVE, -1, -1, wxMoveEventFunction, func)
#define EVT_CLOSE(func) FAF_WX_EVT_ENTRY(wxEVT_CLOSE_WINDOW, -1, -1, wxCloseEventFunction, func)
#define EVT_END_SESSION(func) FAF_WX_EVT_ENTRY(wxEVT_END_SESSION, -1, -1, wxCloseEventFunction, func)
#define EVT_QUERY_END_SESSION(func) FAF_WX_EVT_ENTRY(wxEVT_QUERY_END_SESSION, -1, -1, wxCloseEventFunction, func)
#define EVT_PAINT(func) FAF_WX_EVT_ENTRY(wxEVT_PAINT, -1, -1, wxPaintEventFunction, func)
#define EVT_ERASE_BACKGROUND(func) FAF_WX_EVT_ENTRY(wxEVT_ERASE_BACKGROUND, -1, -1, wxEraseEventFunction, func)
#define EVT_CHAR(func) FAF_WX_EVT_ENTRY(wxEVT_CHAR, -1, -1, wxCharEventFunction, func)
#define EVT_KEY_DOWN(func) FAF_WX_EVT_ENTRY(wxEVT_KEY_DOWN, -1, -1, wxCharEventFunction, func)
#define EVT_KEY_UP(func) FAF_WX_EVT_ENTRY(wxEVT_KEY_UP, -1, -1, wxCharEventFunction, func)
#define EVT_CHAR_HOOK(func) FAF_WX_EVT_ENTRY(wxEVT_CHAR_HOOK, -1, -1, wxCharEventFunction, func)
#define EVT_SET_FOCUS(func) FAF_WX_EVT_ENTRY(wxEVT_SET_FOCUS, -1, -1, wxFocusEventFunction, func)
#define EVT_KILL_FOCUS(func) FAF_WX_EVT_ENTRY(wxEVT_KILL_FOCUS, -1, -1, wxFocusEventFunction, func)
#define EVT_ACTIVATE(func) FAF_WX_EVT_ENTRY(wxEVT_ACTIVATE, -1, -1, wxActivateEventFunction, func)
#define EVT_ACTIVATE_APP(func) FAF_WX_EVT_ENTRY(wxEVT_ACTIVATE_APP, -1, -1, wxActivateEventFunction, func)
#define EVT_SHOW(func) FAF_WX_EVT_ENTRY(wxEVT_SHOW, -1, -1, wxShowEventFunction, func)
#define EVT_ICONIZE(func) FAF_WX_EVT_ENTRY(wxEVT_ICONIZE, -1, -1, wxIconizeEventFunction, func)
#define EVT_MAXIMIZE(func) FAF_WX_EVT_ENTRY(wxEVT_MAXIMIZE, -1, -1, wxMaximizeEventFunction, func)
#define EVT_IDLE(func) FAF_WX_EVT_ENTRY(wxEVT_IDLE, -1, -1, wxIdleEventFunction, func)
#define EVT_INIT_DIALOG(func) FAF_WX_EVT_ENTRY(wxEVT_INIT_DIALOG, -1, -1, wxInitDialogEventFunction, func)
#define EVT_SYS_COLOUR_CHANGED(func) \
  FAF_WX_EVT_ENTRY(wxEVT_SYS_COLOUR_CHANGED, -1, -1, wxSysColourChangedFunction, func)
#define EVT_SET_CURSOR(func) FAF_WX_EVT_ENTRY(wxEVT_SET_CURSOR, -1, -1, wxSetCursorEventFunction, func)
#define EVT_CONTEXT_MENU(func) FAF_WX_EVT_ENTRY(wxEVT_CONTEXT_MENU, -1, -1, wxContextMenuEventFunction, func)
#define EVT_LEFT_DOWN(func) FAF_WX_EVT_ENTRY(wxEVT_LEFT_DOWN, -1, -1, wxMouseEventFunction, func)
#define EVT_LEFT_UP(func) FAF_WX_EVT_ENTRY(wxEVT_LEFT_UP, -1, -1, wxMouseEventFunction, func)
#define EVT_MIDDLE_DOWN(func) FAF_WX_EVT_ENTRY(wxEVT_MIDDLE_DOWN, -1, -1, wxMouseEventFunction, func)
#define EVT_MIDDLE_UP(func) FAF_WX_EVT_ENTRY(wxEVT_MIDDLE_UP, -1, -1, wxMouseEventFunction, func)
#define EVT_RIGHT_DOWN(func) FAF_WX_EVT_ENTRY(wxEVT_RIGHT_DOWN, -1, -1, wxMouseEventFunction, func)
#define EVT_RIGHT_UP(func) FAF_WX_EVT_ENTRY(wxEVT_RIGHT_UP, -1, -1, wxMouseEventFunction, func)
#define EVT_MOTION(func) FAF_WX_EVT_ENTRY(wxEVT_MOTION, -1, -1, wxMouseEventFunction, func)
#define EVT_LEFT_DCLICK(func) FAF_WX_EVT_ENTRY(wxEVT_LEFT_DCLICK, -1, -1, wxMouseEventFunction, func)
#define EVT_MIDDLE_DCLICK(func) FAF_WX_EVT_ENTRY(wxEVT_MIDDLE_DCLICK, -1, -1, wxMouseEventFunction, func)
#define EVT_RIGHT_DCLICK(func) FAF_WX_EVT_ENTRY(wxEVT_RIGHT_DCLICK, -1, -1, wxMouseEventFunction, func)
#define EVT_LEAVE_WINDOW(func) FAF_WX_EVT_ENTRY(wxEVT_LEAVE_WINDOW, -1, -1, wxMouseEventFunction, func)
#define EVT_ENTER_WINDOW(func) FAF_WX_EVT_ENTRY(wxEVT_ENTER_WINDOW, -1, -1, wxMouseEventFunction, func)
#define EVT_MOUSEWHEEL(func) FAF_WX_EVT_ENTRY(wxEVT_MOUSEWHEEL, -1, -1, wxMouseEventFunction, func)
#define EVT_MOUSE_EVENTS(func) \
  EVT_LEFT_DOWN(func)          \
  EVT_LEFT_UP(func)            \
  EVT_MIDDLE_DOWN(func)        \
  EVT_MIDDLE_UP(func)          \
  EVT_RIGHT_DOWN(func)         \
  EVT_RIGHT_UP(func)           \
  EVT_MOTION(func)             \
  EVT_LEFT_DCLICK(func)        \
  EVT_MIDDLE_DCLICK(func)      \
  EVT_RIGHT_DCLICK(func)       \
  EVT_LEAVE_WINDOW(func)       \
  EVT_ENTER_WINDOW(func)       \
  EVT_MOUSEWHEEL(func)

// Scrolling.
#define EVT_SCROLL_TOP(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_TOP, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_BOTTOM(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_BOTTOM, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_LINEUP(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_LINEUP, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_LINEDOWN(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_LINEDOWN, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_PAGEUP(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_PAGEUP, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_PAGEDOWN(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_PAGEDOWN, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_THUMBTRACK(func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_THUMBTRACK, -1, -1, wxScrollEventFunction, func)
#define EVT_SCROLL_THUMBRELEASE(func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_THUMBRELEASE, -1, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_TOP(winid, func) FAF_WX_EVT_ENTRY(wxEVT_SCROLL_TOP, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_BOTTOM(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_BOTTOM, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_LINEUP(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_LINEUP, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_LINEDOWN(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_LINEDOWN, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_PAGEUP(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_PAGEUP, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_PAGEDOWN(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_PAGEDOWN, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_THUMBTRACK(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_THUMBTRACK, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_THUMBRELEASE(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_THUMBRELEASE, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL_ENDSCROLL(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_SCROLL_ENDSCROLL, winid, -1, wxScrollEventFunction, func)
#define EVT_COMMAND_SCROLL(winid, func)         \
  EVT_COMMAND_SCROLL_TOP(winid, func)           \
  EVT_COMMAND_SCROLL_BOTTOM(winid, func)        \
  EVT_COMMAND_SCROLL_LINEUP(winid, func)        \
  EVT_COMMAND_SCROLL_LINEDOWN(winid, func)      \
  EVT_COMMAND_SCROLL_PAGEUP(winid, func)        \
  EVT_COMMAND_SCROLL_PAGEDOWN(winid, func)      \
  EVT_COMMAND_SCROLL_THUMBTRACK(winid, func)    \
  EVT_COMMAND_SCROLL_THUMBRELEASE(winid, func)  \
  EVT_COMMAND_SCROLL_ENDSCROLL(winid, func)

// Command events.
#define EVT_COMMAND(winid, event, func) FAF_WX_EVT_ENTRY(event, winid, -1, wxCommandEventFunction, func)
#define EVT_COMMAND_RANGE(id1, id2, event, func) FAF_WX_EVT_ENTRY(event, id1, id2, wxCommandEventFunction, func)
#define EVT_BUTTON(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_BUTTON_CLICKED, winid, -1, wxCommandEventFunction, func)
#define EVT_CHECKBOX(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_CHECKBOX_CLICKED, winid, -1, wxCommandEventFunction, func)
#define EVT_CHOICE(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_CHOICE_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_LISTBOX(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_LISTBOX_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_LISTBOX_DCLICK(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_LISTBOX_DOUBLECLICKED, winid, -1, wxCommandEventFunction, func)
#define EVT_COMBOBOX(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_COMBOBOX_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_TEXT(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TEXT_UPDATED, winid, -1, wxCommandEventFunction, func)
#define EVT_TEXT_ENTER(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TEXT_ENTER, winid, -1, wxCommandEventFunction, func)
#define EVT_TEXT_URL(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TEXT_URL, winid, -1, wxCommandEventFunction, func)
#define EVT_TEXT_MAXLEN(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TEXT_MAXLEN, winid, -1, wxCommandEventFunction, func)
#define EVT_MENU(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_MENU_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_MENU_RANGE(id1, id2, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_MENU_SELECTED, id1, id2, wxCommandEventFunction, func)
#define EVT_SLIDER(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SLIDER_UPDATED, winid, -1, wxCommandEventFunction, func)
#define EVT_RADIOBOX(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_RADIOBOX_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_RADIOBUTTON(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_RADIOBUTTON_SELECTED, winid, -1, wxCommandEventFunction, func)
#define EVT_SCROLLBAR(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SCROLLBAR_UPDATED, winid, -1, wxCommandEventFunction, func)
#define EVT_TOOL(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TOOL_CLICKED, winid, -1, wxCommandEventFunction, func)
#define EVT_TOOL_RANGE(id1, id2, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TOOL_CLICKED, id1, id2, wxCommandEventFunction, func)
#define EVT_TOOL_RCLICKED(winid, func) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TOOL_RCLICKED, winid, -1, wxCommandEventFunction, func)
#define EVT_TOOL_ENTER(winid, func) FAF_WX_EVT_ENTRY(wxEVT_COMMAND_TOOL_ENTER, winid, -1, wxCommandEventFunction, func)
#define EVT_UPDATE_UI(winid, func) FAF_WX_EVT_ENTRY(wxEVT_UPDATE_UI, winid, -1, wxUpdateUIEventFunction, func)
#define EVT_UPDATE_UI_RANGE(id1, id2, func) \
  FAF_WX_EVT_ENTRY(wxEVT_UPDATE_UI, id1, id2, wxUpdateUIEventFunction, func)

// The menu and tool clicks share one event type in wx.
inline constexpr wxEventType wxEVT_COMMAND_TOOL_CLICKED = wxEVT_COMMAND_MENU_SELECTED;
