#pragma once

// Win32 on POSIX: Files. Included only by ../WinCompat.h (see its rules).
// Implementations: ../WinCompatFiles.cpp.
//
// Paths. The engine works with Windows paths ("c:\dir\file", "c:/dir/file", UNC, and its
// own drive arithmetic: _getdrive, FILE_HasDrive, ...), so the layer presents the POSIX
// filesystem as drive C:, the way Wine presents it as Z:.
//  - Paths going in: '\' and '/' are both separators, and a leading drive "X:" (any
//    letter) stands for the root "/". "C:\Users\me\x" and "/Users/me/x" are one file.
//  - Paths coming out (current directory, module path, full path, temp path, ...) are
//    Windows-form: "C:\Users\me\x".
//  - Names are matched case-insensitively when the volume is case-sensitive (a lookup that
//    fails is retried component by component ignoring case), as on NTFS. The default
//    APFS volume is case-insensitive already.
// Wide (W) functions take 32-bit wchar_t text and convert it to UTF-8; A functions take
// UTF-8 (the layer's ANSI code page is CP_UTF8, see Strings.h).
//
// I/O is synchronous: an OVERLAPPED read/write completes before the call returns (its
// event is set, GetOverlappedResult reports it). Share modes are not enforced.

#include <cstdio>

#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define GENERIC_EXECUTE 0x20000000L
#define GENERIC_ALL 0x10000000L
#define DELETE 0x00010000L
#define FILE_READ_DATA 0x0001
#define FILE_LIST_DIRECTORY 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_ADD_FILE 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_ADD_SUBDIRECTORY 0x0004
#define FILE_READ_EA 0x0008
#define FILE_WRITE_EA 0x0010
#define FILE_EXECUTE 0x0020
#define FILE_TRAVERSE 0x0020
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define FILE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1FF)
#define FILE_GENERIC_READ (STANDARD_RIGHTS_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE)
#define FILE_GENERIC_WRITE                                                                                  \
  (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | FILE_APPEND_DATA | SYNCHRONIZE)
#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_EXECUTE | FILE_READ_ATTRIBUTES | FILE_EXECUTE | SYNCHRONIZE)

#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5

#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_DEVICE 0x00000040
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE 0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT 0x00000400
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800
#define FILE_ATTRIBUTE_OFFLINE 0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED 0x00004000
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_POSIX_SEMANTICS 0x01000000
#define FILE_FLAG_OPEN_REPARSE_POINT 0x00200000

#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

#define FILE_TYPE_UNKNOWN 0x0000
#define FILE_TYPE_DISK 0x0001
#define FILE_TYPE_CHAR 0x0002
#define FILE_TYPE_PIPE 0x0003

#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004
#define MOVEFILE_WRITE_THROUGH 0x00000008
#define REPLACEFILE_WRITE_THROUGH 0x00000001
#define REPLACEFILE_IGNORE_MERGE_ERRORS 0x00000002
#define REPLACEFILE_IGNORE_ACL_ERRORS 0x00000004
#define ERROR_UNABLE_TO_REMOVE_REPLACED 1175L
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT 1176L
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 1177L

#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)

#define FILE_NOTIFY_CHANGE_FILE_NAME 0x00000001
#define FILE_NOTIFY_CHANGE_DIR_NAME 0x00000002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES 0x00000004
#define FILE_NOTIFY_CHANGE_SIZE 0x00000008
#define FILE_NOTIFY_CHANGE_LAST_WRITE 0x00000010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS 0x00000020
#define FILE_NOTIFY_CHANGE_CREATION 0x00000040
#define FILE_NOTIFY_CHANGE_SECURITY 0x00000100
#define FILE_ACTION_ADDED 0x00000001
#define FILE_ACTION_REMOVED 0x00000002
#define FILE_ACTION_MODIFIED 0x00000003
#define FILE_ACTION_RENAMED_OLD_NAME 0x00000004
#define FILE_ACTION_RENAMED_NEW_NAME 0x00000005

typedef struct _FILE_NOTIFY_INFORMATION {
  DWORD NextEntryOffset;
  DWORD Action;
  DWORD FileNameLength; // bytes
  WCHAR FileName[1];
} FILE_NOTIFY_INFORMATION, *PFILE_NOTIFY_INFORMATION;

typedef struct _OVERLAPPED {
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    PVOID Pointer;
  };
  HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;
typedef void(WINAPI* LPOVERLAPPED_COMPLETION_ROUTINE)(DWORD errorCode, DWORD bytesTransferred, LPOVERLAPPED overlapped);

typedef struct _WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef struct _WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[MAX_PATH];
  WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef struct _BY_HANDLE_FILE_INFORMATION {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD dwVolumeSerialNumber;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD nNumberOfLinks;
  DWORD nFileIndexHigh;
  DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *PBY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

// --- Handles and I/O ---------------------------------------------------------------------------
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                   DWORD flags, HANDLE templateFile) noexcept;
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                   DWORD flags, HANDLE templateFile) noexcept;
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped) noexcept;
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD bytesToWrite, LPDWORD bytesWritten, LPOVERLAPPED overlapped) noexcept;
DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD moveMethod) noexcept;
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPosition, DWORD moveMethod) noexcept;
DWORD GetFileSize(HANDLE file, LPDWORD fileSizeHigh) noexcept;
BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER fileSize) noexcept;
BOOL SetEndOfFile(HANDLE file) noexcept;
BOOL FlushFileBuffers(HANDLE file) noexcept;
DWORD GetFileType(HANDLE file) noexcept;
BOOL GetFileTime(HANDLE file, LPFILETIME creationTime, LPFILETIME lastAccessTime, LPFILETIME lastWriteTime) noexcept;
BOOL SetFileTime(HANDLE file, const FILETIME* creationTime, const FILETIME* lastAccessTime,
                 const FILETIME* lastWriteTime) noexcept;
BOOL GetFileInformationByHandle(HANDLE file, LPBY_HANDLE_FILE_INFORMATION information) noexcept;
HANDLE GetStdHandle(DWORD stdHandle) noexcept;
BOOL SetStdHandle(DWORD stdHandle, HANDLE handle) noexcept;
BOOL GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD bytesTransferred, BOOL wait) noexcept;
BOOL CancelIo(HANDLE file) noexcept;
BOOL CancelIoEx(HANDLE file, LPOVERLAPPED overlapped) noexcept;
// Directory change notification is not implemented yet (FAF_POSIX_STUB; fails with
// ERROR_NOT_SUPPORTED): only the hot-reload disk watcher uses it.
BOOL ReadDirectoryChangesW(HANDLE directory, LPVOID buffer, DWORD bufferLength, BOOL watchSubtree, DWORD notifyFilter,
                           LPDWORD bytesReturned, LPOVERLAPPED overlapped,
                           LPOVERLAPPED_COMPLETION_ROUTINE completionRoutine) noexcept;

// --- Names and attributes ----------------------------------------------------------------------
DWORD GetFileAttributesA(LPCSTR path) noexcept;
DWORD GetFileAttributesW(LPCWSTR path) noexcept;
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID information) noexcept;
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID information) noexcept;
BOOL SetFileAttributesA(LPCSTR path, DWORD attributes) noexcept;
BOOL SetFileAttributesW(LPCWSTR path, DWORD attributes) noexcept;
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data) noexcept;
HANDLE FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW data) noexcept;
BOOL FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data) noexcept;
BOOL FindNextFileW(HANDLE find, LPWIN32_FIND_DATAW data) noexcept;
BOOL FindClose(HANDLE find) noexcept;
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attributes) noexcept;
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES attributes) noexcept;
BOOL RemoveDirectoryA(LPCSTR path) noexcept;
BOOL RemoveDirectoryW(LPCWSTR path) noexcept;
BOOL DeleteFileA(LPCSTR path) noexcept;
BOOL DeleteFileW(LPCWSTR path) noexcept;
BOOL MoveFileA(LPCSTR existing, LPCSTR target) noexcept;
BOOL MoveFileW(LPCWSTR existing, LPCWSTR target) noexcept;
BOOL MoveFileExA(LPCSTR existing, LPCSTR target, DWORD flags) noexcept;
BOOL MoveFileExW(LPCWSTR existing, LPCWSTR target, DWORD flags) noexcept;
BOOL CopyFileA(LPCSTR existing, LPCSTR target, BOOL failIfExists) noexcept;
BOOL CopyFileW(LPCWSTR existing, LPCWSTR target, BOOL failIfExists) noexcept;
BOOL ReplaceFileA(LPCSTR replaced, LPCSTR replacement, LPCSTR backup, DWORD flags, LPVOID exclude,
                  LPVOID reserved) noexcept;
BOOL ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID exclude,
                  LPVOID reserved) noexcept;
DWORD GetCurrentDirectoryA(DWORD bufferLength, LPSTR buffer) noexcept;
DWORD GetCurrentDirectoryW(DWORD bufferLength, LPWSTR buffer) noexcept;
BOOL SetCurrentDirectoryA(LPCSTR path) noexcept;
BOOL SetCurrentDirectoryW(LPCWSTR path) noexcept;
DWORD GetFullPathNameA(LPCSTR path, DWORD bufferLength, LPSTR buffer, LPSTR* filePart) noexcept;
DWORD GetFullPathNameW(LPCWSTR path, DWORD bufferLength, LPWSTR buffer, LPWSTR* filePart) noexcept;
DWORD GetTempPathA(DWORD bufferLength, LPSTR buffer) noexcept;
DWORD GetTempPathW(DWORD bufferLength, LPWSTR buffer) noexcept;
UINT GetTempFileNameA(LPCSTR directory, LPCSTR prefix, UINT unique, LPSTR tempFileName) noexcept;
UINT GetTempFileNameW(LPCWSTR directory, LPCWSTR prefix, UINT unique, LPWSTR tempFileName) noexcept;

// <shlwapi.h> / <shlobj.h> path helpers that touch the filesystem.
BOOL PathFileExistsA(LPCSTR path) noexcept;
BOOL PathFileExistsW(LPCWSTR path) noexcept;
int SHCreateDirectoryExA(HWND window, LPCSTR path, const SECURITY_ATTRIBUTES* attributes) noexcept;
int SHCreateDirectoryExW(HWND window, LPCWSTR path, const SECURITY_ATTRIBUTES* attributes) noexcept;

// SHFileOperation: FO_DELETE (wildcards in the last component; FOF_ALLOWUNDO moves to
// ~/.Trash as the Recycle Bin would), FO_MOVE/FO_RENAME (rename), FO_COPY (recursive copy).
// Returns 0 on success, a Win32 error code otherwise. No UI is ever shown.
#define FO_MOVE 0x0001
#define FO_COPY 0x0002
#define FO_DELETE 0x0003
#define FO_RENAME 0x0004
#define FOF_MULTIDESTFILES 0x0001
#define FOF_CONFIRMMOUSE 0x0002
#define FOF_SILENT 0x0004
#define FOF_RENAMEONCOLLISION 0x0008
#define FOF_NOCONFIRMATION 0x0010
#define FOF_WANTMAPPINGHANDLE 0x0020
#define FOF_ALLOWUNDO 0x0040
#define FOF_FILESONLY 0x0080
#define FOF_SIMPLEPROGRESS 0x0100
#define FOF_NOCONFIRMMKDIR 0x0200
#define FOF_NOERRORUI 0x0400
#define FOF_NOCOPYSECURITYATTRIBS 0x0800
#define FOF_NORECURSION 0x1000
#define FOF_NO_UI (FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)
typedef WORD FILEOP_FLAGS;
typedef struct _SHFILEOPSTRUCTA {
  HWND hwnd;
  UINT wFunc;
  LPCSTR pFrom;
  LPCSTR pTo;
  FILEOP_FLAGS fFlags;
  BOOL fAnyOperationsAborted;
  LPVOID hNameMappings;
  LPCSTR lpszProgressTitle;
} SHFILEOPSTRUCTA, *LPSHFILEOPSTRUCTA;
typedef struct _SHFILEOPSTRUCTW {
  HWND hwnd;
  UINT wFunc;
  LPCWSTR pFrom;
  LPCWSTR pTo;
  FILEOP_FLAGS fFlags;
  BOOL fAnyOperationsAborted;
  LPVOID hNameMappings;
  LPCWSTR lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;
int SHFileOperationA(LPSHFILEOPSTRUCTA operation) noexcept;
int SHFileOperationW(LPSHFILEOPSTRUCTW operation) noexcept;

// --- File mappings ------------------------------------------------------------------------------
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
#define PAGE_WRITECOMBINE 0x400
#define SEC_COMMIT 0x08000000
#define SEC_RESERVE 0x04000000
#define SEC_IMAGE 0x01000000
#define FILE_MAP_COPY 0x0001
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define FILE_MAP_EXECUTE 0x0020
#define FILE_MAP_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x001F)
HANDLE CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES attributes, DWORD protect, DWORD maximumSizeHigh,
                          DWORD maximumSizeLow, LPCSTR name) noexcept;
HANDLE CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES attributes, DWORD protect, DWORD maximumSizeHigh,
                          DWORD maximumSizeLow, LPCWSTR name) noexcept;
LPVOID MapViewOfFile(HANDLE mapping, DWORD access, DWORD offsetHigh, DWORD offsetLow, SIZE_T bytes) noexcept;
BOOL UnmapViewOfFile(LPCVOID baseAddress) noexcept;
BOOL FlushViewOfFile(LPCVOID baseAddress, SIZE_T bytes) noexcept;

// --- CRT files (<io.h>, <direct.h>, <stdio.h> secure variants) ---------------------------------
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20
struct __finddata64_t {
  unsigned attrib;
  __time64_t time_create;
  __time64_t time_access;
  __time64_t time_write;
  __int64 size;
  char name[260];
};
struct _wfinddata64_t {
  unsigned attrib;
  __time64_t time_create;
  __time64_t time_access;
  __time64_t time_write;
  __int64 size;
  wchar_t name[260];
};
std::intptr_t _findfirst64(const char* pattern, struct __finddata64_t* data) noexcept;
int _findnext64(std::intptr_t handle, struct __finddata64_t* data) noexcept;
std::intptr_t _wfindfirst64(const wchar_t* pattern, struct _wfinddata64_t* data) noexcept;
int _wfindnext64(std::intptr_t handle, struct _wfinddata64_t* data) noexcept;
int _findclose(std::intptr_t handle) noexcept;

// fopen family: MSVC mode strings ("rt", "wb", "a+, ccs=UTF-8", ...) and Windows paths.
#define _SH_DENYRW 0x10
#define _SH_DENYWR 0x20
#define _SH_DENYRD 0x30
#define _SH_DENYNO 0x40
#define _SH_SECURE 0x80
errno_t fopen_s(FILE** stream, const char* path, const char* mode) noexcept;
errno_t _wfopen_s(FILE** stream, const wchar_t* path, const wchar_t* mode) noexcept;
FILE* _wfopen(const wchar_t* path, const wchar_t* mode) noexcept;
FILE* _fsopen(const char* path, const char* mode, int shareFlag) noexcept;
FILE* _wfsopen(const wchar_t* path, const wchar_t* mode, int shareFlag) noexcept;
errno_t freopen_s(FILE** stream, const char* path, const char* mode, FILE* old) noexcept;
int _wremove(const wchar_t* path) noexcept;
int _wrename(const wchar_t* oldPath, const wchar_t* newPath) noexcept;
int _wunlink(const wchar_t* path) noexcept;
inline int _fileno(FILE* stream) noexcept { return fileno(stream); }
inline int _isatty(int fd) noexcept { return ::isatty(fd); }
inline int _close(int fd) noexcept { return ::close(fd); }
inline int _dup(int fd) noexcept { return ::dup(fd); }
inline int _dup2(int fd, int target) noexcept { return ::dup2(fd, target); }
int _unlink(const char* path) noexcept;
long _lseek(int fd, long offset, int origin) noexcept;
__int64 _lseeki64(int fd, __int64 offset, int origin) noexcept;
long _tell(int fd) noexcept;
__int64 _telli64(int fd) noexcept;
long _filelength(int fd) noexcept;
__int64 _filelengthi64(int fd) noexcept;
int _commit(int fd) noexcept;
int _chsize(int fd, long size) noexcept;
errno_t _chsize_s(int fd, __int64 size) noexcept;
int _fseeki64(FILE* stream, __int64 offset, int origin) noexcept;
__int64 _ftelli64(FILE* stream) noexcept;

// <direct.h>: the current drive is always C: (3).
int _mkdir(const char* path) noexcept;
int _wmkdir(const wchar_t* path) noexcept;
int _rmdir(const char* path) noexcept;
int _wrmdir(const wchar_t* path) noexcept;
int _chdir(const char* path) noexcept;
int _wchdir(const wchar_t* path) noexcept;
char* _getcwd(char* buffer, int maxLength) noexcept;
wchar_t* _wgetcwd(wchar_t* buffer, int maxLength) noexcept;
char* _getdcwd(int drive, char* buffer, int maxLength) noexcept;
wchar_t* _wgetdcwd(int drive, wchar_t* buffer, int maxLength) noexcept;
int _getdrive() noexcept;
int _chdrive(int drive) noexcept;
char* _fullpath(char* absolutePath, const char* relativePath, std::size_t maxLength) noexcept;
wchar_t* _wfullpath(wchar_t* absolutePath, const wchar_t* relativePath, std::size_t maxLength) noexcept;
int _access(const char* path, int mode) noexcept;
int _waccess(const wchar_t* path, int mode) noexcept;
errno_t _access_s(const char* path, int mode) noexcept;
errno_t _waccess_s(const wchar_t* path, int mode) noexcept;

// POSIX extension, not Win32: the host path this layer uses for a Windows-form path
// (separators, drive letter and case resolution as above), for engine code that hands a path
// to a C++ standard library API (std::ofstream, std::filesystem), which does not go through
// this layer and would otherwise take "C:\Users\..." as a relative file name. UTF-8 out.
// Returns the length without the terminator, or, when `capacity` is too small, the size
// needed including the terminator (nothing written). 0 for a null path.
std::size_t faf_posixHostPathA(const char* windowsPath, char* buffer, std::size_t capacity) noexcept;
std::size_t faf_posixHostPathW(const wchar_t* windowsPath, char* buffer, std::size_t capacity) noexcept;
