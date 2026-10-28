#pragma once

// wx stand-in: geometry, colours and the GDI objects (see wx/defs.h). Bitmaps, fonts and
// DCs hold their attributes; nothing is drawn.

#include "wx/faf/object.h"

#include "platform/posix/shell/Shell.h"

#include <memory>

class wxPoint
{
public:
  int x = 0;
  int y = 0;

  wxPoint() = default;
  wxPoint(int xx, int yy) : x(xx), y(yy) {}

  bool operator==(const wxPoint& p) const { return x == p.x && y == p.y; }
  bool operator!=(const wxPoint& p) const { return !(*this == p); }
  wxPoint operator+(const wxPoint& p) const { return wxPoint(x + p.x, y + p.y); }
  wxPoint operator-(const wxPoint& p) const { return wxPoint(x - p.x, y - p.y); }
  wxPoint& operator+=(const wxPoint& p)
  {
    x += p.x;
    y += p.y;
    return *this;
  }
  wxPoint& operator-=(const wxPoint& p)
  {
    x -= p.x;
    y -= p.y;
    return *this;
  }
};

class wxSize
{
public:
  int x = 0;
  int y = 0;

  wxSize() = default;
  wxSize(int xx, int yy) : x(xx), y(yy) {}

  bool operator==(const wxSize& s) const { return x == s.x && y == s.y; }
  bool operator!=(const wxSize& s) const { return !(*this == s); }
  wxSize operator+(const wxSize& s) const { return wxSize(x + s.x, y + s.y); }
  wxSize operator-(const wxSize& s) const { return wxSize(x - s.x, y - s.y); }

  void Set(int xx, int yy)
  {
    x = xx;
    y = yy;
  }
  void SetWidth(int w) { x = w; }
  void SetHeight(int h) { y = h; }
  int GetWidth() const { return x; }
  int GetHeight() const { return y; }
  int GetX() const { return x; }
  int GetY() const { return y; }
};

class wxRect
{
public:
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  wxRect() = default;
  wxRect(int xx, int yy, int ww, int hh) : x(xx), y(yy), width(ww), height(hh) {}
  wxRect(const wxPoint& topLeft, const wxPoint& bottomRight)
    : x(topLeft.x), y(topLeft.y), width(bottomRight.x - topLeft.x + 1), height(bottomRight.y - topLeft.y + 1)
  {}
  wxRect(const wxPoint& pos, const wxSize& size) : x(pos.x), y(pos.y), width(size.x), height(size.y) {}

  int GetX() const { return x; }
  void SetX(int xx) { x = xx; }
  int GetY() const { return y; }
  void SetY(int yy) { y = yy; }
  int GetWidth() const { return width; }
  void SetWidth(int w) { width = w; }
  int GetHeight() const { return height; }
  void SetHeight(int h) { height = h; }
  wxPoint GetPosition() const { return wxPoint(x, y); }
  void SetPosition(const wxPoint& p)
  {
    x = p.x;
    y = p.y;
  }
  wxSize GetSize() const { return wxSize(width, height); }
  void SetSize(const wxSize& s)
  {
    width = s.x;
    height = s.y;
  }
  wxPoint GetTopLeft() const { return GetPosition(); }
  wxPoint GetBottomRight() const { return wxPoint(GetRight(), GetBottom()); }
  int GetLeft() const { return x; }
  int GetTop() const { return y; }
  int GetBottom() const { return y + height - 1; }
  int GetRight() const { return x + width - 1; }
  void SetLeft(int left) { x = left; }
  void SetRight(int right) { width = right - x + 1; }
  void SetTop(int top) { y = top; }
  void SetBottom(int bottom) { height = bottom - y + 1; }

  wxRect& Inflate(int dx, int dy)
  {
    x -= dx;
    y -= dy;
    width += 2 * dx;
    height += 2 * dy;
    return *this;
  }
  wxRect& Inflate(int d) { return Inflate(d, d); }
  wxRect& Deflate(int dx, int dy) { return Inflate(-dx, -dy); }
  wxRect& Deflate(int d) { return Inflate(-d); }
  void Offset(int dx, int dy)
  {
    x += dx;
    y += dy;
  }
  void Offset(const wxPoint& p) { Offset(p.x, p.y); }

  bool Inside(int cx, int cy) const { return cx >= x && cy >= y && cy < y + height && cx < x + width; }
  bool Inside(const wxPoint& p) const { return Inside(p.x, p.y); }
  bool Intersects(const wxRect& r) const
  {
    const int left = x > r.x ? x : r.x;
    const int top = y > r.y ? y : r.y;
    const int right = GetRight() < r.GetRight() ? GetRight() : r.GetRight();
    const int bottom = GetBottom() < r.GetBottom() ? GetBottom() : r.GetBottom();
    return left <= right && top <= bottom;
  }
  wxRect& Intersect(const wxRect& r)
  {
    const int left = x > r.x ? x : r.x;
    const int top = y > r.y ? y : r.y;
    const int right = GetRight() < r.GetRight() ? GetRight() : r.GetRight();
    const int bottom = GetBottom() < r.GetBottom() ? GetBottom() : r.GetBottom();
    if (left > right || top > bottom) {
      *this = wxRect();
    } else {
      *this = wxRect(left, top, right - left + 1, bottom - top + 1);
    }
    return *this;
  }
  bool operator==(const wxRect& r) const { return x == r.x && y == r.y && width == r.width && height == r.height; }
  bool operator!=(const wxRect& r) const { return !(*this == r); }
};

inline const wxPoint wxDefaultPosition(-1, -1);
inline const wxSize wxDefaultSize(-1, -1);

class wxColour : public wxObject
{
public:
  wxColour() = default;
  wxColour(unsigned char red, unsigned char green, unsigned char blue) { Set(red, green, blue); }
  explicit wxColour(unsigned long rgb) { Set(rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF); }
  wxColour(const wxString& name) { InitFromName(name); }
  wxColour(const wxChar* name) { InitFromName(name); }

  void Set(unsigned char red, unsigned char green, unsigned char blue)
  {
    mRed = red;
    mGreen = green;
    mBlue = blue;
    mOk = true;
  }
  bool Ok() const { return mOk; }
  bool IsOk() const { return mOk; }
  unsigned char Red() const { return mRed; }
  unsigned char Green() const { return mGreen; }
  unsigned char Blue() const { return mBlue; }
  // COLORREF layout (0x00BBGGRR), as wxColour::GetPixel on MSW.
  WXDWORD GetPixel() const { return static_cast<WXDWORD>(mRed | (mGreen << 8) | (mBlue << 16)); }
  bool operator==(const wxColour& c) const
  {
    return mOk == c.mOk && mRed == c.mRed && mGreen == c.mGreen && mBlue == c.mBlue;
  }
  bool operator!=(const wxColour& c) const { return !(*this == c); }

private:
  void InitFromName(const wxString& name)
  {
    struct Named
    {
      const wxChar* name;
      unsigned char r, g, b;
    };
    static const Named kNamed[] = {
      {L"BLACK", 0, 0, 0},       {L"WHITE", 255, 255, 255}, {L"RED", 255, 0, 0},
      {L"BLUE", 0, 0, 255},      {L"GREEN", 0, 255, 0},     {L"CYAN", 0, 255, 255},
      {L"LIGHT GREY", 192, 192, 192}, {L"GREY", 128, 128, 128}, {L"YELLOW", 255, 255, 0},
    };
    for (const Named& entry : kNamed) {
      if (name.CmpNoCase(entry.name) == 0) {
        Set(entry.r, entry.g, entry.b);
        return;
      }
    }
  }

  unsigned char mRed = 0;
  unsigned char mGreen = 0;
  unsigned char mBlue = 0;
  bool mOk = false;
};

class wxColourDatabase
{
public:
  wxColour Find(const wxString& name) const { return wxColour(name); }
  wxColour* FindColour(const wxString& name)
  {
    mLast = wxColour(name);
    return mLast.Ok() ? &mLast : nullptr;
  }
  wxString FindName(const wxColour&) const { return wxString(); }

private:
  wxColour mLast;
};

class wxGDIObject : public wxObject
{
public:
  bool Ok() const { return mOk; }
  bool IsOk() const { return mOk; }

protected:
  bool mOk = false;
};

class wxBrush : public wxGDIObject
{
public:
  wxBrush() = default;
  wxBrush(const wxColour& colour, int style = wxSOLID) : mColour(colour), mStyle(style) { mOk = true; }
  wxBrush(const wxBitmap& stipple);
  wxColour& GetColour() { return mColour; }
  const wxColour& GetColour() const { return mColour; }
  int GetStyle() const { return mStyle; }
  void SetColour(const wxColour& colour)
  {
    mColour = colour;
    mOk = true;
  }
  void SetColour(unsigned char r, unsigned char g, unsigned char b) { SetColour(wxColour(r, g, b)); }
  void SetStyle(int style) { mStyle = style; }

private:
  wxColour mColour;
  int mStyle = wxSOLID;
};

class wxPen : public wxGDIObject
{
public:
  wxPen() = default;
  wxPen(const wxColour& colour, int width = 1, int style = wxSOLID) : mColour(colour), mWidth(width), mStyle(style)
  {
    mOk = true;
  }
  wxColour& GetColour() { return mColour; }
  const wxColour& GetColour() const { return mColour; }
  int GetWidth() const { return mWidth; }
  int GetStyle() const { return mStyle; }
  void SetColour(const wxColour& colour)
  {
    mColour = colour;
    mOk = true;
  }
  void SetColour(unsigned char r, unsigned char g, unsigned char b) { SetColour(wxColour(r, g, b)); }
  void SetWidth(int width) { mWidth = width; }
  void SetStyle(int style) { mStyle = style; }

private:
  wxColour mColour;
  int mWidth = 1;
  int mStyle = wxSOLID;
};

class wxFont : public wxGDIObject
{
public:
  wxFont() = default;
  wxFont(
    int pointSize,
    int family,
    int style,
    int weight,
    bool underlined = false,
    const wxString& face = wxEmptyString,
    int encoding = 0
  )
    : mPointSize(pointSize), mFamily(family), mStyle(style), mWeight(weight), mUnderlined(underlined), mFace(face)
  {
    (void)encoding;
    mOk = true;
  }
  int GetPointSize() const { return mPointSize; }
  int GetFamily() const { return mFamily; }
  int GetStyle() const { return mStyle; }
  int GetWeight() const { return mWeight; }
  bool GetUnderlined() const { return mUnderlined; }
  wxString GetFaceName() const { return mFace; }
  void SetPointSize(int size) { mPointSize = size; }
  void SetFamily(int family) { mFamily = family; }
  void SetStyle(int style) { mStyle = style; }
  void SetWeight(int weight) { mWeight = weight; }
  void SetUnderlined(bool underlined) { mUnderlined = underlined; }
  void SetFaceName(const wxString& face) { mFace = face; }

private:
  int mPointSize = 10;
  int mFamily = wxDEFAULT;
  int mStyle = wxNORMAL;
  int mWeight = wxNORMAL;
  bool mUnderlined = false;
  wxString mFace;
};

class wxPalette : public wxGDIObject
{};

class wxRegion : public wxGDIObject
{
public:
  wxRegion() = default;
  wxRegion(int x, int y, int w, int h) : mRect(x, y, w, h) { mOk = true; }
  explicit wxRegion(const wxRect& rect) : mRect(rect) { mOk = true; }
  wxRect GetBox() const { return mRect; }

private:
  wxRect mRect;
};

class wxImage;

// Bitmaps keep their size; loading from files is not available (nothing displays them).
class wxBitmap : public wxGDIObject
{
public:
  wxBitmap() = default;
  wxBitmap(int width, int height, int depth = -1) : mWidth(width), mHeight(height), mDepth(depth)
  {
    mOk = width > 0 && height > 0;
  }
  wxBitmap(const char bits[], int width, int height, int depth = 1) : wxBitmap(width, height, depth) { (void)bits; }
  wxBitmap(const char** xpmData) { (void)xpmData; }
  wxBitmap(char** xpmData) { (void)xpmData; }
  wxBitmap(const wxString& name, long type = wxBITMAP_TYPE_BMP_RESOURCE) { LoadFile(name, type); }
  explicit wxBitmap(const wxImage& image, int depth = -1);

  bool LoadFile(const wxString& name, long type = wxBITMAP_TYPE_BMP_RESOURCE)
  {
    (void)name;
    (void)type;
    FAF_POSIX_STUB("wxBitmap::LoadFile: no image loading in the wx stand-in");
    mOk = false;
    return false;
  }
  bool SaveFile(const wxString& name, int type, const wxPalette* palette = nullptr) const
  {
    (void)name;
    (void)type;
    (void)palette;
    return false;
  }
  int GetWidth() const { return mWidth; }
  int GetHeight() const { return mHeight; }
  int GetDepth() const { return mDepth; }
  void SetWidth(int width) { mWidth = width; }
  void SetHeight(int height) { mHeight = height; }
  void SetDepth(int depth) { mDepth = depth; }
  wxImage ConvertToImage() const;
  void SetMask(class wxMask* mask);
  wxBitmap GetSubBitmap(const wxRect& rect) const { return wxBitmap(rect.width, rect.height, mDepth); }

private:
  int mWidth = 0;
  int mHeight = 0;
  int mDepth = -1;
};

class wxMask : public wxObject
{
public:
  wxMask() = default;
  wxMask(const wxBitmap&, const wxColour&) {}
  explicit wxMask(const wxBitmap&) {}
};

inline void wxBitmap::SetMask(wxMask* mask)
{
  delete mask;
}

class wxIcon : public wxBitmap
{
public:
  wxIcon() = default;
  wxIcon(const wxString& name, long type = wxBITMAP_TYPE_ICO_RESOURCE, int desiredWidth = -1, int desiredHeight = -1)
  {
    (void)name;
    (void)type;
    (void)desiredWidth;
    (void)desiredHeight;
  }
  void CopyFromBitmap(const wxBitmap& bitmap) { static_cast<wxBitmap&>(*this) = bitmap; }
};

class wxCursor : public wxBitmap
{
public:
  wxCursor() = default;
  wxCursor(int cursorId) : mId(cursorId) {}
  wxCursor(const wxString& name, long type, int hotSpotX = 0, int hotSpotY = 0)
  {
    (void)name;
    (void)type;
    (void)hotSpotX;
    (void)hotSpotY;
  }
  int GetStockId() const { return mId; }

private:
  int mId = wxCURSOR_NONE;
};

class wxImageHandler : public wxObject
{
public:
  wxImageHandler() = default;
  wxImageHandler(const wxString& name, const wxString& extension, long type)
    : mName(name), mExtension(extension), mType(type)
  {}
  const wxString& GetName() const { return mName; }
  const wxString& GetExtension() const { return mExtension; }
  long GetType() const { return mType; }

protected:
  wxString mName;
  wxString mExtension;
  long mType = 0;
};

class wxPNGHandler : public wxImageHandler
{
public:
  wxPNGHandler() : wxImageHandler(L"PNG file", L"png", wxBITMAP_TYPE_PNG) {}
};

class wxBMPHandler : public wxImageHandler
{
public:
  wxBMPHandler() : wxImageHandler(L"Windows bitmap file", L"bmp", wxBITMAP_TYPE_BMP) {}
};

class wxICOResourceHandler : public wxImageHandler
{
public:
  wxICOResourceHandler() : wxImageHandler(L"ICO resource", L"ico", wxBITMAP_TYPE_ICO_RESOURCE) {}
};

class wxImage : public wxObject
{
public:
  wxImage() = default;
  wxImage(int width, int height, bool clear = true)
    : mWidth(width), mHeight(height), mData(static_cast<size_t>(width > 0 && height > 0 ? width * height * 3 : 0))
  {
    (void)clear;
  }
  wxImage(const wxString& name, long type = wxBITMAP_TYPE_ANY) { LoadFile(name, type); }

  // The engine adds handlers once and never removes them: the stand-in owns them.
  static void AddHandler(wxImageHandler* handler) { Handlers().emplace_back(handler); }
  static void InsertHandler(wxImageHandler* handler) { AddHandler(handler); }
  static void InitStandardHandlers() {}
  static void CleanUpHandlers() { Handlers().clear(); }
  static wxImageHandler* FindHandler(long type)
  {
    for (const auto& handler : Handlers()) {
      if (handler->GetType() == type) {
        return handler.get();
      }
    }
    return nullptr;
  }

  bool LoadFile(const wxString& name, long type = wxBITMAP_TYPE_ANY)
  {
    (void)name;
    (void)type;
    FAF_POSIX_STUB("wxImage::LoadFile: no image loading in the wx stand-in");
    return false;
  }
  bool SaveFile(const wxString& name, int type) const
  {
    (void)name;
    (void)type;
    return false;
  }
  bool Ok() const { return mWidth > 0 && mHeight > 0; }
  int GetWidth() const { return mWidth; }
  int GetHeight() const { return mHeight; }
  unsigned char* GetData() { return mData.empty() ? nullptr : mData.data(); }
  void SetData(unsigned char* data)
  {
    if (data != nullptr) {
      std::memcpy(mData.data(), data, mData.size());
      std::free(data);
    }
  }
  void Create(int width, int height)
  {
    mWidth = width;
    mHeight = height;
    mData.assign(static_cast<size_t>(width > 0 && height > 0 ? width * height * 3 : 0), 0);
  }
  void Destroy() { Create(0, 0); }
  wxImage Scale(int width, int height) const { return wxImage(width, height); }
  wxImage& Rescale(int width, int height)
  {
    Create(width, height);
    return *this;
  }
  void SetRGB(int x, int y, unsigned char r, unsigned char g, unsigned char b)
  {
    if (x >= 0 && y >= 0 && x < mWidth && y < mHeight) {
      unsigned char* pixel = &mData[static_cast<size_t>((y * mWidth + x) * 3)];
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
    }
  }
  unsigned char GetRed(int x, int y) const { return mData[static_cast<size_t>((y * mWidth + x) * 3)]; }
  unsigned char GetGreen(int x, int y) const { return mData[static_cast<size_t>((y * mWidth + x) * 3 + 1)]; }
  unsigned char GetBlue(int x, int y) const { return mData[static_cast<size_t>((y * mWidth + x) * 3 + 2)]; }
  void SetMask(bool mask = true) { (void)mask; }
  void SetMaskColour(unsigned char r, unsigned char g, unsigned char b)
  {
    (void)r;
    (void)g;
    (void)b;
  }
  wxBitmap ConvertToBitmap() const { return wxBitmap(mWidth, mHeight); }

private:
  static std::vector<std::unique_ptr<wxImageHandler>>& Handlers()
  {
    static std::vector<std::unique_ptr<wxImageHandler>> handlers;
    return handlers;
  }

  int mWidth = 0;
  int mHeight = 0;
  std::vector<unsigned char> mData;
};

inline wxBitmap::wxBitmap(const wxImage& image, int depth) : wxBitmap(image.GetWidth(), image.GetHeight(), depth) {}
inline wxImage wxBitmap::ConvertToImage() const
{
  return wxImage(mWidth, mHeight);
}
inline wxBrush::wxBrush(const wxBitmap& stipple) : mStyle(wxSTIPPLE)
{
  mOk = stipple.Ok();
}

class wxImageList : public wxObject
{
public:
  wxImageList() = default;
  wxImageList(int width, int height, bool mask = true, int initialCount = 1) : mWidth(width), mHeight(height)
  {
    (void)mask;
    (void)initialCount;
  }
  bool Create(int width, int height, bool mask = true, int initialCount = 1)
  {
    (void)mask;
    (void)initialCount;
    mWidth = width;
    mHeight = height;
    return true;
  }
  int Add(const wxBitmap& bitmap, const wxBitmap& mask = wxBitmap())
  {
    (void)mask;
    mImages.push_back(bitmap);
    return static_cast<int>(mImages.size()) - 1;
  }
  int Add(const wxBitmap& bitmap, const wxColour& maskColour)
  {
    (void)maskColour;
    return Add(bitmap);
  }
  int Add(const wxIcon& icon) { return Add(static_cast<const wxBitmap&>(icon)); }
  bool Replace(int index, const wxBitmap& bitmap)
  {
    if (index < 0 || index >= static_cast<int>(mImages.size())) {
      return false;
    }
    mImages[static_cast<size_t>(index)] = bitmap;
    return true;
  }
  bool Remove(int index)
  {
    if (index < 0 || index >= static_cast<int>(mImages.size())) {
      return false;
    }
    mImages.erase(mImages.begin() + index);
    return true;
  }
  bool RemoveAll()
  {
    mImages.clear();
    return true;
  }
  int GetImageCount() const { return static_cast<int>(mImages.size()); }
  bool GetSize(int index, int& width, int& height) const
  {
    (void)index;
    width = mWidth;
    height = mHeight;
    return true;
  }
  bool Draw(int index, wxDC& dc, int x, int y, int flags = 0, bool solidBackground = false)
  {
    (void)index;
    (void)dc;
    (void)x;
    (void)y;
    (void)flags;
    (void)solidBackground;
    return true;
  }

private:
  int mWidth = 0;
  int mHeight = 0;
  std::vector<wxBitmap> mImages;
};

// --- Stock objects (wx/gdicmn.h). wx 2.4.2 exposes most of them as pointers. ---------------
namespace faf_wx_detail
{
  inline const wxColour kBlack(0, 0, 0);
  inline const wxColour kWhite(255, 255, 255);
  inline const wxColour kRed(255, 0, 0);
  inline const wxColour kBlue(0, 0, 255);
  inline const wxColour kGreen(0, 255, 0);
  inline const wxColour kCyan(0, 255, 255);
  inline const wxColour kLightGrey(192, 192, 192);
  inline const wxColour kGrey(128, 128, 128);
  inline const wxColour kMediumGrey(100, 100, 100);
} // namespace faf_wx_detail

inline wxColour* wxBLACK = const_cast<wxColour*>(&faf_wx_detail::kBlack);
inline wxColour* wxWHITE = const_cast<wxColour*>(&faf_wx_detail::kWhite);
inline wxColour* wxRED = const_cast<wxColour*>(&faf_wx_detail::kRed);
inline wxColour* wxBLUE = const_cast<wxColour*>(&faf_wx_detail::kBlue);
inline wxColour* wxGREEN = const_cast<wxColour*>(&faf_wx_detail::kGreen);
inline wxColour* wxCYAN = const_cast<wxColour*>(&faf_wx_detail::kCyan);
inline wxColour* wxLIGHT_GREY = const_cast<wxColour*>(&faf_wx_detail::kLightGrey);
inline wxColour wxNullColour;

inline wxBrush wxFafBlackBrush(faf_wx_detail::kBlack);
inline wxBrush wxFafWhiteBrush(faf_wx_detail::kWhite);
inline wxBrush wxFafGreyBrush(faf_wx_detail::kGrey);
inline wxBrush wxFafMediumGreyBrush(faf_wx_detail::kMediumGrey);
inline wxBrush wxFafLightGreyBrush(faf_wx_detail::kLightGrey);
inline wxBrush wxFafRedBrush(faf_wx_detail::kRed);
inline wxBrush wxFafBlueBrush(faf_wx_detail::kBlue);
inline wxBrush wxFafGreenBrush(faf_wx_detail::kGreen);
inline wxBrush wxFafCyanBrush(faf_wx_detail::kCyan);
inline wxBrush wxFafTransparentBrush(faf_wx_detail::kBlack, wxTRANSPARENT);
inline wxBrush* wxBLACK_BRUSH = &wxFafBlackBrush;
inline wxBrush* wxWHITE_BRUSH = &wxFafWhiteBrush;
inline wxBrush* wxGREY_BRUSH = &wxFafGreyBrush;
inline wxBrush* wxMEDIUM_GREY_BRUSH = &wxFafMediumGreyBrush;
inline wxBrush* wxLIGHT_GREY_BRUSH = &wxFafLightGreyBrush;
inline wxBrush* wxRED_BRUSH = &wxFafRedBrush;
inline wxBrush* wxBLUE_BRUSH = &wxFafBlueBrush;
inline wxBrush* wxGREEN_BRUSH = &wxFafGreenBrush;
inline wxBrush* wxCYAN_BRUSH = &wxFafCyanBrush;
inline wxBrush* wxTRANSPARENT_BRUSH = &wxFafTransparentBrush;
inline wxBrush wxNullBrush;

inline wxPen wxFafBlackPen(faf_wx_detail::kBlack);
inline wxPen wxFafWhitePen(faf_wx_detail::kWhite);
inline wxPen wxFafRedPen(faf_wx_detail::kRed);
inline wxPen wxFafCyanPen(faf_wx_detail::kCyan);
inline wxPen wxFafGreenPen(faf_wx_detail::kGreen);
inline wxPen wxFafGreyPen(faf_wx_detail::kGrey);
inline wxPen wxFafMediumGreyPen(faf_wx_detail::kMediumGrey);
inline wxPen wxFafLightGreyPen(faf_wx_detail::kLightGrey);
inline wxPen wxFafBlackDashedPen(faf_wx_detail::kBlack, 1, wxSHORT_DASH);
inline wxPen wxFafTransparentPen(faf_wx_detail::kBlack, 1, wxTRANSPARENT);
inline wxPen* wxBLACK_PEN = &wxFafBlackPen;
inline wxPen* wxWHITE_PEN = &wxFafWhitePen;
inline wxPen* wxRED_PEN = &wxFafRedPen;
inline wxPen* wxCYAN_PEN = &wxFafCyanPen;
inline wxPen* wxGREEN_PEN = &wxFafGreenPen;
inline wxPen* wxGREY_PEN = &wxFafGreyPen;
inline wxPen* wxMEDIUM_GREY_PEN = &wxFafMediumGreyPen;
inline wxPen* wxLIGHT_GREY_PEN = &wxFafLightGreyPen;
inline wxPen* wxBLACK_DASHED_PEN = &wxFafBlackDashedPen;
inline wxPen* wxTRANSPARENT_PEN = &wxFafTransparentPen;
inline wxPen wxNullPen;

inline wxFont wxFafNormalFont(10, wxSWISS, wxNORMAL, wxNORMAL);
inline wxFont wxFafSmallFont(8, wxSWISS, wxNORMAL, wxNORMAL);
inline wxFont wxFafItalicFont(10, wxROMAN, wxITALIC, wxNORMAL);
inline wxFont wxFafSwissFont(10, wxSWISS, wxNORMAL, wxNORMAL);
inline wxFont* wxNORMAL_FONT = &wxFafNormalFont;
inline wxFont* wxSMALL_FONT = &wxFafSmallFont;
inline wxFont* wxITALIC_FONT = &wxFafItalicFont;
inline wxFont* wxSWISS_FONT = &wxFafSwissFont;
inline wxFont wxNullFont;

inline wxBitmap wxNullBitmap;
inline wxIcon wxNullIcon;
inline wxCursor wxNullCursor;
inline wxPalette wxNullPalette;
inline wxCursor wxFafStandardCursor(wxCURSOR_ARROW);
inline wxCursor wxFafHourglassCursor(wxCURSOR_WAIT);
inline wxCursor wxFafCrossCursor(wxCURSOR_CROSS);
inline wxCursor* wxSTANDARD_CURSOR = &wxFafStandardCursor;
inline wxCursor* wxHOURGLASS_CURSOR = &wxFafHourglassCursor;
inline wxCursor* wxCROSS_CURSOR = &wxFafCrossCursor;
inline wxColourDatabase wxFafColourDatabase;
inline wxColourDatabase* wxTheColourDatabase = &wxFafColourDatabase;

// --- Device contexts (wx/dc.h, wx/dcclient.h, wx/dcmemory.h) -------------------------------
class wxDC : public wxObject
{
public:
  wxDC() = default;

  bool Ok() const { return true; }
  void BeginDrawing() {}
  void EndDrawing() {}

  void SetBrush(const wxBrush& brush) { mBrush = brush; }
  void SetPen(const wxPen& pen) { mPen = pen; }
  void SetFont(const wxFont& font) { mFont = font; }
  void SetBackground(const wxBrush& brush) { mBackground = brush; }
  void SetBackgroundMode(int mode) { mBackgroundMode = mode; }
  void SetTextForeground(const wxColour& colour) { mTextForeground = colour; }
  void SetTextBackground(const wxColour& colour) { mTextBackground = colour; }
  void SetLogicalFunction(int function) { mLogicalFunction = function; }
  void SetPalette(const wxPalette&) {}
  const wxBrush& GetBrush() const { return mBrush; }
  const wxPen& GetPen() const { return mPen; }
  const wxFont& GetFont() const { return mFont; }
  const wxBrush& GetBackground() const { return mBackground; }
  const wxColour& GetTextForeground() const { return mTextForeground; }
  const wxColour& GetTextBackground() const { return mTextBackground; }
  int GetBackgroundMode() const { return mBackgroundMode; }
  int GetLogicalFunction() const { return mLogicalFunction; }

  void Clear() {}
  void DrawLine(wxCoord, wxCoord, wxCoord, wxCoord) {}
  void DrawLine(const wxPoint&, const wxPoint&) {}
  void DrawLines(int, wxPoint[], wxCoord = 0, wxCoord = 0) {}
  void DrawPoint(wxCoord, wxCoord) {}
  void DrawPoint(const wxPoint&) {}
  void DrawRectangle(wxCoord, wxCoord, wxCoord, wxCoord) {}
  void DrawRectangle(const wxRect&) {}
  void DrawRectangle(const wxPoint&, const wxSize&) {}
  void DrawRoundedRectangle(wxCoord, wxCoord, wxCoord, wxCoord, double = 20.0) {}
  void DrawCircle(wxCoord, wxCoord, wxCoord) {}
  void DrawEllipse(wxCoord, wxCoord, wxCoord, wxCoord) {}
  void DrawArc(wxCoord, wxCoord, wxCoord, wxCoord, wxCoord, wxCoord) {}
  void DrawPolygon(int, wxPoint[], wxCoord = 0, wxCoord = 0, int = wxODDEVEN_RULE) {}
  void DrawText(const wxString&, wxCoord, wxCoord) {}
  void DrawText(const wxString&, const wxPoint&) {}
  void DrawRotatedText(const wxString&, wxCoord, wxCoord, double) {}
  void DrawBitmap(const wxBitmap&, wxCoord, wxCoord, bool = false) {}
  void DrawBitmap(const wxBitmap&, const wxPoint&, bool = false) {}
  void DrawIcon(const wxIcon&, wxCoord, wxCoord) {}
  void CrossHair(wxCoord, wxCoord) {}
  bool FloodFill(wxCoord, wxCoord, const wxColour&, int = 1) { return false; }
  bool Blit(wxCoord, wxCoord, wxCoord, wxCoord, wxDC*, wxCoord, wxCoord, int = 0, bool = false) { return false; }

  void SetClippingRegion(wxCoord, wxCoord, wxCoord, wxCoord) {}
  void SetClippingRegion(const wxRect&) {}
  void SetClippingRegion(const wxRegion&) {}
  void DestroyClippingRegion() {}
  void SetDeviceOrigin(wxCoord x, wxCoord y)
  {
    mOriginX = x;
    mOriginY = y;
  }
  void SetLogicalOrigin(wxCoord x, wxCoord y)
  {
    mOriginX = x;
    mOriginY = y;
  }
  void SetUserScale(double, double) {}

  void GetSize(int* width, int* height) const
  {
    if (width != nullptr) {
      *width = mWidth;
    }
    if (height != nullptr) {
      *height = mHeight;
    }
  }
  wxSize GetSize() const { return wxSize(mWidth, mHeight); }
  void GetTextExtent(
    const wxString& text,
    wxCoord* width,
    wxCoord* height,
    wxCoord* descent = nullptr,
    wxCoord* externalLeading = nullptr,
    const wxFont* font = nullptr
  ) const
  {
    (void)font;
    const int pointSize = mFont.Ok() ? mFont.GetPointSize() : 10;
    if (width != nullptr) {
      *width = static_cast<wxCoord>(text.Len()) * (pointSize * 6 / 10 + 1);
    }
    if (height != nullptr) {
      *height = pointSize + pointSize / 3 + 1;
    }
    if (descent != nullptr) {
      *descent = pointSize / 4;
    }
    if (externalLeading != nullptr) {
      *externalLeading = 0;
    }
  }
  wxCoord GetCharHeight() const
  {
    wxCoord height = 0;
    GetTextExtent(L"M", nullptr, &height);
    return height;
  }
  wxCoord GetCharWidth() const
  {
    wxCoord width = 0;
    GetTextExtent(L"M", &width, nullptr);
    return width;
  }
  WXHDC GetHDC() const { return 0; }

protected:
  void SetFafSize(int width, int height)
  {
    mWidth = width;
    mHeight = height;
  }

  wxBrush mBrush;
  wxPen mPen;
  wxFont mFont;
  wxBrush mBackground;
  wxColour mTextForeground;
  wxColour mTextBackground;
  int mBackgroundMode = wxTRANSPARENT;
  int mLogicalFunction = 0;
  int mWidth = 0;
  int mHeight = 0;
  wxCoord mOriginX = 0;
  wxCoord mOriginY = 0;
};

class wxMemoryDC : public wxDC
{
public:
  wxMemoryDC() = default;
  explicit wxMemoryDC(wxDC*) {}
  void SelectObject(const wxBitmap& bitmap) { SetFafSize(bitmap.GetWidth(), bitmap.GetHeight()); }
};

class wxScreenDC : public wxDC
{
public:
  static bool StartDrawingOnTop(wxWindow* = nullptr) { return true; }
  static bool EndDrawingOnTop() { return true; }
};

// --- System settings (wx/settings.h) -----------------------------------------------------------
enum wxSystemColour
{
  wxSYS_COLOUR_SCROLLBAR,
  wxSYS_COLOUR_BACKGROUND,
  wxSYS_COLOUR_ACTIVECAPTION,
  wxSYS_COLOUR_INACTIVECAPTION,
  wxSYS_COLOUR_MENU,
  wxSYS_COLOUR_WINDOW,
  wxSYS_COLOUR_WINDOWFRAME,
  wxSYS_COLOUR_MENUTEXT,
  wxSYS_COLOUR_WINDOWTEXT,
  wxSYS_COLOUR_CAPTIONTEXT,
  wxSYS_COLOUR_ACTIVEBORDER,
  wxSYS_COLOUR_INACTIVEBORDER,
  wxSYS_COLOUR_APPWORKSPACE,
  wxSYS_COLOUR_HIGHLIGHT,
  wxSYS_COLOUR_HIGHLIGHTTEXT,
  wxSYS_COLOUR_BTNFACE,
  wxSYS_COLOUR_BTNSHADOW,
  wxSYS_COLOUR_GRAYTEXT,
  wxSYS_COLOUR_BTNTEXT,
  wxSYS_COLOUR_INACTIVECAPTIONTEXT,
  wxSYS_COLOUR_BTNHIGHLIGHT,
  wxSYS_COLOUR_3DDKSHADOW,
  wxSYS_COLOUR_3DLIGHT,
  wxSYS_COLOUR_INFOTEXT,
  wxSYS_COLOUR_INFOBK,
  wxSYS_COLOUR_LISTBOX,
  wxSYS_COLOUR_DESKTOP = wxSYS_COLOUR_BACKGROUND,
  wxSYS_COLOUR_3DFACE = wxSYS_COLOUR_BTNFACE,
  wxSYS_COLOUR_3DSHADOW = wxSYS_COLOUR_BTNSHADOW,
  wxSYS_COLOUR_3DHIGHLIGHT = wxSYS_COLOUR_BTNHIGHLIGHT,
  wxSYS_COLOUR_3DHILIGHT = wxSYS_COLOUR_BTNHIGHLIGHT,
  wxSYS_COLOUR_BTNHILIGHT = wxSYS_COLOUR_BTNHIGHLIGHT
};

enum wxSystemMetric
{
  wxSYS_MOUSE_BUTTONS = 1,
  wxSYS_BORDER_X,
  wxSYS_BORDER_Y,
  wxSYS_CURSOR_X,
  wxSYS_CURSOR_Y,
  wxSYS_DCLICK_X,
  wxSYS_DCLICK_Y,
  wxSYS_DRAG_X,
  wxSYS_DRAG_Y,
  wxSYS_EDGE_X,
  wxSYS_EDGE_Y,
  wxSYS_HSCROLL_ARROW_X,
  wxSYS_HSCROLL_ARROW_Y,
  wxSYS_HTHUMB_X,
  wxSYS_ICON_X,
  wxSYS_ICON_Y,
  wxSYS_ICONSPACING_X,
  wxSYS_ICONSPACING_Y,
  wxSYS_WINDOWMIN_X,
  wxSYS_WINDOWMIN_Y,
  wxSYS_SCREEN_X,
  wxSYS_SCREEN_Y,
  wxSYS_FRAMESIZE_X,
  wxSYS_FRAMESIZE_Y,
  wxSYS_SMALLICON_X,
  wxSYS_SMALLICON_Y,
  wxSYS_HSCROLL_Y,
  wxSYS_VSCROLL_X,
  wxSYS_VSCROLL_ARROW_X,
  wxSYS_VSCROLL_ARROW_Y,
  wxSYS_VTHUMB_Y,
  wxSYS_CAPTION_Y,
  wxSYS_MENU_Y,
  wxSYS_NETWORK_PRESENT,
  wxSYS_PENWINDOWS_PRESENT,
  wxSYS_SHOW_SOUNDS,
  wxSYS_SWAP_BUTTONS
};

enum wxSystemFont
{
  wxSYS_OEM_FIXED_FONT = 10,
  wxSYS_ANSI_FIXED_FONT,
  wxSYS_ANSI_VAR_FONT,
  wxSYS_SYSTEM_FONT,
  wxSYS_DEVICE_DEFAULT_FONT,
  wxSYS_DEFAULT_PALETTE,
  wxSYS_SYSTEM_FIXED_FONT,
  wxSYS_DEFAULT_GUI_FONT
};

class wxSystemSettings
{
public:
  static wxColour GetColour(wxSystemColour index)
  {
    switch (index) {
      case wxSYS_COLOUR_WINDOW:
      case wxSYS_COLOUR_LISTBOX:
      case wxSYS_COLOUR_INFOBK:
        return wxColour(255, 255, 255);
      case wxSYS_COLOUR_HIGHLIGHT:
        return wxColour(49, 106, 197);
      case wxSYS_COLOUR_HIGHLIGHTTEXT:
        return wxColour(255, 255, 255);
      case wxSYS_COLOUR_WINDOWTEXT:
      case wxSYS_COLOUR_BTNTEXT:
      case wxSYS_COLOUR_MENUTEXT:
      case wxSYS_COLOUR_INFOTEXT:
        return wxColour(0, 0, 0);
      default:
        return wxColour(212, 208, 200);
    }
  }
  static wxColour GetSystemColour(int index) { return GetColour(static_cast<wxSystemColour>(index)); }
  static wxFont GetFont(wxSystemFont) { return *wxNORMAL_FONT; }
  static wxFont GetSystemFont(int index) { return GetFont(static_cast<wxSystemFont>(index)); }
  static int GetMetric(wxSystemMetric index)
  {
    // The window system's primary display while it is up (GetSystemMetrics answers from it);
    // a fixed 1920x1080 otherwise.
    switch (index) {
      case wxSYS_SCREEN_X:
        return faf_posix::shell::IsActive() ? ::GetSystemMetrics(SM_CXSCREEN) : 1920;
      case wxSYS_SCREEN_Y:
        return faf_posix::shell::IsActive() ? ::GetSystemMetrics(SM_CYSCREEN) : 1080;
      case wxSYS_MOUSE_BUTTONS:
        return 3;
      default:
        return 0;
    }
  }
  static int GetSystemMetric(int index) { return GetMetric(static_cast<wxSystemMetric>(index)); }
};
