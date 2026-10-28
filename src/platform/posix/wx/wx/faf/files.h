#pragma once

// wx stand-in: files, streams and URLs (see wx/defs.h). The engine includes these headers
// but does its own file I/O; what is here is the small part of the API around them.

#include "wx/faf/app.h"

class wxFFile
{
public:
  wxFFile() = default;
  explicit wxFFile(FILE* fp) : m_fp(fp) {}
  wxFFile(const wxChar* filename, const char* mode = "r") { Open(filename, mode); }
  ~wxFFile() { Close(); }
  wxFFile(const wxFFile&) = delete;
  wxFFile& operator=(const wxFFile&) = delete;

  bool Open(const wxChar* filename, const char* mode = "r")
  {
    Close();
    const wxCharBuffer path = wxString(filename).mb_str(wxConvUTF8);
    m_fp = path.data() != nullptr ? std::fopen(path.data(), mode) : nullptr;
    return m_fp != nullptr;
  }
  bool Close()
  {
    if (m_fp == nullptr) {
      return true;
    }
    const bool ok = std::fclose(m_fp) == 0;
    m_fp = nullptr;
    return ok;
  }
  bool IsOpened() const { return m_fp != nullptr; }
  FILE* fp() const { return m_fp; }
  void Attach(FILE* fp) { m_fp = fp; }
  FILE* Detach()
  {
    FILE* const fp = m_fp;
    m_fp = nullptr;
    return fp;
  }
  size_t Read(void* buffer, size_t count) { return m_fp != nullptr ? std::fread(buffer, 1, count, m_fp) : 0; }
  size_t Write(const void* buffer, size_t count) { return m_fp != nullptr ? std::fwrite(buffer, 1, count, m_fp) : 0; }
  bool Write(const wxString& s, const wxMBConv& conv = wxConvUTF8)
  {
    const wxCharBuffer data = s.mb_str(conv);
    const size_t length = data.data() != nullptr ? std::strlen(data.data()) : 0;
    return Write(data.data(), length) == length;
  }
  bool Flush() { return m_fp != nullptr && std::fflush(m_fp) == 0; }
  bool Eof() const { return m_fp == nullptr || std::feof(m_fp) != 0; }
  bool Error() const { return m_fp == nullptr || std::ferror(m_fp) != 0; }
  long Tell() const { return m_fp != nullptr ? std::ftell(m_fp) : -1; }
  bool Seek(long offset) { return m_fp != nullptr && std::fseek(m_fp, offset, SEEK_SET) == 0; }

private:
  FILE* m_fp = nullptr;
};

class wxFile
{
public:
  enum OpenMode
  {
    read,
    write,
    read_write,
    write_append,
    write_excl
  };

  wxFile() = default;
  wxFile(const wxChar* filename, OpenMode mode = read) { Open(filename, mode); }
  ~wxFile() { Close(); }
  wxFile(const wxFile&) = delete;
  wxFile& operator=(const wxFile&) = delete;

  static bool Exists(const wxChar* filename)
  {
    const wxCharBuffer path = wxString(filename).mb_str(wxConvUTF8);
    FILE* const fp = path.data() != nullptr ? std::fopen(path.data(), "rb") : nullptr;
    if (fp == nullptr) {
      return false;
    }
    std::fclose(fp);
    return true;
  }
  bool Open(const wxChar* filename, OpenMode mode = read)
  {
    static const char* const kModes[] = {"rb", "wb", "r+b", "ab", "wbx"};
    return m_file.Open(filename, kModes[mode]);
  }
  bool Create(const wxChar* filename, bool overwrite = false)
  {
    return Open(filename, overwrite ? write : write_excl);
  }
  bool Close() { return m_file.Close(); }
  bool IsOpened() const { return m_file.IsOpened(); }
  size_t Read(void* buffer, size_t count) { return m_file.Read(buffer, count); }
  size_t Write(const void* buffer, size_t count) { return m_file.Write(buffer, count); }
  bool Write(const wxString& s, const wxMBConv& conv = wxConvUTF8) { return m_file.Write(s, conv); }
  bool Flush() { return m_file.Flush(); }
  bool Eof() const { return m_file.Eof(); }
  long Tell() const { return m_file.Tell(); }
  long Seek(long offset) { return m_file.Seek(offset) ? offset : -1; }

private:
  wxFFile m_file;
};

class wxFileName
{
public:
  wxFileName() = default;
  wxFileName(const wxString& fullpath) { Assign(fullpath); }
  wxFileName(const wxString& path, const wxString& name) { Assign(path + L"/" + name); }

  void Assign(const wxString& fullpath)
  {
    wxString path = fullpath;
    path.Replace(L"\\", L"/");
    const int slash = path.Find(L'/', true);
    m_dir = slash >= 0 ? path.Left(static_cast<size_t>(slash)) : wxString();
    const wxString fileName = slash >= 0 ? path.Mid(static_cast<size_t>(slash) + 1) : path;
    const int dot = fileName.Find(L'.', true);
    m_name = dot >= 0 ? fileName.Left(static_cast<size_t>(dot)) : fileName;
    m_ext = dot >= 0 ? fileName.Mid(static_cast<size_t>(dot) + 1) : wxString();
  }
  wxString GetPath() const { return m_dir; }
  wxString GetName() const { return m_name; }
  wxString GetExt() const { return m_ext; }
  wxString GetFullName() const { return m_ext.IsEmpty() ? m_name : m_name + L"." + m_ext; }
  wxString GetFullPath() const { return m_dir.IsEmpty() ? GetFullName() : m_dir + L"/" + GetFullName(); }
  void SetName(const wxString& name) { m_name = name; }
  void SetExt(const wxString& ext) { m_ext = ext; }
  void SetPath(const wxString& path) { m_dir = path; }
  bool HasExt() const { return !m_ext.IsEmpty(); }
  bool HasName() const { return !m_name.IsEmpty(); }
  bool FileExists() const { return wxFile::Exists(GetFullPath().c_str()); }

private:
  wxString m_dir;
  wxString m_name;
  wxString m_ext;
};

// Streams (wx/stream.h, wx/wfstream.h).
enum wxStreamError
{
  wxSTREAM_NO_ERROR = 0,
  wxSTREAM_EOF,
  wxSTREAM_WRITE_ERROR,
  wxSTREAM_READ_ERROR
};

class wxStreamBase
{
public:
  virtual ~wxStreamBase() = default;
  wxStreamError GetLastError() const { return m_lastError; }
  bool IsOk() const { return m_lastError == wxSTREAM_NO_ERROR; }
  bool Ok() const { return IsOk(); }

protected:
  wxStreamError m_lastError = wxSTREAM_NO_ERROR;
};

class wxInputStream : public wxStreamBase
{
public:
  virtual size_t OnSysRead(void* buffer, size_t size) = 0;
  wxInputStream& Read(void* buffer, size_t size)
  {
    m_lastRead = OnSysRead(buffer, size);
    if (m_lastRead < size) {
      m_lastError = wxSTREAM_EOF;
    }
    return *this;
  }
  size_t LastRead() const { return m_lastRead; }
  bool Eof() const { return m_lastError == wxSTREAM_EOF; }

private:
  size_t m_lastRead = 0;
};

class wxOutputStream : public wxStreamBase
{
public:
  virtual size_t OnSysWrite(const void* buffer, size_t size) = 0;
  wxOutputStream& Write(const void* buffer, size_t size)
  {
    m_lastWrite = OnSysWrite(buffer, size);
    if (m_lastWrite < size) {
      m_lastError = wxSTREAM_WRITE_ERROR;
    }
    return *this;
  }
  size_t LastWrite() const { return m_lastWrite; }

private:
  size_t m_lastWrite = 0;
};

class wxFFileInputStream : public wxInputStream
{
public:
  explicit wxFFileInputStream(const wxString& fileName) : m_file(fileName.c_str(), "rb")
  {
    if (!m_file.IsOpened()) {
      m_lastError = wxSTREAM_READ_ERROR;
    }
  }
  size_t OnSysRead(void* buffer, size_t size) override { return m_file.Read(buffer, size); }

private:
  wxFFile m_file;
};

class wxFFileOutputStream : public wxOutputStream
{
public:
  explicit wxFFileOutputStream(const wxString& fileName) : m_file(fileName.c_str(), "wb")
  {
    if (!m_file.IsOpened()) {
      m_lastError = wxSTREAM_WRITE_ERROR;
    }
  }
  size_t OnSysWrite(const void* buffer, size_t size) override { return m_file.Write(buffer, size); }

private:
  wxFFile m_file;
};

typedef wxFFileInputStream wxFileInputStream;
typedef wxFFileOutputStream wxFileOutputStream;

// --- URLs (wx/url.h) -----------------------------------------------------------------------------
// wxURL splits the protocol off the front ("scheme:rest") and accepts the protocols the
// library registers (http, ftp, file); anything else is wxURL_NOPROTO, as in wx 2.4.2.
enum wxURLError
{
  wxURL_NOERR = 0,
  wxURL_SNTXERR,
  wxURL_NOPROTO,
  wxURL_NOHOST,
  wxURL_NOPATH,
  wxURL_CONNERR,
  wxURL_PROTOERR
};

class wxURL : public wxObject
{
public:
  explicit wxURL(const wxString& url) : m_url(url)
  {
    const int colon = url.Find(L':');
    if (colon <= 0) {
      m_error = wxURL_SNTXERR;
      return;
    }
    m_protocolName = url.Left(static_cast<size_t>(colon));
    static const wxChar* const kProtocols[] = {L"http", L"ftp", L"file"};
    bool known = false;
    for (const wxChar* protocol : kProtocols) {
      if (m_protocolName.CmpNoCase(protocol) == 0) {
        known = true;
      }
    }
    if (!known) {
      m_error = wxURL_NOPROTO;
      return;
    }
    wxString rest = url.Mid(static_cast<size_t>(colon) + 1);
    if (rest.StartsWith(L"//", &rest)) {
      const int slash = rest.Find(L'/');
      m_hostname = slash >= 0 ? rest.Left(static_cast<size_t>(slash)) : rest;
      m_path = slash >= 0 ? rest.Mid(static_cast<size_t>(slash)) : wxString(L"/");
    } else {
      m_path = rest;
    }
  }

  wxString GetProtocolName() const { return m_protocolName; }
  wxString GetHostName() const { return m_hostname; }
  wxString GetURL() const { return m_url; }
  wxString GetPath() const { return m_path; }
  wxURLError GetError() const { return m_error; }
  wxInputStream* GetInputStream()
  {
    FAF_POSIX_STUB("wxURL::GetInputStream: no network access in the wx stand-in");
    m_error = wxURL_CONNERR;
    return nullptr;
  }

private:
  wxString m_url;
  wxString m_protocolName;
  wxString m_hostname;
  wxString m_path;
  wxURLError m_error = wxURL_NOERR;
};
