#pragma once

// wx stand-in: wxObject, class info and the window list (see wx/defs.h).

#include "wx/faf/string.h"

#include <atomic>
#include <list>

// FAF_POSIX_STUB is the platform layer's "this path is not implemented on POSIX yet" log
// (platform/posix/README.md, rule 6). The stand-in logs through it; the fallback below is
// only used if the platform layer has not defined it yet.
#ifndef FAF_POSIX_STUB
#define FAF_POSIX_STUB(what)                                                                  \
  do {                                                                                        \
    static std::atomic<bool> faf_posix_stub_logged_{false};                                   \
    if (!faf_posix_stub_logged_.exchange(true)) {                                             \
      std::fprintf(stderr, "FAF_POSIX_STUB: %s (%s:%d)\n", (what), __FILE__, __LINE__);      \
    }                                                                                         \
  } while (0)
#endif

class wxClassInfo
{
public:
  wxClassInfo(const wxChar* className, const wxClassInfo* baseInfo1, const wxClassInfo* baseInfo2 = nullptr)
    : mClassName(className), mBaseInfo1(baseInfo1), mBaseInfo2(baseInfo2)
  {}

  const wxChar* GetClassName() const { return mClassName; }
  const wxClassInfo* GetBaseClass1() const { return mBaseInfo1; }
  const wxClassInfo* GetBaseClass2() const { return mBaseInfo2; }
  bool IsKindOf(const wxClassInfo* info) const
  {
    return info != nullptr &&
           (info == this || (mBaseInfo1 != nullptr && mBaseInfo1->IsKindOf(info)) ||
            (mBaseInfo2 != nullptr && mBaseInfo2->IsKindOf(info)));
  }

private:
  const wxChar* mClassName;
  const wxClassInfo* mBaseInfo1;
  const wxClassInfo* mBaseInfo2;
};

#define DECLARE_ABSTRACT_CLASS(name)                     \
public:                                                  \
  static wxClassInfo sm_class##name;                     \
  virtual wxClassInfo* GetClassInfo() const override { return &name::sm_class##name; }
#define DECLARE_DYNAMIC_CLASS(name) DECLARE_ABSTRACT_CLASS(name)
#define DECLARE_DYNAMIC_CLASS_NO_COPY(name) DECLARE_ABSTRACT_CLASS(name)
#define DECLARE_CLASS(name) DECLARE_ABSTRACT_CLASS(name)
#define DECLARE_NO_COPY_CLASS(name)
#define IMPLEMENT_ABSTRACT_CLASS(name, basename) \
  wxClassInfo name::sm_class##name(wxT(#name), &basename::sm_class##basename);
#define IMPLEMENT_DYNAMIC_CLASS(name, basename) IMPLEMENT_ABSTRACT_CLASS(name, basename)
#define IMPLEMENT_CLASS(name, basename) IMPLEMENT_ABSTRACT_CLASS(name, basename)
#define CLASSINFO(name) (&name::sm_class##name)

class wxObject
{
public:
  wxObject() = default;
  wxObject(const wxObject&) = default;
  wxObject& operator=(const wxObject&) = default;
  virtual ~wxObject() = default;

  inline static wxClassInfo sm_classwxObject{L"wxObject", nullptr};
  virtual wxClassInfo* GetClassInfo() const { return &sm_classwxObject; }
  bool IsKindOf(const wxClassInfo* info) const { return GetClassInfo()->IsKindOf(info); }
};

// wxList of windows. wx's is a linked list of wxNode; the engine only appends, removes and
// iterates, which this keeps.
template <class T>
class wxFafPointerList
{
public:
  typedef typename std::list<T*>::iterator iterator;
  typedef typename std::list<T*>::const_iterator const_iterator;

  size_t GetCount() const { return mItems.size(); }
  size_t size() const { return mItems.size(); }
  bool IsEmpty() const { return mItems.empty(); }
  bool empty() const { return mItems.empty(); }
  void Append(T* item) { mItems.push_back(item); }
  void Insert(T* item) { mItems.push_front(item); }
  bool DeleteObject(T* item)
  {
    for (auto it = mItems.begin(); it != mItems.end(); ++it) {
      if (*it == item) {
        mItems.erase(it);
        return true;
      }
    }
    return false;
  }
  bool Member(const T* item) const
  {
    for (T* candidate : mItems) {
      if (candidate == item) {
        return true;
      }
    }
    return false;
  }
  int IndexOf(const T* item) const
  {
    int index = 0;
    for (T* candidate : mItems) {
      if (candidate == item) {
        return index;
      }
      ++index;
    }
    return -1;
  }
  T* Item(size_t index) const
  {
    for (T* candidate : mItems) {
      if (index-- == 0) {
        return candidate;
      }
    }
    return nullptr;
  }
  T* GetFirst() const { return mItems.empty() ? nullptr : mItems.front(); }
  T* GetLast() const { return mItems.empty() ? nullptr : mItems.back(); }
  void Clear() { mItems.clear(); }
  iterator begin() { return mItems.begin(); }
  iterator end() { return mItems.end(); }
  const_iterator begin() const { return mItems.begin(); }
  const_iterator end() const { return mItems.end(); }

private:
  std::list<T*> mItems;
};

class wxWindowList : public wxFafPointerList<wxWindow>
{};

class wxList : public wxFafPointerList<wxObject>
{};
