#pragma once

// wx stand-in: controls (see wx/defs.h). Controls keep their contents (text, check state,
// items, tree nodes, pages) so code that reads back what it wrote sees it; nothing is drawn.

#include "wx/faf/window.h"

#include <algorithm>
#include <map>
#include <memory>

class wxControlBase : public wxWindow
{
public:
  wxControlBase() = default;
};

class wxControl : public wxControlBase
{
public:
  wxControl() = default;
  wxControl(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxControlNameStr
  )
  {
    Create(parent, winid, pos, size, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxControlNameStr
  )
  {
    const bool created = wxWindow::Create(parent, winid, pos, size, style, name);
    SetValidator(validator);
    return created;
  }
  void Command(wxCommandEvent& event) { ProcessCommand(event); }
  bool ProcessCommand(wxCommandEvent& event)
  {
    event.SetEventObject(this);
    return GetEventHandler()->ProcessEvent(event);
  }

protected:
  // Sends a command event from this control, as its native notification would.
  bool FafSendCommand(wxEventType type, int intValue = 0, const wxString& text = wxString())
  {
    wxCommandEvent event(type, GetId());
    event.SetInt(intValue);
    event.SetString(text);
    return ProcessCommand(event);
  }
};

class wxPanel : public wxWindow
{
public:
  wxPanel() = default;
  wxPanel(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxTAB_TRAVERSAL | wxNO_BORDER,
    const wxString& name = wxPanelNameStr
  )
  {
    Create(parent, winid, pos, size, style, name);
  }
  void InitDialog() override { wxWindow::InitDialog(); }
  void SetDefaultItem(wxWindow* item) { m_defaultItem = item; }
  wxWindow* GetDefaultItem() const { return m_defaultItem; }

private:
  wxWindow* m_defaultItem = nullptr;
};

class wxScrolledWindow : public wxPanel
{
public:
  using wxPanel::wxPanel;
  void SetScrollRate(int xstep, int ystep)
  {
    (void)xstep;
    (void)ystep;
  }
  void Scroll(int x, int y)
  {
    (void)x;
    (void)y;
  }
};

// --- Buttons, labels, check boxes ------------------------------------------------------------
class wxButton : public wxControl
{
public:
  wxButton() = default;
  wxButton(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    Create(parent, winid, label, pos, size, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    SetLabel(label);
    return wxControl::Create(parent, winid, pos, size, style, validator, name);
  }
  void SetDefault() {}
  static wxSize GetDefaultSize() { return wxSize(75, 23); }
};

class wxBitmapButtonBase : public wxButton
{
public:
  wxBitmapButtonBase() = default;
  void SetBitmapLabel(const wxBitmap& bitmap)
  {
    m_bmpNormal = bitmap;
    OnSetBitmap();
  }
  void SetBitmapSelected(const wxBitmap& sel)
  {
    m_bmpSelected = sel;
    OnSetBitmap();
  }
  void SetBitmapFocus(const wxBitmap& focus)
  {
    m_bmpFocus = focus;
    OnSetBitmap();
  }
  void SetBitmapDisabled(const wxBitmap& disabled)
  {
    m_bmpDisabled = disabled;
    OnSetBitmap();
  }
  const wxBitmap& GetBitmapLabel() const { return m_bmpNormal; }
  const wxBitmap& GetBitmapSelected() const { return m_bmpSelected; }
  const wxBitmap& GetBitmapFocus() const { return m_bmpFocus; }
  const wxBitmap& GetBitmapDisabled() const { return m_bmpDisabled; }
  void SetMargins(int x, int y)
  {
    m_marginX = x;
    m_marginY = y;
  }
  int GetMarginX() const { return m_marginX; }
  int GetMarginY() const { return m_marginY; }

protected:
  virtual void OnSetBitmap() {}

  wxBitmap m_bmpNormal;
  wxBitmap m_bmpSelected;
  wxBitmap m_bmpFocus;
  wxBitmap m_bmpDisabled;
  int m_marginX = 0;
  int m_marginY = 0;
};

class wxBitmapButton : public wxBitmapButtonBase
{
public:
  wxBitmapButton() = default;
  wxBitmapButton(
    wxWindow* parent,
    wxWindowID winid,
    const wxBitmap& bitmap,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxBU_AUTODRAW,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    Create(parent, winid, bitmap, pos, size, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxBitmap& bitmap,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxBU_AUTODRAW,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxButtonNameStr
  )
  {
    m_bmpNormal = bitmap;
    return wxControl::Create(parent, winid, pos, size, style, validator, name);
  }
};

class wxStaticText : public wxControl
{
public:
  wxStaticText() = default;
  wxStaticText(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticTextNameStr
  )
  {
    Create(parent, winid, label, pos, size, style, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticTextNameStr
  )
  {
    SetLabel(label);
    return wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }
};

class wxStaticBox : public wxControl
{
public:
  wxStaticBox() = default;
  wxStaticBox(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxStaticBoxNameStr
  )
  {
    SetLabel(label);
    wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }
};

class wxStaticLine : public wxControl
{
public:
  wxStaticLine(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxHORIZONTAL,
    const wxString& name = L"staticLine"
  )
  {
    wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }
};

class wxStaticBitmap : public wxControl
{
public:
  wxStaticBitmap(
    wxWindow* parent,
    wxWindowID winid,
    const wxBitmap& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = L"staticBitmap"
  )
    : m_bitmap(label)
  {
    wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }
  void SetBitmap(const wxBitmap& bitmap) { m_bitmap = bitmap; }
  const wxBitmap& GetBitmap() const { return m_bitmap; }

private:
  wxBitmap m_bitmap;
};

class wxCheckBox : public wxControl
{
public:
  wxCheckBox() = default;
  wxCheckBox(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxCheckBoxNameStr
  )
  {
    Create(parent, winid, label, pos, size, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& label,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxCheckBoxNameStr
  )
  {
    SetLabel(label);
    return wxControl::Create(parent, winid, pos, size, style, validator, name);
  }
  virtual void SetValue(bool value) { m_value = value; }
  virtual bool GetValue() const { return m_value; }
  bool IsChecked() const { return GetValue(); }

private:
  bool m_value = false;
};

class wxRadioButton : public wxCheckBox
{
public:
  using wxCheckBox::wxCheckBox;
};

// --- Text --------------------------------------------------------------------------------------
class wxTextAttr
{
public:
  wxTextAttr() = default;
  wxTextAttr(const wxColour& colText, const wxColour& colBack = wxNullColour, const wxFont& font = wxNullFont)
    : m_colText(colText), m_colBack(colBack), m_font(font)
  {}
  void SetTextColour(const wxColour& colText) { m_colText = colText; }
  void SetBackgroundColour(const wxColour& colBack) { m_colBack = colBack; }
  void SetFont(const wxFont& font) { m_font = font; }
  bool HasTextColour() const { return m_colText.Ok(); }
  bool HasBackgroundColour() const { return m_colBack.Ok(); }
  bool HasFont() const { return m_font.Ok(); }
  bool IsDefault() const { return !HasTextColour() && !HasBackgroundColour() && !HasFont(); }
  const wxColour& GetTextColour() const { return m_colText; }
  const wxColour& GetBackgroundColour() const { return m_colBack; }
  const wxFont& GetFont() const { return m_font; }

private:
  wxColour m_colText;
  wxColour m_colBack;
  wxFont m_font;
};

class wxTextCtrlBase : public wxControl
{
public:
  wxTextCtrlBase() = default;
};

class wxTextCtrl : public wxTextCtrlBase
{
public:
  wxTextCtrl() = default;
  wxTextCtrl(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTextCtrlNameStr
  )
  {
    Create(parent, winid, value, pos, size, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTextCtrlNameStr
  )
  {
    m_value = value;
    return wxControl::Create(parent, winid, pos, size, style, validator, name);
  }

  // Changes notify like the native control's EN_CHANGE (wxEVT_COMMAND_TEXT_UPDATED).
  virtual wxString GetValue() const { return m_value; }
  virtual void SetValue(const wxString& value)
  {
    m_value = value;
    m_modified = false;
    m_insertionPoint = static_cast<long>(m_value.Len());
    FafSendCommand(wxEVT_COMMAND_TEXT_UPDATED, 0, m_value);
  }
  void Clear() override { SetValue(wxEmptyString); }
  virtual void WriteText(const wxString& text)
  {
    const size_t at = std::min(static_cast<size_t>(std::max(m_insertionPoint, 0L)), m_value.Len());
    m_value = m_value.Left(at) + text + m_value.Mid(at);
    m_insertionPoint = static_cast<long>(at + text.Len());
    FafSendCommand(wxEVT_COMMAND_TEXT_UPDATED, 0, m_value);
  }
  virtual void AppendText(const wxString& text)
  {
    SetInsertionPointEnd();
    WriteText(text);
  }
  virtual void Replace(long from, long to, const wxString& value)
  {
    const size_t first = std::min(static_cast<size_t>(std::max(from, 0L)), m_value.Len());
    const size_t last = std::min(static_cast<size_t>(std::max(to, from)), m_value.Len());
    m_value = m_value.Left(first) + value + m_value.Mid(last);
    FafSendCommand(wxEVT_COMMAND_TEXT_UPDATED, 0, m_value);
  }
  virtual void Remove(long from, long to) { Replace(from, to, wxEmptyString); }
  virtual int GetLineLength(long lineNo) const { return static_cast<int>(GetLineText(lineNo).Len()); }
  virtual wxString GetLineText(long lineNo) const
  {
    long line = 0;
    size_t start = 0;
    for (size_t i = 0; i <= m_value.Len(); ++i) {
      if (i == m_value.Len() || m_value[i] == L'\n') {
        if (line == lineNo) {
          return m_value.Mid(start, i - start);
        }
        ++line;
        start = i + 1;
      }
    }
    return wxString();
  }
  virtual int GetNumberOfLines() const { return static_cast<int>(m_value.Freq(L'\n')) + 1; }
  virtual bool IsModified() const { return m_modified; }
  virtual bool IsEditable() const { return !HasFlag(wxTE_READONLY); }
  virtual void GetSelection(long* from, long* to) const
  {
    if (from != nullptr) {
      *from = m_selectionFrom;
    }
    if (to != nullptr) {
      *to = m_selectionTo;
    }
  }
  virtual void SetSelection(long from, long to)
  {
    m_selectionFrom = from;
    m_selectionTo = to;
  }
  virtual void SetMaxLength(unsigned long length) { (void)length; }
  virtual void SetEditable(bool editable)
  {
    if (editable) {
      m_windowStyle &= ~static_cast<long>(wxTE_READONLY);
    } else {
      m_windowStyle |= wxTE_READONLY;
    }
  }
  virtual void SetInsertionPoint(long pos) { m_insertionPoint = pos; }
  virtual void SetInsertionPointEnd() { m_insertionPoint = static_cast<long>(m_value.Len()); }
  virtual long GetInsertionPoint() const { return m_insertionPoint; }
  virtual long GetLastPosition() const { return static_cast<long>(m_value.Len()); }
  virtual void ShowPosition(long pos) { (void)pos; }
  virtual bool SetStyle(long start, long end, const wxTextAttr& style)
  {
    (void)start;
    (void)end;
    (void)style;
    return true;
  }
  virtual bool SetDefaultStyle(const wxTextAttr& style)
  {
    m_defaultStyle = style;
    return true;
  }
  const wxTextAttr& GetDefaultStyle() const { return m_defaultStyle; }
  virtual long XYToPosition(long x, long y) const
  {
    long line = 0;
    for (size_t i = 0; i <= m_value.Len(); ++i) {
      if (line == y) {
        return static_cast<long>(i) + x;
      }
      if (i < m_value.Len() && m_value[i] == L'\n') {
        ++line;
      }
    }
    return -1;
  }
  virtual bool PositionToXY(long pos, long* x, long* y) const
  {
    long line = 0;
    long column = 0;
    for (long i = 0; i < pos && i < static_cast<long>(m_value.Len()); ++i) {
      if (m_value[static_cast<size_t>(i)] == L'\n') {
        ++line;
        column = 0;
      } else {
        ++column;
      }
    }
    if (x != nullptr) {
      *x = column;
    }
    if (y != nullptr) {
      *y = line;
    }
    return pos >= 0 && pos <= static_cast<long>(m_value.Len());
  }
  virtual void Copy() {}
  virtual void Cut() {}
  virtual void Paste() {}
  virtual void Undo() {}
  virtual void Redo() {}
  virtual bool CanCopy() const { return false; }
  virtual bool CanCut() const { return false; }
  virtual bool CanPaste() const { return false; }
  virtual bool CanUndo() const { return false; }
  virtual bool CanRedo() const { return false; }
  virtual void DiscardEdits() { m_modified = false; }
  virtual void MarkDirty() { m_modified = true; }
  virtual bool LoadFile(const wxString& file)
  {
    (void)file;
    return false;
  }
  virtual bool SaveFile(const wxString& file = wxEmptyString)
  {
    (void)file;
    return false;
  }

private:
  wxString m_value;
  wxTextAttr m_defaultStyle;
  long m_insertionPoint = 0;
  long m_selectionFrom = 0;
  long m_selectionTo = 0;
  bool m_modified = false;
};

// --- Item containers ---------------------------------------------------------------------------
class wxControlWithItems : public wxControl
{
public:
  wxControlWithItems() = default;

  int Append(const wxString& item) { return Append(item, static_cast<void*>(nullptr)); }
  int Append(const wxString& item, void* clientData)
  {
    m_items.push_back(item);
    m_clientData.push_back(clientData);
    return static_cast<int>(m_items.size()) - 1;
  }
  void Append(const wxArrayString& items)
  {
    for (size_t i = 0; i < items.GetCount(); ++i) {
      Append(items[i]);
    }
  }
  int Insert(const wxString& item, int pos)
  {
    pos = std::clamp(pos, 0, static_cast<int>(m_items.size()));
    m_items.insert(m_items.begin() + pos, item);
    m_clientData.insert(m_clientData.begin() + pos, nullptr);
    return pos;
  }
  virtual void Clear() override
  {
    m_items.clear();
    m_clientData.clear();
    m_selection = -1;
  }
  void Delete(int n)
  {
    if (n < 0 || n >= static_cast<int>(m_items.size())) {
      return;
    }
    m_items.erase(m_items.begin() + n);
    m_clientData.erase(m_clientData.begin() + n);
    if (m_selection == n) {
      m_selection = -1;
    } else if (m_selection > n) {
      --m_selection;
    }
  }
  int GetCount() const { return static_cast<int>(m_items.size()); }
  int Number() const { return GetCount(); }
  bool IsEmpty() const { return m_items.empty(); }
  wxString GetString(int n) const
  {
    return (n >= 0 && n < static_cast<int>(m_items.size())) ? m_items[static_cast<size_t>(n)] : wxString();
  }
  void SetString(int n, const wxString& s)
  {
    if (n >= 0 && n < static_cast<int>(m_items.size())) {
      m_items[static_cast<size_t>(n)] = s;
    }
  }
  int FindString(const wxString& s) const
  {
    for (size_t i = 0; i < m_items.size(); ++i) {
      if (m_items[i] == s) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }
  virtual void SetSelection(int n) { m_selection = n; }
  virtual int GetSelection() const { return m_selection; }
  void Select(int n) { SetSelection(n); }
  wxString GetStringSelection() const { return GetString(GetSelection()); }
  bool SetStringSelection(const wxString& s)
  {
    const int n = FindString(s);
    if (n < 0) {
      return false;
    }
    SetSelection(n);
    return true;
  }
  void SetClientData(int n, void* data)
  {
    if (n >= 0 && n < static_cast<int>(m_clientData.size())) {
      m_clientData[static_cast<size_t>(n)] = data;
    }
  }
  void* GetClientData(int n) const
  {
    return (n >= 0 && n < static_cast<int>(m_clientData.size())) ? m_clientData[static_cast<size_t>(n)] : nullptr;
  }

protected:
  void InitItems(int n, const wxString choices[])
  {
    for (int i = 0; i < n && choices != nullptr; ++i) {
      Append(choices[i]);
    }
  }

  std::vector<wxString> m_items;
  std::vector<void*> m_clientData;
  int m_selection = -1;
};

class wxChoice : public wxControlWithItems
{
public:
  wxChoice() = default;
  wxChoice(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    int n = 0,
    const wxString choices[] = nullptr,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxChoiceNameStr
  )
  {
    wxControl::Create(parent, winid, pos, size, style, validator, name);
    InitItems(n, choices);
  }
};

class wxListBox : public wxControlWithItems
{
public:
  wxListBox() = default;
  wxListBox(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    int n = 0,
    const wxString choices[] = nullptr,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxListBoxNameStr
  )
  {
    wxControl::Create(parent, winid, pos, size, style, validator, name);
    InitItems(n, choices);
  }
};

class wxComboBox : public wxControlWithItems
{
public:
  wxComboBox() = default;
  wxComboBox(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    int n = 0,
    const wxString choices[] = nullptr,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxComboBoxNameStr
  )
  {
    Create(parent, winid, value, pos, size, n, choices, style, validator, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxString& value = wxEmptyString,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    int n = 0,
    const wxString choices[] = nullptr,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxComboBoxNameStr
  )
  {
    const bool created = wxControl::Create(parent, winid, pos, size, style, validator, name);
    InitItems(n, choices);
    m_value = value;
    return created;
  }
  wxString GetValue() const { return m_value; }
  void SetValue(const wxString& value) { m_value = value; }
  void SetSelection(int n) override
  {
    wxControlWithItems::SetSelection(n);
    m_value = GetString(n);
  }
  void SetEditable(bool editable) { (void)editable; }
  void SetInsertionPoint(long pos) { (void)pos; }
  void SetInsertionPointEnd() {}
  long GetInsertionPoint() const { return 0; }
  long GetLastPosition() const { return static_cast<long>(m_value.Len()); }

private:
  wxString m_value;
};

class wxSlider : public wxControl
{
public:
  wxSlider() = default;
  wxSlider(
    wxWindow* parent,
    wxWindowID winid,
    int value,
    int minValue,
    int maxValue,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxSL_HORIZONTAL,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxSliderNameStr
  )
    : m_value(value), m_min(minValue), m_max(maxValue)
  {
    wxControl::Create(parent, winid, pos, size, style, validator, name);
  }
  virtual int GetValue() const { return m_value; }
  virtual void SetValue(int value) { m_value = std::clamp(value, m_min, m_max); }
  virtual void SetRange(int minValue, int maxValue)
  {
    m_min = minValue;
    m_max = maxValue;
    m_value = std::clamp(m_value, m_min, m_max);
  }
  int GetMin() const { return m_min; }
  int GetMax() const { return m_max; }
  void SetTickFreq(int n, int pos) { (void)n, (void)pos; }
  void SetPageSize(int pageSize) { (void)pageSize; }
  void SetLineSize(int lineSize) { (void)lineSize; }
  void SetThumbLength(int length) { (void)length; }
  void SetSelection(int minPos, int maxPos) { (void)minPos, (void)maxPos; }

private:
  int m_value = 0;
  int m_min = 0;
  int m_max = 100;
};

class wxGauge : public wxControl
{
public:
  wxGauge(
    wxWindow* parent,
    wxWindowID winid,
    int range,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxHORIZONTAL,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = L"gauge"
  )
    : m_range(range)
  {
    wxControl::Create(parent, winid, pos, size, style, validator, name);
  }
  void SetRange(int range) { m_range = range; }
  int GetRange() const { return m_range; }
  void SetValue(int value) { m_value = value; }
  int GetValue() const { return m_value; }

private:
  int m_range;
  int m_value = 0;
};

// --- List control (wx/listctrl.h) --------------------------------------------------------------
class wxListItemAttr
{
public:
  wxListItemAttr() = default;
  wxListItemAttr(const wxColour& colText, const wxColour& colBack, const wxFont& font)
    : m_colText(colText), m_colBack(colBack), m_font(font)
  {}
  void SetTextColour(const wxColour& colText) { m_colText = colText; }
  void SetBackgroundColour(const wxColour& colBack) { m_colBack = colBack; }
  void SetFont(const wxFont& font) { m_font = font; }
  bool HasTextColour() const { return m_colText.Ok(); }
  bool HasBackgroundColour() const { return m_colBack.Ok(); }
  bool HasFont() const { return m_font.Ok(); }
  const wxColour& GetTextColour() const { return m_colText; }
  const wxColour& GetBackgroundColour() const { return m_colBack; }
  const wxFont& GetFont() const { return m_font; }

private:
  wxColour m_colText;
  wxColour m_colBack;
  wxFont m_font;
};

class wxListItem : public wxObject
{
public:
  wxListItem() = default;
  void Clear() { *this = wxListItem(); }
  void SetMask(long mask) { m_mask = mask; }
  void SetId(long id) { m_itemId = id; }
  void SetColumn(int col) { m_col = col; }
  void SetState(long state)
  {
    m_mask |= wxLIST_MASK_STATE;
    m_state = state;
    m_stateMask |= state;
  }
  void SetStateMask(long stateMask) { m_stateMask = stateMask; }
  void SetText(const wxString& text)
  {
    m_mask |= wxLIST_MASK_TEXT;
    m_text = text;
  }
  void SetImage(int image)
  {
    m_mask |= wxLIST_MASK_IMAGE;
    m_image = image;
  }
  void SetData(long data)
  {
    m_mask |= wxLIST_MASK_DATA;
    m_data = data;
  }
  void SetData(void* data) { SetData(static_cast<long>(reinterpret_cast<std::intptr_t>(data))); }
  void SetWidth(int width)
  {
    m_mask |= wxLIST_MASK_WIDTH;
    m_width = width;
  }
  void SetAlign(wxListColumnFormat align)
  {
    m_mask |= wxLIST_MASK_FORMAT;
    m_format = align;
  }
  void SetTextColour(const wxColour& colText) { m_attr.SetTextColour(colText); }
  void SetBackgroundColour(const wxColour& colBack) { m_attr.SetBackgroundColour(colBack); }
  void SetFont(const wxFont& font) { m_attr.SetFont(font); }
  long GetMask() const { return m_mask; }
  long GetId() const { return m_itemId; }
  int GetColumn() const { return m_col; }
  long GetState() const { return m_state & m_stateMask; }
  const wxString& GetText() const { return m_text; }
  int GetImage() const { return m_image; }
  long GetData() const { return m_data; }
  int GetWidth() const { return m_width; }
  wxListColumnFormat GetAlign() const { return m_format; }

  long m_mask = 0;
  long m_itemId = 0;
  int m_col = 0;
  long m_state = 0;
  long m_stateMask = 0;
  wxString m_text;
  int m_image = -1;
  long m_data = 0;
  int m_width = 0;
  wxListColumnFormat m_format = wxLIST_FORMAT_LEFT;
  wxListItemAttr m_attr;
};

class wxListEvent : public wxNotifyEvent
{
public:
  wxListEvent(wxEventType commandType = wxEVT_NULL, int winid = 0) : wxNotifyEvent(commandType, winid) {}
  int GetKeyCode() const { return m_code; }
  long GetIndex() const { return m_itemIndex; }
  int GetColumn() const { return m_col; }
  wxPoint GetPoint() const { return m_pointDrag; }
  const wxString& GetLabel() const { return m_item.m_text; }
  const wxString& GetText() const { return m_item.m_text; }
  int GetImage() const { return m_item.m_image; }
  long GetData() const { return m_item.m_data; }
  long GetMask() const { return m_item.m_mask; }
  const wxListItem& GetItem() const { return m_item; }
  long GetCacheFrom() const { return m_oldItemIndex; }
  long GetCacheTo() const { return m_itemIndex; }
  bool IsEditCancelled() const { return m_editCancelled; }
  void SetEditCanceled(bool editCancelled) { m_editCancelled = editCancelled; }
  wxEvent* Clone() const override { return new wxListEvent(*this); }

  int m_code = 0;
  long m_oldItemIndex = 0;
  long m_itemIndex = 0;
  int m_col = 0;
  wxPoint m_pointDrag;
  wxListItem m_item;
  bool m_editCancelled = false;
};

typedef void (wxEvtHandler::*wxListEventFunction)(wxListEvent&);

#define FAF_WX_LIST_EVT(type, winid, func) FAF_WX_EVT_ENTRY(type, winid, -1, wxListEventFunction, func)
#define EVT_LIST_BEGIN_DRAG(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_BEGIN_DRAG, winid, func)
#define EVT_LIST_BEGIN_RDRAG(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_BEGIN_RDRAG, winid, func)
#define EVT_LIST_BEGIN_LABEL_EDIT(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_BEGIN_LABEL_EDIT, winid, func)
#define EVT_LIST_END_LABEL_EDIT(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_END_LABEL_EDIT, winid, func)
#define EVT_LIST_DELETE_ITEM(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_DELETE_ITEM, winid, func)
#define EVT_LIST_DELETE_ALL_ITEMS(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_DELETE_ALL_ITEMS, winid, func)
#define EVT_LIST_ITEM_SELECTED(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_SELECTED, winid, func)
#define EVT_LIST_ITEM_DESELECTED(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_DESELECTED, winid, func)
#define EVT_LIST_KEY_DOWN(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_KEY_DOWN, winid, func)
#define EVT_LIST_INSERT_ITEM(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_INSERT_ITEM, winid, func)
#define EVT_LIST_COL_CLICK(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_COL_CLICK, winid, func)
#define EVT_LIST_COL_RIGHT_CLICK(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_COL_RIGHT_CLICK, winid, func)
#define EVT_LIST_COL_BEGIN_DRAG(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_COL_BEGIN_DRAG, winid, func)
#define EVT_LIST_COL_DRAGGING(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_COL_DRAGGING, winid, func)
#define EVT_LIST_COL_END_DRAG(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_COL_END_DRAG, winid, func)
#define EVT_LIST_ITEM_RIGHT_CLICK(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_RIGHT_CLICK, winid, func)
#define EVT_LIST_ITEM_MIDDLE_CLICK(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_MIDDLE_CLICK, winid, func)
#define EVT_LIST_ITEM_ACTIVATED(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_ACTIVATED, winid, func)
#define EVT_LIST_ITEM_FOCUSED(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_ITEM_FOCUSED, winid, func)
#define EVT_LIST_CACHE_HINT(winid, func) FAF_WX_LIST_EVT(wxEVT_COMMAND_LIST_CACHE_HINT, winid, func)

class wxListCtrl : public wxControl
{
public:
  wxListCtrl() = default;
  wxListCtrl(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxLC_ICON,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxListCtrlNameStr
  )
  {
    Create(parent, winid, pos, size, style, validator, name);
  }
  ~wxListCtrl() override
  {
    if (m_ownsImageListNormal) {
      delete m_imageListNormal;
    }
    if (m_ownsImageListSmall) {
      delete m_imageListSmall;
    }
    if (m_ownsImageListState) {
      delete m_imageListState;
    }
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxLC_ICON,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxListCtrlNameStr
  )
  {
    return wxControl::Create(parent, winid, pos, size, style, validator, name);
  }

  // Columns.
  long InsertColumn(long col, const wxString& heading, int format = wxLIST_FORMAT_LEFT, int width = -1)
  {
    Column column;
    column.heading = heading;
    column.format = format;
    column.width = width;
    col = std::clamp(col, 0L, static_cast<long>(m_columns.size()));
    m_columns.insert(m_columns.begin() + col, column);
    return col;
  }
  long InsertColumn(long col, wxListItem& info)
  {
    return InsertColumn(col, info.m_text, info.m_format, (info.m_mask & wxLIST_MASK_WIDTH) != 0 ? info.m_width : -1);
  }
  bool DeleteColumn(int col)
  {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) {
      return false;
    }
    m_columns.erase(m_columns.begin() + col);
    return true;
  }
  bool DeleteAllColumns()
  {
    m_columns.clear();
    return true;
  }
  int GetColumnCount() const { return static_cast<int>(m_columns.size()); }
  bool SetColumnWidth(int col, int width)
  {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) {
      return false;
    }
    m_columns[static_cast<size_t>(col)].width = width;
    return true;
  }
  int GetColumnWidth(int col) const
  {
    return (col >= 0 && col < static_cast<int>(m_columns.size())) ? m_columns[static_cast<size_t>(col)].width : 0;
  }
  bool GetColumn(int col, wxListItem& item) const
  {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) {
      return false;
    }
    const Column& column = m_columns[static_cast<size_t>(col)];
    item.m_text = column.heading;
    item.m_width = column.width;
    item.m_format = static_cast<wxListColumnFormat>(column.format);
    return true;
  }
  bool SetColumn(int col, wxListItem& item)
  {
    if (col < 0 || col >= static_cast<int>(m_columns.size())) {
      return false;
    }
    Column& column = m_columns[static_cast<size_t>(col)];
    if ((item.m_mask & wxLIST_MASK_TEXT) != 0) {
      column.heading = item.m_text;
    }
    if ((item.m_mask & wxLIST_MASK_WIDTH) != 0) {
      column.width = item.m_width;
    }
    return true;
  }

  // Items.
  bool IsVirtual() const { return HasFlag(wxLC_VIRTUAL); }
  void SetItemCount(long count)
  {
    m_virtualCount = std::max(count, 0L);
    m_states.resize(static_cast<size_t>(m_virtualCount), 0);
  }
  int GetItemCount() const
  {
    return IsVirtual() ? static_cast<int>(m_virtualCount) : static_cast<int>(m_rows.size());
  }
  long InsertItem(long index, const wxString& label)
  {
    index = std::clamp(index, 0L, static_cast<long>(m_rows.size()));
    Row row;
    row.columns.push_back(label);
    m_rows.insert(m_rows.begin() + index, row);
    m_states.insert(m_states.begin() + std::min<long>(index, static_cast<long>(m_states.size())), 0);
    return index;
  }
  long InsertItem(long index, const wxString& label, int imageIndex)
  {
    index = InsertItem(index, label);
    m_rows[static_cast<size_t>(index)].image = imageIndex;
    return index;
  }
  long InsertItem(long index, int imageIndex) { return InsertItem(index, wxEmptyString, imageIndex); }
  long InsertItem(wxListItem& info) { return InsertItem(info.m_itemId, info.m_text, info.m_image); }
  bool SetItem(long index, int col, const wxString& label, int imageId = -1)
  {
    if (index < 0 || index >= static_cast<long>(m_rows.size()) || col < 0) {
      return false;
    }
    Row& row = m_rows[static_cast<size_t>(index)];
    if (row.columns.size() <= static_cast<size_t>(col)) {
      row.columns.resize(static_cast<size_t>(col) + 1);
    }
    row.columns[static_cast<size_t>(col)] = label;
    if (imageId != -1) {
      row.image = imageId;
    }
    return true;
  }
  bool SetItem(wxListItem& info)
  {
    if ((info.m_mask & wxLIST_MASK_STATE) != 0) {
      SetItemState(info.m_itemId, info.m_state, info.m_stateMask);
    }
    if ((info.m_mask & wxLIST_MASK_DATA) != 0) {
      SetItemData(info.m_itemId, info.m_data);
    }
    return SetItem(info.m_itemId, info.m_col, info.m_text, (info.m_mask & wxLIST_MASK_IMAGE) != 0 ? info.m_image : -1);
  }
  bool GetItem(wxListItem& info) const
  {
    if (info.m_itemId < 0 || info.m_itemId >= GetItemCount()) {
      return false;
    }
    info.m_text = GetItemTextAt(info.m_itemId, info.m_col);
    info.m_state = GetItemState(info.m_itemId, info.m_stateMask != 0 ? info.m_stateMask : ~0L);
    info.m_data = GetItemData(info.m_itemId);
    return true;
  }
  void SetItemText(long item, const wxString& text) { SetItem(item, 0, text); }
  wxString GetItemText(long item) const { return GetItemTextAt(item, 0); }
  bool SetItemData(long item, long data)
  {
    if (item < 0 || item >= static_cast<long>(m_rows.size())) {
      return false;
    }
    m_rows[static_cast<size_t>(item)].data = data;
    return true;
  }
  long GetItemData(long item) const
  {
    return (item >= 0 && item < static_cast<long>(m_rows.size())) ? m_rows[static_cast<size_t>(item)].data : 0;
  }
  bool SetItemImage(long item, int image, int selImage = -1)
  {
    (void)selImage;
    if (item < 0 || item >= static_cast<long>(m_rows.size())) {
      return false;
    }
    m_rows[static_cast<size_t>(item)].image = image;
    return true;
  }
  bool SetItemState(long item, long state, long stateMask)
  {
    if (item == -1) {
      for (long i = 0; i < static_cast<long>(m_states.size()); ++i) {
        SetItemState(i, state, stateMask);
      }
      return true;
    }
    if (item < 0 || item >= static_cast<long>(m_states.size())) {
      return false;
    }
    if (HasFlag(wxLC_SINGLE_SEL) && (stateMask & state & wxLIST_STATE_SELECTED) != 0) {
      for (long& other : m_states) {
        other &= ~static_cast<long>(wxLIST_STATE_SELECTED);
      }
    }
    long& current = m_states[static_cast<size_t>(item)];
    current = (current & ~stateMask) | (state & stateMask);
    return true;
  }
  int GetItemState(long item, long stateMask) const
  {
    return (item >= 0 && item < static_cast<long>(m_states.size()))
             ? static_cast<int>(m_states[static_cast<size_t>(item)] & stateMask)
             : 0;
  }
  long GetNextItem(long item, int geometry = wxLIST_NEXT_ALL, int state = wxLIST_STATE_DONTCARE) const
  {
    (void)geometry;
    const long count = GetItemCount();
    for (long i = item + 1; i < count; ++i) {
      if (state == wxLIST_STATE_DONTCARE || (GetItemState(i, state) & state) != 0) {
        return i;
      }
    }
    return -1;
  }
  int GetSelectedItemCount() const
  {
    return static_cast<int>(std::count_if(m_states.begin(), m_states.end(), [](long s) {
      return (s & wxLIST_STATE_SELECTED) != 0;
    }));
  }
  bool DeleteItem(long item)
  {
    if (item < 0 || item >= static_cast<long>(m_rows.size())) {
      return false;
    }
    m_rows.erase(m_rows.begin() + item);
    if (item < static_cast<long>(m_states.size())) {
      m_states.erase(m_states.begin() + item);
    }
    return true;
  }
  bool DeleteAllItems()
  {
    m_rows.clear();
    m_states.assign(IsVirtual() ? static_cast<size_t>(m_virtualCount) : 0u, 0);
    return true;
  }
  bool ClearAll()
  {
    DeleteAllItems();
    m_columns.clear();
    return true;
  }
  bool EnsureVisible(long item)
  {
    (void)item;
    return true;
  }
  void RefreshItem(long item) { (void)item; }
  void RefreshItems(long itemFrom, long itemTo)
  {
    (void)itemFrom;
    (void)itemTo;
  }
  long GetTopItem() const { return 0; }
  int GetCountPerPage() const { return 1; }
  bool ScrollList(int dx, int dy)
  {
    (void)dx;
    (void)dy;
    return true;
  }
  long FindItem(long start, const wxString& str, bool partial = false) const
  {
    for (long i = start + 1; i < GetItemCount(); ++i) {
      const wxString text = GetItemText(i);
      if (partial ? text.StartsWith(str.c_str()) : text == str) {
        return i;
      }
    }
    return -1;
  }
  long HitTest(const wxPoint& point, int& flags) const
  {
    (void)point;
    flags = wxLIST_HITTEST_NOWHERE;
    return -1;
  }
  bool GetItemRect(long item, wxRect& rect, int code = 0) const
  {
    (void)item;
    (void)code;
    rect = wxRect();
    return false;
  }
  void SetSingleStyle(long style, bool add = true)
  {
    if (add) {
      m_windowStyle |= style;
    } else {
      m_windowStyle &= ~style;
    }
  }
  wxTextCtrl* EditLabel(long item)
  {
    (void)item;
    return nullptr;
  }

  // Image lists: Set leaves ownership with the caller, Assign transfers it.
  void SetImageList(wxImageList* imageList, int which)
  {
    ImageListSlot(which) = imageList;
    OwnsImageListSlot(which) = false;
  }
  void AssignImageList(wxImageList* imageList, int which)
  {
    SetImageList(imageList, which);
    OwnsImageListSlot(which) = true;
  }
  wxImageList* GetImageList(int which) const
  {
    return which == wxIMAGE_LIST_NORMAL  ? m_imageListNormal
           : which == wxIMAGE_LIST_SMALL ? m_imageListSmall
                                         : m_imageListState;
  }

  // Virtual list hooks (wxLC_VIRTUAL).
  virtual wxString OnGetItemText(long item, long col) const
  {
    (void)item;
    (void)col;
    return wxEmptyString;
  }
  virtual int OnGetItemImage(long item) const
  {
    (void)item;
    return -1;
  }
  virtual wxListItemAttr* OnGetItemAttr(long item) const
  {
    (void)item;
    return nullptr;
  }

private:
  struct Column
  {
    wxString heading;
    int format = wxLIST_FORMAT_LEFT;
    int width = -1;
  };
  struct Row
  {
    std::vector<wxString> columns;
    long data = 0;
    int image = -1;
  };

  wxString GetItemTextAt(long item, int col) const
  {
    if (IsVirtual()) {
      return OnGetItemText(item, col);
    }
    if (item < 0 || item >= static_cast<long>(m_rows.size())) {
      return wxString();
    }
    const Row& row = m_rows[static_cast<size_t>(item)];
    return (col >= 0 && static_cast<size_t>(col) < row.columns.size()) ? row.columns[static_cast<size_t>(col)]
                                                                       : wxString();
  }
  wxImageList*& ImageListSlot(int which)
  {
    return which == wxIMAGE_LIST_NORMAL  ? m_imageListNormal
           : which == wxIMAGE_LIST_SMALL ? m_imageListSmall
                                         : m_imageListState;
  }
  bool& OwnsImageListSlot(int which)
  {
    return which == wxIMAGE_LIST_NORMAL  ? m_ownsImageListNormal
           : which == wxIMAGE_LIST_SMALL ? m_ownsImageListSmall
                                         : m_ownsImageListState;
  }

  std::vector<Column> m_columns;
  std::vector<Row> m_rows;
  std::vector<long> m_states;
  long m_virtualCount = 0;
  wxImageList* m_imageListNormal = nullptr;
  wxImageList* m_imageListSmall = nullptr;
  wxImageList* m_imageListState = nullptr;
  bool m_ownsImageListNormal = false;
  bool m_ownsImageListSmall = false;
  bool m_ownsImageListState = false;
};

class wxListView : public wxListCtrl
{
public:
  using wxListCtrl::wxListCtrl;
  void Select(long n, bool on = true)
  {
    SetItemState(n, on ? wxLIST_STATE_SELECTED : 0, wxLIST_STATE_SELECTED);
  }
  void Focus(long index) { SetItemState(index, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED); }
  long GetFirstSelected() const { return GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); }
  long GetNextSelected(long item) const { return GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); }
  bool IsSelected(long index) const { return GetItemState(index, wxLIST_STATE_SELECTED) != 0; }
};

// --- Trees (wx/treebase.h, wx/treectrl.h, contrib wx/treelistctrl.h) ---------------------------
class wxClientData
{
public:
  wxClientData() = default;
  virtual ~wxClientData() = default;
};

class wxStringClientData : public wxClientData
{
public:
  wxStringClientData() = default;
  explicit wxStringClientData(const wxString& data) : m_data(data) {}
  void SetData(const wxString& data) { m_data = data; }
  const wxString& GetData() const { return m_data; }

private:
  wxString m_data;
};

class wxTreeItemId
{
public:
  wxTreeItemId() = default;
  wxTreeItemId(void* pItem) : m_pItem(pItem) {}
  bool IsOk() const { return m_pItem != nullptr; }
  void* GetID() const { return m_pItem; }
  bool operator==(const wxTreeItemId& other) const { return m_pItem == other.m_pItem; }
  bool operator!=(const wxTreeItemId& other) const { return m_pItem != other.m_pItem; }

  void* m_pItem = nullptr;
};

class wxTreeItemData : public wxClientData
{
public:
  wxTreeItemData() = default;
  const wxTreeItemId& GetId() const { return m_pItem; }
  void SetId(const wxTreeItemId& id) { m_pItem = id; }

protected:
  wxTreeItemId m_pItem;
};

enum wxTreeItemIcon
{
  wxTreeItemIcon_Normal,
  wxTreeItemIcon_Selected,
  wxTreeItemIcon_Expanded,
  wxTreeItemIcon_SelectedExpanded,
  wxTreeItemIcon_Max
};

class wxTreeEvent : public wxNotifyEvent
{
public:
  wxTreeEvent(wxEventType commandType = wxEVT_NULL, int winid = 0) : wxNotifyEvent(commandType, winid) {}
  wxTreeItemId GetItem() const { return m_item; }
  void SetItem(const wxTreeItemId& item) { m_item = item; }
  wxTreeItemId GetOldItem() const { return m_itemOld; }
  void SetOldItem(const wxTreeItemId& item) { m_itemOld = item; }
  wxPoint GetPoint() const { return m_pointDrag; }
  void SetPoint(const wxPoint& pt) { m_pointDrag = pt; }
  const wxKeyEvent& GetKeyEvent() const { return m_evtKey; }
  int GetKeyCode() const { return m_evtKey.GetKeyCode(); }
  const wxString& GetLabel() const { return m_label; }
  void SetLabel(const wxString& label) { m_label = label; }
  bool IsEditCancelled() const { return m_editCancelled; }
  void SetEditCanceled(bool editCancelled) { m_editCancelled = editCancelled; }
  int GetCode() const { return m_evtKey.GetKeyCode(); }
  wxEvent* Clone() const override { return new wxTreeEvent(*this); }

  wxKeyEvent m_evtKey;
  wxTreeItemId m_item;
  wxTreeItemId m_itemOld;
  wxPoint m_pointDrag;
  wxString m_label;
  bool m_editCancelled = false;
};

typedef void (wxEvtHandler::*wxTreeEventFunction)(wxTreeEvent&);

#define FAF_WX_TREE_EVT(type, winid, func) FAF_WX_EVT_ENTRY(type, winid, -1, wxTreeEventFunction, func)
#define EVT_TREE_BEGIN_DRAG(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_BEGIN_DRAG, winid, func)
#define EVT_TREE_BEGIN_RDRAG(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_BEGIN_RDRAG, winid, func)
#define EVT_TREE_END_DRAG(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_END_DRAG, winid, func)
#define EVT_TREE_BEGIN_LABEL_EDIT(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_BEGIN_LABEL_EDIT, winid, func)
#define EVT_TREE_END_LABEL_EDIT(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_END_LABEL_EDIT, winid, func)
#define EVT_TREE_GET_INFO(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_GET_INFO, winid, func)
#define EVT_TREE_SET_INFO(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_SET_INFO, winid, func)
#define EVT_TREE_ITEM_EXPANDED(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_EXPANDED, winid, func)
#define EVT_TREE_ITEM_EXPANDING(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_EXPANDING, winid, func)
#define EVT_TREE_ITEM_COLLAPSED(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_COLLAPSED, winid, func)
#define EVT_TREE_ITEM_COLLAPSING(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_COLLAPSING, winid, func)
#define EVT_TREE_SEL_CHANGED(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_SEL_CHANGED, winid, func)
#define EVT_TREE_SEL_CHANGING(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_SEL_CHANGING, winid, func)
#define EVT_TREE_KEY_DOWN(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_KEY_DOWN, winid, func)
#define EVT_TREE_DELETE_ITEM(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_DELETE_ITEM, winid, func)
#define EVT_TREE_ITEM_ACTIVATED(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_ACTIVATED, winid, func)
#define EVT_TREE_ITEM_RIGHT_CLICK(winid, func) FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_RIGHT_CLICK, winid, func)
#define EVT_TREE_ITEM_MIDDLE_CLICK(winid, func) \
  FAF_WX_TREE_EVT(wxEVT_COMMAND_TREE_ITEM_MIDDLE_CLICK, winid, func)

namespace faf_wx_detail
{
  // The item store behind wxTreeCtrl and wxTreeListCtrl: nodes own their children and
  // their wxTreeItemData (deleted with the node, as the library does).
  class TreeModel
  {
  public:
    struct Node
    {
      Node* parent = nullptr;
      std::vector<std::unique_ptr<Node>> children;
      std::vector<wxString> text;
      wxTreeItemData* data = nullptr;
      int image[wxTreeItemIcon_Max] = {-1, -1, -1, -1};
      bool hasChildren = false;
      bool expanded = false;
      bool selected = false;
      bool bold = false;

      ~Node() { delete data; }
    };

    static Node* NodeOf(const wxTreeItemId& item) { return static_cast<Node*>(item.m_pItem); }

    wxTreeItemId AddRoot(const wxString& text, wxTreeItemData* data)
    {
      m_root = std::make_unique<Node>();
      m_root->text.push_back(text);
      SetData(m_root.get(), data);
      return wxTreeItemId(m_root.get());
    }
    wxTreeItemId Insert(const wxTreeItemId& parent, size_t before, const wxString& text, wxTreeItemData* data)
    {
      Node* const parentNode = NodeOf(parent);
      if (parentNode == nullptr) {
        return wxTreeItemId();
      }
      auto node = std::make_unique<Node>();
      node->parent = parentNode;
      node->text.push_back(text);
      Node* const raw = node.get();
      SetData(raw, data);
      before = std::min(before, parentNode->children.size());
      parentNode->children.insert(parentNode->children.begin() + static_cast<std::ptrdiff_t>(before), std::move(node));
      return wxTreeItemId(raw);
    }
    void Delete(const wxTreeItemId& item)
    {
      Node* const node = NodeOf(item);
      if (node == nullptr) {
        return;
      }
      if (node == m_root.get()) {
        m_root.reset();
        return;
      }
      auto& siblings = node->parent->children;
      for (auto it = siblings.begin(); it != siblings.end(); ++it) {
        if (it->get() == node) {
          siblings.erase(it);
          return;
        }
      }
    }
    void DeleteChildren(const wxTreeItemId& item)
    {
      if (Node* const node = NodeOf(item)) {
        node->children.clear();
      }
    }
    void DeleteAll() { m_root.reset(); }
    wxTreeItemId GetRoot() const { return wxTreeItemId(m_root.get()); }
    size_t GetCount() const { return m_root ? 1 + CountChildren(m_root.get(), true) : 0; }

    static void SetData(Node* node, wxTreeItemData* data)
    {
      node->data = data;
      if (data != nullptr) {
        data->SetId(wxTreeItemId(node));
      }
    }
    static wxString GetText(const wxTreeItemId& item, size_t column)
    {
      const Node* const node = NodeOf(item);
      return (node != nullptr && column < node->text.size()) ? node->text[column] : wxString();
    }
    static void SetText(const wxTreeItemId& item, size_t column, const wxString& text)
    {
      if (Node* const node = NodeOf(item)) {
        if (node->text.size() <= column) {
          node->text.resize(column + 1);
        }
        node->text[column] = text;
      }
    }
    static size_t CountChildren(const Node* node, bool recursively)
    {
      size_t count = node->children.size();
      if (recursively) {
        for (const auto& child : node->children) {
          count += CountChildren(child.get(), true);
        }
      }
      return count;
    }
    static wxTreeItemId ChildAt(const wxTreeItemId& item, size_t index)
    {
      const Node* const node = NodeOf(item);
      return (node != nullptr && index < node->children.size()) ? wxTreeItemId(node->children[index].get())
                                                                : wxTreeItemId();
    }
    static wxTreeItemId Sibling(const wxTreeItemId& item, int offset)
    {
      const Node* const node = NodeOf(item);
      if (node == nullptr || node->parent == nullptr) {
        return wxTreeItemId();
      }
      const auto& siblings = node->parent->children;
      for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == node) {
          const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(i) + offset;
          return (target >= 0 && target < static_cast<std::ptrdiff_t>(siblings.size()))
                   ? wxTreeItemId(siblings[static_cast<size_t>(target)].get())
                   : wxTreeItemId();
        }
      }
      return wxTreeItemId();
    }
    static void SortChildren(const wxTreeItemId& item)
    {
      if (Node* const node = NodeOf(item)) {
        std::stable_sort(node->children.begin(), node->children.end(), [](const auto& a, const auto& b) {
          const wxString textA = a->text.empty() ? wxString() : a->text[0];
          const wxString textB = b->text.empty() ? wxString() : b->text[0];
          return textA.Cmp(textB) < 0;
        });
      }
    }
    void ClearSelection(Node* node = nullptr)
    {
      if (node == nullptr) {
        node = m_root.get();
      }
      if (node == nullptr) {
        return;
      }
      node->selected = false;
      for (const auto& child : node->children) {
        ClearSelection(child.get());
      }
    }
    wxTreeItemId FindSelected(Node* node = nullptr) const
    {
      if (node == nullptr) {
        node = m_root.get();
      }
      if (node == nullptr) {
        return wxTreeItemId();
      }
      if (node->selected) {
        return wxTreeItemId(node);
      }
      for (const auto& child : node->children) {
        const wxTreeItemId found = FindSelected(child.get());
        if (found.IsOk()) {
          return found;
        }
      }
      return wxTreeItemId();
    }

  private:
    std::unique_ptr<Node> m_root;
  };
} // namespace faf_wx_detail

// The tree API shared by wxTreeCtrl and wxTreeListCtrl (the latter adds a column argument).
template <class Base>
class wxFafTreeCtrlBase : public Base
{
public:
  using Node = faf_wx_detail::TreeModel::Node;

  ~wxFafTreeCtrlBase() override
  {
    if (m_ownsImageList) {
      delete m_imageList;
    }
    if (m_ownsStateImageList) {
      delete m_stateImageList;
    }
  }

  size_t GetCount() const { return m_model.GetCount(); }
  wxTreeItemId AddRoot(const wxString& text, int image = -1, int selectedImage = -1, wxTreeItemData* data = nullptr)
  {
    const wxTreeItemId root = m_model.AddRoot(text, data);
    SetImages(root, image, selectedImage);
    return root;
  }
  wxTreeItemId AppendItem(
    const wxTreeItemId& parent,
    const wxString& text,
    int image = -1,
    int selectedImage = -1,
    wxTreeItemData* data = nullptr
  )
  {
    const Node* const parentNode = faf_wx_detail::TreeModel::NodeOf(parent);
    const size_t at = parentNode != nullptr ? parentNode->children.size() : 0;
    const wxTreeItemId item = m_model.Insert(parent, at, text, data);
    SetImages(item, image, selectedImage);
    return item;
  }
  wxTreeItemId PrependItem(
    const wxTreeItemId& parent,
    const wxString& text,
    int image = -1,
    int selectedImage = -1,
    wxTreeItemData* data = nullptr
  )
  {
    const wxTreeItemId item = m_model.Insert(parent, 0, text, data);
    SetImages(item, image, selectedImage);
    return item;
  }
  wxTreeItemId InsertItem(
    const wxTreeItemId& parent,
    size_t before,
    const wxString& text,
    int image = -1,
    int selectedImage = -1,
    wxTreeItemData* data = nullptr
  )
  {
    const wxTreeItemId item = m_model.Insert(parent, before, text, data);
    SetImages(item, image, selectedImage);
    return item;
  }
  void Delete(const wxTreeItemId& item) { m_model.Delete(item); }
  void DeleteChildren(const wxTreeItemId& item) { m_model.DeleteChildren(item); }
  void DeleteAllItems() { m_model.DeleteAll(); }

  void Expand(const wxTreeItemId& item)
  {
    Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    if (node == nullptr || node->expanded) {
      return;
    }
    if (!SendTreeEvent(wxEVT_COMMAND_TREE_ITEM_EXPANDING, item)) {
      return;
    }
    node->expanded = true;
    SendTreeEvent(wxEVT_COMMAND_TREE_ITEM_EXPANDED, item);
  }
  void Collapse(const wxTreeItemId& item)
  {
    Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    if (node == nullptr || !node->expanded) {
      return;
    }
    if (!SendTreeEvent(wxEVT_COMMAND_TREE_ITEM_COLLAPSING, item)) {
      return;
    }
    node->expanded = false;
    SendTreeEvent(wxEVT_COMMAND_TREE_ITEM_COLLAPSED, item);
  }
  void CollapseAndReset(const wxTreeItemId& item)
  {
    Collapse(item);
    DeleteChildren(item);
  }
  void Toggle(const wxTreeItemId& item)
  {
    if (IsExpanded(item)) {
      Collapse(item);
    } else {
      Expand(item);
    }
  }
  void EnsureVisible(const wxTreeItemId& item) { (void)item; }
  void ScrollTo(const wxTreeItemId& item) { (void)item; }
  void SelectItem(const wxTreeItemId& item)
  {
    Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    if (node == nullptr) {
      return;
    }
    m_model.ClearSelection();
    node->selected = true;
  }
  void Unselect() { m_model.ClearSelection(); }
  void UnselectAll() { m_model.ClearSelection(); }
  wxTreeItemId GetSelection() const { return m_model.FindSelected(); }
  wxTreeItemId GetRootItem() const { return m_model.GetRoot(); }

  wxTreeItemData* GetItemData(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr ? node->data : nullptr;
  }
  void SetItemData(const wxTreeItemId& item, wxTreeItemData* data)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      faf_wx_detail::TreeModel::SetData(node, data);
    }
  }
  void SetItemHasChildren(const wxTreeItemId& item, bool has = true)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      node->hasChildren = has;
    }
  }
  bool ItemHasChildren(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr && (node->hasChildren || !node->children.empty());
  }
  bool HasChildren(const wxTreeItemId& item) const { return ItemHasChildren(item); }
  size_t GetChildrenCount(const wxTreeItemId& item, bool recursively = true) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr ? faf_wx_detail::TreeModel::CountChildren(node, recursively) : 0;
  }
  bool IsExpanded(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr && node->expanded;
  }
  bool IsSelected(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr && node->selected;
  }
  bool IsVisible(const wxTreeItemId& item) const
  {
    for (const Node* node = faf_wx_detail::TreeModel::NodeOf(item); node != nullptr && node->parent != nullptr;
         node = node->parent) {
      if (!node->parent->expanded) {
        return false;
      }
    }
    return item.IsOk();
  }
  bool IsBold(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr && node->bold;
  }
  void SetItemBold(const wxTreeItemId& item, bool bold = true)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      node->bold = bold;
    }
  }
  void SetItemTextColour(const wxTreeItemId& item, const wxColour& colour)
  {
    (void)item;
    (void)colour;
  }
  void SetItemBackgroundColour(const wxTreeItemId& item, const wxColour& colour)
  {
    (void)item;
    (void)colour;
  }
  void SetItemFont(const wxTreeItemId& item, const wxFont& font)
  {
    (void)item;
    (void)font;
  }

  wxTreeItemId GetItemParent(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr ? wxTreeItemId(node->parent) : wxTreeItemId();
  }
  wxTreeItemId GetParent(const wxTreeItemId& item) const { return GetItemParent(item); }
  wxWindow* GetParent() const { return Base::GetParent(); }
  wxTreeItemId GetFirstChild(const wxTreeItemId& item, long& cookie) const
  {
    cookie = 0;
    return GetNextChild(item, cookie);
  }
  wxTreeItemId GetNextChild(const wxTreeItemId& item, long& cookie) const
  {
    return faf_wx_detail::TreeModel::ChildAt(item, static_cast<size_t>(cookie++));
  }
  wxTreeItemId GetFirstChild(const wxTreeItemId& item, void*& cookie) const
  {
    cookie = nullptr;
    return GetNextChild(item, cookie);
  }
  wxTreeItemId GetNextChild(const wxTreeItemId& item, void*& cookie) const
  {
    auto index = reinterpret_cast<std::uintptr_t>(cookie);
    const wxTreeItemId child = faf_wx_detail::TreeModel::ChildAt(item, static_cast<size_t>(index));
    cookie = reinterpret_cast<void*>(index + 1);
    return child;
  }
  wxTreeItemId GetLastChild(const wxTreeItemId& item) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return (node != nullptr && !node->children.empty()) ? wxTreeItemId(node->children.back().get())
                                                        : wxTreeItemId();
  }
  wxTreeItemId GetNextSibling(const wxTreeItemId& item) const { return faf_wx_detail::TreeModel::Sibling(item, 1); }
  wxTreeItemId GetPrevSibling(const wxTreeItemId& item) const { return faf_wx_detail::TreeModel::Sibling(item, -1); }

  void SortChildren(const wxTreeItemId& item) { faf_wx_detail::TreeModel::SortChildren(item); }

  void SetImageList(wxImageList* imageList)
  {
    if (m_ownsImageList) {
      delete m_imageList;
    }
    m_imageList = imageList;
    m_ownsImageList = false;
  }
  void AssignImageList(wxImageList* imageList)
  {
    SetImageList(imageList);
    m_ownsImageList = true;
  }
  void SetStateImageList(wxImageList* imageList)
  {
    if (m_ownsStateImageList) {
      delete m_stateImageList;
    }
    m_stateImageList = imageList;
    m_ownsStateImageList = false;
  }
  void AssignStateImageList(wxImageList* imageList)
  {
    SetStateImageList(imageList);
    m_ownsStateImageList = true;
  }
  wxImageList* GetImageList() const { return m_imageList; }
  wxImageList* GetStateImageList() const { return m_stateImageList; }
  void SetIndent(unsigned int indent) { m_indent = indent; }
  unsigned int GetIndent() const { return m_indent; }
  wxTreeItemId HitTest(const wxPoint& point, int& flags) const
  {
    (void)point;
    flags = 0;
    return wxTreeItemId();
  }
  bool GetBoundingRect(const wxTreeItemId& item, wxRect& rect, bool textOnly = false) const
  {
    (void)item;
    (void)textOnly;
    rect = wxRect();
    return false;
  }

protected:
  void SetImages(const wxTreeItemId& item, int image, int selectedImage)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      node->image[wxTreeItemIcon_Normal] = image;
      node->image[wxTreeItemIcon_Selected] = selectedImage;
    }
  }
  // Notifications for expand/collapse, as the native control sends them; false when vetoed.
  bool SendTreeEvent(wxEventType type, const wxTreeItemId& item)
  {
    wxTreeEvent event(type, this->GetId());
    event.SetEventObject(this);
    event.SetItem(item);
    this->GetEventHandler()->ProcessEvent(event);
    return event.IsAllowed();
  }

  faf_wx_detail::TreeModel m_model;
  wxImageList* m_imageList = nullptr;
  wxImageList* m_stateImageList = nullptr;
  bool m_ownsImageList = false;
  bool m_ownsStateImageList = false;
  unsigned int m_indent = 15;
};

class wxTreeCtrl : public wxFafTreeCtrlBase<wxControl>
{
public:
  wxTreeCtrl() = default;
  wxTreeCtrl(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxTR_DEFAULT_STYLE,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTreeCtrlNameStr
  )
  {
    Create(parent, winid, pos, size, style, validator, name);
  }

  wxString GetItemText(const wxTreeItemId& item) const { return faf_wx_detail::TreeModel::GetText(item, 0); }
  void SetItemText(const wxTreeItemId& item, const wxString& text)
  {
    faf_wx_detail::TreeModel::SetText(item, 0, text);
  }
  int GetItemImage(const wxTreeItemId& item, wxTreeItemIcon which = wxTreeItemIcon_Normal) const
  {
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr ? node->image[which] : -1;
  }
  void SetItemImage(const wxTreeItemId& item, int image, wxTreeItemIcon which = wxTreeItemIcon_Normal)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      node->image[which] = image;
    }
  }
  wxTextCtrl* EditLabel(const wxTreeItemId& item)
  {
    (void)item;
    return nullptr;
  }
  void EndEditLabel(const wxTreeItemId& item, bool discardChanges = false)
  {
    (void)item;
    (void)discardChanges;
  }
  virtual int OnCompareItems(const wxTreeItemId& item1, const wxTreeItemId& item2)
  {
    return GetItemText(item1).Cmp(GetItemText(item2));
  }
};

// wxTreeListCtrl from wx's contrib (gizmos). The engine passes a bool as AddColumn's third
// argument; it is the alignment/flag lane of the vendored version, accepted as an int.
#define DEFAULT_COL_WIDTH 100

enum wxTreeListColumnAlign
{
  wxTL_ALIGN_LEFT,
  wxTL_ALIGN_RIGHT,
  wxTL_ALIGN_CENTER
};

class wxTreeListColumnInfo
{
public:
  wxTreeListColumnInfo(const wxString& text = wxEmptyString, int image = -1, size_t width = DEFAULT_COL_WIDTH, int alignment = 0)
    : m_text(text), m_image(image), m_width(width), m_alignment(alignment)
  {}
  const wxString& GetText() const { return m_text; }
  size_t GetWidth() const { return m_width; }
  int GetAlignment() const { return m_alignment; }
  int GetImage() const { return m_image; }
  wxTreeListColumnInfo& SetText(const wxString& text)
  {
    m_text = text;
    return *this;
  }
  wxTreeListColumnInfo& SetWidth(size_t width)
  {
    m_width = width;
    return *this;
  }

private:
  wxString m_text;
  int m_image;
  size_t m_width;
  int m_alignment;
};

class wxTreeListCtrl : public wxFafTreeCtrlBase<wxControl>
{
public:
  wxTreeListCtrl() = default;
  wxTreeListCtrl(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxTR_DEFAULT_STYLE,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxTreeListCtrlNameStr
  )
  {
    Create(parent, winid, pos, size, style, validator, name);
  }

  void AddColumn(const wxString& text) { AddColumn(wxTreeListColumnInfo(text)); }
  void AddColumn(const wxString& text, size_t width, int alignment = 0)
  {
    AddColumn(wxTreeListColumnInfo(text, -1, width, alignment));
  }
  void AddColumn(const wxTreeListColumnInfo& column) { m_columns.push_back(column); }
  void InsertColumn(size_t before, const wxString& text) { InsertColumn(before, wxTreeListColumnInfo(text)); }
  void InsertColumn(size_t before, const wxTreeListColumnInfo& column)
  {
    before = std::min(before, m_columns.size());
    m_columns.insert(m_columns.begin() + static_cast<std::ptrdiff_t>(before), column);
  }
  void RemoveColumn(size_t column)
  {
    if (column < m_columns.size()) {
      m_columns.erase(m_columns.begin() + static_cast<std::ptrdiff_t>(column));
    }
  }
  size_t GetColumnCount() const { return m_columns.size(); }
  void SetColumnWidth(size_t column, size_t width)
  {
    if (column < m_columns.size()) {
      m_columns[column].SetWidth(width);
    }
  }
  int GetColumnWidth(size_t column) const
  {
    return column < m_columns.size() ? static_cast<int>(m_columns[column].GetWidth()) : 0;
  }
  void SetMainColumn(size_t column) { m_mainColumn = column; }
  size_t GetMainColumn() const { return m_mainColumn; }
  void SetColumnText(size_t column, const wxString& text)
  {
    if (column < m_columns.size()) {
      m_columns[column].SetText(text);
    }
  }
  wxString GetColumnText(size_t column) const
  {
    return column < m_columns.size() ? m_columns[column].GetText() : wxString();
  }

  wxString GetItemText(const wxTreeItemId& item) const { return GetItemText(item, m_mainColumn); }
  wxString GetItemText(const wxTreeItemId& item, size_t column) const
  {
    return faf_wx_detail::TreeModel::GetText(item, column);
  }
  void SetItemText(const wxTreeItemId& item, const wxString& text) { SetItemText(item, m_mainColumn, text); }
  void SetItemText(const wxTreeItemId& item, size_t column, const wxString& text)
  {
    faf_wx_detail::TreeModel::SetText(item, column, text);
  }
  int GetItemImage(const wxTreeItemId& item, size_t column = 0, wxTreeItemIcon which = wxTreeItemIcon_Normal) const
  {
    (void)column;
    const Node* const node = faf_wx_detail::TreeModel::NodeOf(item);
    return node != nullptr ? node->image[which] : -1;
  }
  void SetItemImage(const wxTreeItemId& item, int image, wxTreeItemIcon which = wxTreeItemIcon_Normal)
  {
    if (Node* const node = faf_wx_detail::TreeModel::NodeOf(item)) {
      node->image[which] = image;
    }
  }
  void EditLabel(const wxTreeItemId& item) { EditLabel(item, m_mainColumn); }
  void EditLabel(const wxTreeItemId& item, int column)
  {
    (void)item;
    (void)column;
  }
  wxWindow* GetHeaderWindow() const { return nullptr; }
  wxWindow* GetMainWindow() const { return nullptr; }

private:
  std::vector<wxTreeListColumnInfo> m_columns;
  size_t m_mainColumn = 0;
};

// --- Notebook (wx/notebook.h) ------------------------------------------------------------------
typedef wxWindow wxNotebookPage;

class wxNotebookEvent : public wxNotifyEvent
{
public:
  wxNotebookEvent(wxEventType commandType = wxEVT_NULL, int winid = 0, int nSel = -1, int nOldSel = -1)
    : wxNotifyEvent(commandType, winid), m_nSel(nSel), m_nOldSel(nOldSel)
  {}
  int GetSelection() const { return m_nSel; }
  void SetSelection(int nSel) { m_nSel = nSel; }
  int GetOldSelection() const { return m_nOldSel; }
  void SetOldSelection(int nOldSel) { m_nOldSel = nOldSel; }
  wxEvent* Clone() const override { return new wxNotebookEvent(*this); }

private:
  int m_nSel;
  int m_nOldSel;
};

typedef void (wxEvtHandler::*wxNotebookEventFunction)(wxNotebookEvent&);
#define EVT_NOTEBOOK_PAGE_CHANGED(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, winid, -1, wxNotebookEventFunction, fn)
#define EVT_NOTEBOOK_PAGE_CHANGING(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGING, winid, -1, wxNotebookEventFunction, fn)

class wxNotebookBase : public wxControl
{
public:
  wxNotebookBase() = default;
};

class wxNotebook : public wxNotebookBase
{
public:
  wxNotebook() = default;
  wxNotebook(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxNotebookNameStr
  )
  {
    Create(parent, winid, pos, size, style, name);
  }
  ~wxNotebook() override
  {
    if (m_ownsImageList) {
      delete m_imageList;
    }
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxNotebookNameStr
  )
  {
    return wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }

  int GetPageCount() const { return static_cast<int>(m_pages.size()); }
  wxNotebookPage* GetPage(int page) const
  {
    return (page >= 0 && page < GetPageCount()) ? m_pages[static_cast<size_t>(page)].window : nullptr;
  }
  int GetSelection() const { return m_selection; }
  virtual int SetSelection(int page)
  {
    if (page < 0 || page >= GetPageCount() || page == m_selection) {
      return m_selection;
    }
    const int old = m_selection;
    wxNotebookEvent changing(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGING, GetId(), page, old);
    changing.SetEventObject(this);
    GetEventHandler()->ProcessEvent(changing);
    if (!changing.IsAllowed()) {
      return old;
    }
    m_selection = page;
    wxNotebookEvent changed(wxEVT_COMMAND_NOTEBOOK_PAGE_CHANGED, GetId(), page, old);
    changed.SetEventObject(this);
    GetEventHandler()->ProcessEvent(changed);
    return old;
  }
  void AdvanceSelection(bool forward = true)
  {
    if (GetPageCount() == 0) {
      return;
    }
    const int count = GetPageCount();
    SetSelection(((m_selection < 0 ? 0 : m_selection) + (forward ? 1 : count - 1)) % count);
  }
  bool SetPageText(int page, const wxString& text)
  {
    if (page < 0 || page >= GetPageCount()) {
      return false;
    }
    m_pages[static_cast<size_t>(page)].text = text;
    return true;
  }
  wxString GetPageText(int page) const
  {
    return (page >= 0 && page < GetPageCount()) ? m_pages[static_cast<size_t>(page)].text : wxString();
  }
  bool SetPageImage(int page, int image)
  {
    if (page < 0 || page >= GetPageCount()) {
      return false;
    }
    m_pages[static_cast<size_t>(page)].image = image;
    return true;
  }
  int GetPageImage(int page) const
  {
    return (page >= 0 && page < GetPageCount()) ? m_pages[static_cast<size_t>(page)].image : -1;
  }
  int GetRowCount() const { return 1; }
  void SetPageSize(const wxSize& size) { (void)size; }
  void SetPadding(const wxSize& padding) { (void)padding; }

  bool AddPage(wxNotebookPage* page, const wxString& text, bool select = false, int imageId = -1)
  {
    return InsertPage(GetPageCount(), page, text, select, imageId);
  }
  virtual bool InsertPage(int position, wxNotebookPage* page, const wxString& text, bool select = false, int imageId = -1)
  {
    if (page == nullptr) {
      return false;
    }
    position = std::clamp(position, 0, GetPageCount());
    m_pages.insert(m_pages.begin() + position, Page{page, text, imageId});
    if (m_selection >= position) {
      ++m_selection;
    }
    if (select || m_selection < 0) {
      SetSelection(position);
    }
    return true;
  }
  // DeletePage deletes the window, RemovePage only takes it out.
  bool DeletePage(int page)
  {
    wxNotebookPage* const window = TakePage(page);
    if (window == nullptr) {
      return false;
    }
    delete window;
    return true;
  }
  bool RemovePage(int page) { return TakePage(page) != nullptr; }
  bool DeleteAllPages()
  {
    while (GetPageCount() > 0) {
      DeletePage(GetPageCount() - 1);
    }
    return true;
  }

  void SetImageList(wxImageList* imageList)
  {
    if (m_ownsImageList) {
      delete m_imageList;
    }
    m_imageList = imageList;
    m_ownsImageList = false;
  }
  void AssignImageList(wxImageList* imageList)
  {
    SetImageList(imageList);
    m_ownsImageList = true;
  }
  wxImageList* GetImageList() const { return m_imageList; }

private:
  struct Page
  {
    wxNotebookPage* window;
    wxString text;
    int image;
  };

  wxNotebookPage* TakePage(int page)
  {
    if (page < 0 || page >= GetPageCount()) {
      return nullptr;
    }
    wxNotebookPage* const window = m_pages[static_cast<size_t>(page)].window;
    m_pages.erase(m_pages.begin() + page);
    if (m_selection == page) {
      m_selection = m_pages.empty() ? -1 : std::min(page, GetPageCount() - 1);
    } else if (m_selection > page) {
      --m_selection;
    }
    return window;
  }

  std::vector<Page> m_pages;
  int m_selection = -1;
  wxImageList* m_imageList = nullptr;
  bool m_ownsImageList = false;
};

// --- Splitter (wx/splitter.h) ------------------------------------------------------------------
enum
{
  wxSPLIT_HORIZONTAL = 1,
  wxSPLIT_VERTICAL
};

class wxSplitterEvent : public wxNotifyEvent
{
public:
  wxSplitterEvent(wxEventType type = wxEVT_NULL, wxWindow* splitter = nullptr)
    : wxNotifyEvent(type, splitter != nullptr ? splitter->GetId() : 0)
  {
    SetEventObject(splitter);
  }
  void SetSashPosition(int pos) { m_sashPos = pos; }
  int GetSashPosition() const { return m_sashPos; }
  wxWindow* GetWindowBeingRemoved() const { return m_windowRemoved; }
  int GetX() const { return m_x; }
  int GetY() const { return m_y; }
  wxEvent* Clone() const override { return new wxSplitterEvent(*this); }

  int m_sashPos = 0;
  wxWindow* m_windowRemoved = nullptr;
  int m_x = 0;
  int m_y = 0;
};

typedef void (wxEvtHandler::*wxSplitterEventFunction)(wxSplitterEvent&);
#define EVT_SPLITTER_SASH_POS_CHANGED(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGED, winid, -1, wxSplitterEventFunction, fn)
#define EVT_SPLITTER_SASH_POS_CHANGING(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SPLITTER_SASH_POS_CHANGING, winid, -1, wxSplitterEventFunction, fn)
#define EVT_SPLITTER_DCLICK(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SPLITTER_DOUBLECLICKED, winid, -1, wxSplitterEventFunction, fn)
#define EVT_SPLITTER_UNSPLIT(winid, fn) \
  FAF_WX_EVT_ENTRY(wxEVT_COMMAND_SPLITTER_UNSPLIT, winid, -1, wxSplitterEventFunction, fn)

class wxSplitterWindow : public wxWindow
{
public:
  wxSplitterWindow() = default;
  wxSplitterWindow(
    wxWindow* parent,
    wxWindowID winid = -1,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxSP_3D,
    const wxString& name = wxSplitterNameStr
  )
  {
    Create(parent, winid, pos, size, style, name);
  }

  wxWindow* GetWindow1() const { return m_window1; }
  wxWindow* GetWindow2() const { return m_window2; }
  int GetSplitMode() const { return m_splitMode; }
  void SetSplitMode(int mode) { m_splitMode = mode; }
  void Initialize(wxWindow* window)
  {
    m_window1 = window;
    m_window2 = nullptr;
  }
  virtual bool SplitVertically(wxWindow* window1, wxWindow* window2, int sashPosition = 0)
  {
    return Split(window1, window2, sashPosition, wxSPLIT_VERTICAL);
  }
  virtual bool SplitHorizontally(wxWindow* window1, wxWindow* window2, int sashPosition = 0)
  {
    return Split(window1, window2, sashPosition, wxSPLIT_HORIZONTAL);
  }
  bool Unsplit(wxWindow* toRemove = nullptr)
  {
    if (!IsSplit()) {
      return false;
    }
    if (toRemove == nullptr || toRemove == m_window2) {
      m_window2 = nullptr;
    } else if (toRemove == m_window1) {
      m_window1 = m_window2;
      m_window2 = nullptr;
    } else {
      return false;
    }
    return true;
  }
  bool ReplaceWindow(wxWindow* windowOld, wxWindow* windowNew)
  {
    if (windowOld == m_window1) {
      m_window1 = windowNew;
    } else if (windowOld == m_window2) {
      m_window2 = windowNew;
    } else {
      return false;
    }
    return true;
  }
  bool IsSplit() const { return m_window2 != nullptr; }
  void SetSashSize(int width) { m_sashSize = width; }
  int GetSashSize() const { return m_sashSize; }
  void SetBorderSize(int width) { (void)width; }
  void SetSashPosition(int position, bool redraw = true)
  {
    (void)redraw;
    m_sashPosition = position;
  }
  int GetSashPosition() const { return m_sashPosition; }
  void SetMinimumPaneSize(int min) { m_minimumPaneSize = min; }
  int GetMinimumPaneSize() const { return m_minimumPaneSize; }
  void SizeWindows() {}

private:
  bool Split(wxWindow* window1, wxWindow* window2, int sashPosition, int mode)
  {
    if (IsSplit()) {
      return false;
    }
    m_window1 = window1;
    m_window2 = window2;
    m_splitMode = mode;
    const int extent = mode == wxSPLIT_VERTICAL ? GetClientSize().x : GetClientSize().y;
    m_sashPosition = sashPosition > 0 ? sashPosition : sashPosition < 0 ? extent + sashPosition : extent / 2;
    return true;
  }

  wxWindow* m_window1 = nullptr;
  wxWindow* m_window2 = nullptr;
  int m_splitMode = wxSPLIT_VERTICAL;
  int m_sashPosition = 0;
  int m_sashSize = 7;
  int m_minimumPaneSize = 0;
};

// --- Directory tree (wx/dirctrl.h) ------------------------------------------------------------
class wxGenericDirCtrl : public wxControl
{
public:
  wxGenericDirCtrl() = default;
  wxGenericDirCtrl(
    wxWindow* parent,
    const wxWindowID winid = -1,
    const wxString& dir = wxDirDialogDefaultFolderStr,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDIRCTRL_3D_INTERNAL | wxSUNKEN_BORDER,
    const wxString& filter = wxEmptyString,
    int defaultFilter = 0,
    const wxString& name = wxTreeCtrlNameStr
  )
    : m_defaultPath(dir), m_path(dir), m_filter(filter), m_filterIndex(defaultFilter)
  {
    wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
    m_treeCtrl = new wxTreeCtrl(this, NewControlId(), wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE);
  }
  wxTreeCtrl* GetTreeCtrl() const { return m_treeCtrl; }
  bool ExpandPath(const wxString& path)
  {
    m_path = path;
    return true;
  }
  wxString GetDefaultPath() const { return m_defaultPath; }
  void SetDefaultPath(const wxString& path) { m_defaultPath = path; }
  wxString GetPath() const { return m_path; }
  wxString GetFilePath() const { return HasFlag(wxDIRCTRL_DIR_ONLY) ? wxString() : m_path; }
  void SetPath(const wxString& path) { m_path = path; }
  void ShowHidden(bool show) { (void)show; }
  bool GetShowHidden() const { return false; }
  wxString GetFilter() const { return m_filter; }
  void SetFilter(const wxString& filter) { m_filter = filter; }
  int GetFilterIndex() const { return m_filterIndex; }
  void SetFilterIndex(int n) { m_filterIndex = n; }
  wxTreeItemId GetRootId() const { return m_treeCtrl != nullptr ? m_treeCtrl->GetRootItem() : wxTreeItemId(); }
  void ReCreateTree() {}

private:
  wxTreeCtrl* m_treeCtrl = nullptr;
  wxString m_defaultPath;
  wxString m_path;
  wxString m_filter;
  int m_filterIndex = 0;
};

// --- Tool and status bars (wx/tbarbase.h, wx/statusbr.h) ---------------------------------------
class wxToolBarToolBase : public wxObject
{
public:
  wxToolBarToolBase(
    int toolId,
    const wxString& label,
    const wxBitmap& bitmap,
    wxItemKind kind,
    wxObject* clientData,
    const wxString& shortHelp,
    const wxString& longHelp,
    wxControl* control = nullptr
  )
    : m_id(toolId), m_label(label), m_bitmap(bitmap), m_kind(kind), m_clientData(clientData), m_shortHelp(shortHelp),
      m_longHelp(longHelp), m_control(control)
  {}
  ~wxToolBarToolBase() override { delete m_clientData; }
  int GetId() const { return m_id; }
  wxControl* GetControl() const { return m_control; }
  bool IsButton() const { return m_id != -1 && m_control == nullptr; }
  bool IsControl() const { return m_control != nullptr; }
  bool IsSeparator() const { return m_id == -1 && m_control == nullptr; }
  bool IsEnabled() const { return m_enabled; }
  bool IsToggled() const { return m_toggled; }
  bool CanBeToggled() const { return m_kind == wxITEM_CHECK || m_kind == wxITEM_RADIO; }
  wxItemKind GetKind() const { return m_kind; }
  const wxString& GetLabel() const { return m_label; }
  const wxString& GetShortHelp() const { return m_shortHelp; }
  const wxString& GetLongHelp() const { return m_longHelp; }
  const wxBitmap& GetNormalBitmap() const { return m_bitmap; }
  wxObject* GetClientData() const { return m_clientData; }
  bool Enable(bool enable)
  {
    const bool changed = enable != m_enabled;
    m_enabled = enable;
    return changed;
  }
  bool Toggle(bool toggle)
  {
    const bool changed = toggle != m_toggled;
    m_toggled = toggle;
    return changed;
  }
  void SetShortHelp(const wxString& help) { m_shortHelp = help; }
  void SetLongHelp(const wxString& help) { m_longHelp = help; }
  void SetClientData(wxObject* clientData)
  {
    delete m_clientData;
    m_clientData = clientData;
  }

private:
  int m_id;
  wxString m_label;
  wxBitmap m_bitmap;
  wxItemKind m_kind;
  wxObject* m_clientData;
  wxString m_shortHelp;
  wxString m_longHelp;
  wxControl* m_control;
  bool m_enabled = true;
  bool m_toggled = false;
};

class wxToolBarBase : public wxControl
{
public:
  wxToolBarBase() = default;
  ~wxToolBarBase() override { ClearTools(); }

  wxToolBarToolBase* AddTool(
    int toolId,
    const wxString& label,
    const wxBitmap& bitmap,
    const wxBitmap& bmpDisabled,
    wxItemKind kind = wxITEM_NORMAL,
    const wxString& shortHelp = wxEmptyString,
    const wxString& longHelp = wxEmptyString,
    wxObject* data = nullptr
  )
  {
    (void)bmpDisabled;
    return InsertToolAt(m_tools.size(), new wxToolBarToolBase(toolId, label, bitmap, kind, data, shortHelp, longHelp));
  }
  wxToolBarToolBase* AddTool(
    int toolId,
    const wxString& label,
    const wxBitmap& bitmap,
    const wxString& shortHelp = wxEmptyString,
    wxItemKind kind = wxITEM_NORMAL
  )
  {
    return AddTool(toolId, label, bitmap, wxNullBitmap, kind, shortHelp);
  }
  wxToolBarToolBase* AddTool(
    int toolId,
    const wxBitmap& bitmap,
    const wxBitmap& pushedBitmap,
    bool toggle = false,
    wxObject* clientData = nullptr,
    const wxString& shortHelpString = wxEmptyString,
    const wxString& longHelpString = wxEmptyString
  )
  {
    return AddTool(
      toolId, wxEmptyString, bitmap, pushedBitmap, toggle ? wxITEM_CHECK : wxITEM_NORMAL, shortHelpString,
      longHelpString, clientData
    );
  }
  wxToolBarToolBase* AddTool(
    int toolId,
    const wxBitmap& bitmap,
    const wxString& shortHelpString = wxEmptyString,
    const wxString& longHelpString = wxEmptyString
  )
  {
    return AddTool(toolId, bitmap, wxNullBitmap, false, nullptr, shortHelpString, longHelpString);
  }
  wxToolBarToolBase* AddCheckTool(
    int toolId,
    const wxString& label,
    const wxBitmap& bitmap,
    const wxBitmap& bmpDisabled = wxNullBitmap,
    const wxString& shortHelp = wxEmptyString,
    const wxString& longHelp = wxEmptyString,
    wxObject* data = nullptr
  )
  {
    return AddTool(toolId, label, bitmap, bmpDisabled, wxITEM_CHECK, shortHelp, longHelp, data);
  }
  wxToolBarToolBase* AddRadioTool(
    int toolId,
    const wxString& label,
    const wxBitmap& bitmap,
    const wxBitmap& bmpDisabled = wxNullBitmap,
    const wxString& shortHelp = wxEmptyString,
    const wxString& longHelp = wxEmptyString,
    wxObject* data = nullptr
  )
  {
    return AddTool(toolId, label, bitmap, bmpDisabled, wxITEM_RADIO, shortHelp, longHelp, data);
  }
  wxToolBarToolBase* AddControl(wxControl* control)
  {
    return InsertToolAt(
      m_tools.size(),
      new wxToolBarToolBase(
        control->GetId(), wxEmptyString, wxNullBitmap, wxITEM_NORMAL, nullptr, wxEmptyString, wxEmptyString, control
      )
    );
  }
  wxToolBarToolBase* AddSeparator()
  {
    return InsertToolAt(
      m_tools.size(),
      new wxToolBarToolBase(-1, wxEmptyString, wxNullBitmap, wxITEM_SEPARATOR, nullptr, wxEmptyString, wxEmptyString)
    );
  }
  bool DeleteTool(int toolId)
  {
    for (auto it = m_tools.begin(); it != m_tools.end(); ++it) {
      if ((*it)->GetId() == toolId) {
        delete *it;
        m_tools.erase(it);
        return true;
      }
    }
    return false;
  }
  void ClearTools()
  {
    for (wxToolBarToolBase* tool : m_tools) {
      delete tool;
    }
    m_tools.clear();
  }
  virtual bool Realize() { return true; }
  wxToolBarToolBase* FindById(int toolId) const
  {
    for (wxToolBarToolBase* tool : m_tools) {
      if (tool->GetId() == toolId) {
        return tool;
      }
    }
    return nullptr;
  }
  void EnableTool(int toolId, bool enable)
  {
    if (wxToolBarToolBase* const tool = FindById(toolId)) {
      tool->Enable(enable);
    }
  }
  void ToggleTool(int toolId, bool toggle)
  {
    if (wxToolBarToolBase* const tool = FindById(toolId)) {
      tool->Toggle(toggle);
    }
  }
  bool GetToolState(int toolId) const
  {
    const wxToolBarToolBase* const tool = FindById(toolId);
    return tool != nullptr && tool->IsToggled();
  }
  bool GetToolEnabled(int toolId) const
  {
    const wxToolBarToolBase* const tool = FindById(toolId);
    return tool != nullptr && tool->IsEnabled();
  }
  void SetToolShortHelp(int toolId, const wxString& help)
  {
    if (wxToolBarToolBase* const tool = FindById(toolId)) {
      tool->SetShortHelp(help);
    }
  }
  void SetToolLongHelp(int toolId, const wxString& help)
  {
    if (wxToolBarToolBase* const tool = FindById(toolId)) {
      tool->SetLongHelp(help);
    }
  }
  virtual void SetToolBitmapSize(const wxSize& size) { m_bitmapSize = size; }
  virtual wxSize GetToolBitmapSize() const { return m_bitmapSize; }
  virtual wxSize GetToolSize() const { return m_bitmapSize; }
  void SetMargins(int x, int y)
  {
    (void)x;
    (void)y;
  }
  void SetToolPacking(int packing) { (void)packing; }
  void SetToolSeparation(int separation) { (void)separation; }
  int GetToolsCount() const { return static_cast<int>(m_tools.size()); }
  void SetRows(int rows) { (void)rows; }

protected:
  wxToolBarToolBase* InsertToolAt(size_t pos, wxToolBarToolBase* tool)
  {
    m_tools.insert(m_tools.begin() + static_cast<std::ptrdiff_t>(std::min(pos, m_tools.size())), tool);
    return tool;
  }

  std::vector<wxToolBarToolBase*> m_tools;
  wxSize m_bitmapSize = wxSize(16, 15);
};

class wxToolBar : public wxToolBarBase
{
public:
  wxToolBar() = default;
  wxToolBar(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxNO_BORDER | wxTB_HORIZONTAL,
    const wxString& name = wxToolBarNameStr
  )
  {
    Create(parent, winid, pos, size, style, name);
  }
  bool Create(
    wxWindow* parent,
    wxWindowID winid,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxNO_BORDER | wxTB_HORIZONTAL,
    const wxString& name = wxToolBarNameStr
  )
  {
    return wxControl::Create(parent, winid, pos, size, style, wxDefaultValidator, name);
  }
};

class wxStatusBar : public wxWindow
{
public:
  wxStatusBar() = default;
  wxStatusBar(
    wxWindow* parent,
    wxWindowID winid,
    long style = wxST_SIZEGRIP,
    const wxString& name = wxStatusLineNameStr
  )
  {
    Create(parent, winid, wxDefaultPosition, wxDefaultSize, style, name);
  }
  virtual void SetFieldsCount(int number = 1, const int* widths = nullptr)
  {
    (void)widths;
    m_texts.resize(static_cast<size_t>(std::max(number, 1)));
  }
  int GetFieldsCount() const { return static_cast<int>(m_texts.size()); }
  virtual void SetStatusText(const wxString& text, int number = 0)
  {
    if (number >= 0 && static_cast<size_t>(number) < m_texts.size()) {
      m_texts[static_cast<size_t>(number)] = text;
    }
  }
  virtual wxString GetStatusText(int number = 0) const
  {
    return (number >= 0 && static_cast<size_t>(number) < m_texts.size()) ? m_texts[static_cast<size_t>(number)]
                                                                         : wxString();
  }
  virtual void SetStatusWidths(int n, const int widths[])
  {
    (void)n;
    (void)widths;
  }

private:
  std::vector<wxString> m_texts = std::vector<wxString>(1);
};
