#pragma once

// Win32 on POSIX: Sockets (Winsock 2 over BSD sockets). Included only by ../WinCompat.h
// (see its rules). Implementations: ../WinCompatSockets.cpp.
//
// The engine's Winsock code (moho/net) compiles against this header with Winsock's
// behaviour where its callers depend on it:
// - SOCKET is the POSIX descriptor; INVALID_SOCKET and SOCKET_ERROR are -1, which is
//   what the engine compares against (`sock == INVALID_SOCKET`, `rc == SOCKET_ERROR`).
// - u_long is 32 bits, as on Windows (LLP64). The BSD headers typedef it as `unsigned long`
//   (64 bits on macOS), so the name is redirected to a 32-bit type by a macro once those
//   headers are in. Engine addresses (`u_long address`, host order) and ioctlsocket's
//   argument keep the Windows width.
// - WSAGetLastError() translates the thread's errno into the WSA code Windows reports for
//   the same failure (EAGAIN -> WSAEWOULDBLOCK; a non-blocking connect's EINPROGRESS is
//   WSAEWOULDBLOCK, as Winsock reports it). Callers compare these codes.
// - Calls whose Winsock signature differs from BSD in a pointer type (int* lengths) or whose
//   Winsock behaviour differs (re-enabling WSAEventSelect events, WSAEMSGSIZE on a truncated
//   datagram, no SIGPIPE) are C++ overloads taking Winsock's parameter types; for a
//   Winsock-style call (char* buffer, int length) overload resolution picks them over the
//   BSD function of the same name. The other calls (socket, bind, listen, connect,
//   setsockopt, shutdown, getaddrinfo, getnameinfo) are the BSD functions themselves.
// - select(): Winsock ignores the first argument and its fd_set is a count plus an array;
//   the engine's call sites are guarded (FAF_POSIX) to fill a BSD fd_set and pass the
//   highest descriptor + 1.
// - WSAEventSelect is implemented for the events the engine selects (FD_READ, FD_WRITE,
//   FD_ACCEPT, FD_CONNECT, FD_CLOSE): one poller thread watches the selected sockets and
//   sets the event object (an ordinary event HANDLE, compat/Sync.h) with Winsock's
//   re-enabling rules -- once FD_READ/FD_ACCEPT has been signalled it is not signalled again
//   until recv/recvfrom/accept is called on the socket, FD_WRITE not until a send would
//   block, FD_CONNECT and FD_CLOSE once per selection.

#if defined(__cplusplus)

#include <type_traits>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h> // struct pollfd and POLL*, which <winsock2.h> also declares (for WSAPoll)

// --- Basic types ---------------------------------------------------------------------------
// u_char, u_short and u_int come from <sys/types.h> with their Windows sizes. u_long there
// is `unsigned long` (64 bits); Windows' is 32.
typedef std::uint32_t faf_posix_u_long;
#define u_long faf_posix_u_long

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

static_assert(sizeof(u_short) == 2 && sizeof(u_long) == 4, "Winsock integer sizes");
static_assert(!std::is_same_v<socklen_t, int>, "the int* overloads below need socklen_t to differ from int");

typedef struct sockaddr SOCKADDR;
typedef struct sockaddr* PSOCKADDR;
typedef struct sockaddr* LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in* PSOCKADDR_IN;
typedef struct sockaddr_in* LPSOCKADDR_IN;
typedef struct in_addr IN_ADDR;
typedef struct in_addr* PIN_ADDR;
typedef struct in_addr* LPIN_ADDR;
typedef struct addrinfo ADDRINFOA;
typedef struct addrinfo* PADDRINFOA;
typedef struct hostent HOSTENT;
typedef struct hostent* PHOSTENT;
typedef struct hostent* LPHOSTENT;
typedef struct timeval TIMEVAL;

// shutdown() directions: Winsock's SD_* have the values of SHUT_*.
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

// --- Byte order ----------------------------------------------------------------------------
// The BSD headers define these as macros, which breaks Winsock-style `::htons(port)`.
// Functions with Winsock's signatures replace them (arm64 is little-endian).
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "compat/Sockets.h assumes a little-endian target"
#endif
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
constexpr u_short htons(const u_short v) noexcept { return __builtin_bswap16(v); }
constexpr u_short ntohs(const u_short v) noexcept { return __builtin_bswap16(v); }
constexpr u_long htonl(const u_long v) noexcept { return __builtin_bswap32(v); }
constexpr u_long ntohl(const u_long v) noexcept { return __builtin_bswap32(v); }

// --- Startup -------------------------------------------------------------------------------
#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128
typedef struct WSAData
{
  WORD wVersion;
  WORD wHighVersion;
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
  char szDescription[WSADESCRIPTION_LEN + 1];
  char szSystemStatus[WSASYS_STATUS_LEN + 1];
} WSADATA, *LPWSADATA;

// Nothing to initialise on POSIX: fills `data` for the version asked (up to 2.2) and returns
// 0. Also makes writes to a closed connection fail with EPIPE instead of raising SIGPIPE,
// which Windows does not have.
int WSAStartup(WORD versionRequested, LPWSADATA data) noexcept;
int WSACleanup() noexcept;

// --- Errors (winerror.h values) ------------------------------------------------------------
#define WSABASEERR 10000
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAESOCKTNOSUPPORT 10044
#define WSAEOPNOTSUPP 10045
#define WSAEPFNOSUPPORT 10046
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAESHUTDOWN 10058
#define WSAETOOMANYREFS 10059
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAELOOP 10062
#define WSAENAMETOOLONG 10063
#define WSAEHOSTDOWN 10064
#define WSAEHOSTUNREACH 10065
#define WSAENOTEMPTY 10066
#define WSAEPROCLIM 10067
#define WSAEUSERS 10068
#define WSAEDQUOT 10069
#define WSAESTALE 10070
#define WSAEREMOTE 10071
#define WSASYSNOTREADY 10091
#define WSAVERNOTSUPPORTED 10092
#define WSANOTINITIALISED 10093
#define WSAEDISCON 10101
#define WSAHOST_NOT_FOUND 11001
#define WSATRY_AGAIN 11002
#define WSANO_RECOVERY 11003
#define WSANO_DATA 11004

// The WSA code for the calling thread's last socket failure (translated from errno; codes
// with no Winsock counterpart are returned as is).
int WSAGetLastError() noexcept;

// --- Socket calls with Winsock's semantics ---------------------------------------------------
// closesocket also cancels the socket's WSAEventSelect.
int closesocket(SOCKET s) noexcept;
// FIONBIO (non-blocking on/off; turning it off fails with WSAEINVAL while WSAEventSelect is
// active, as on Windows) and FIONREAD (bytes readable).
int ioctlsocket(SOCKET s, long cmd, u_long* argp) noexcept;

// Overloads with Winsock's parameter types (see the top of this file). They retry on EINTR
// (Winsock calls are not interrupted by signals), send without SIGPIPE, and re-enable the
// socket's selected events as Winsock does.
int recv(SOCKET s, char* buf, int len, int flags) noexcept;
// A datagram larger than `len` fails with WSAEMSGSIZE (the truncated data is in `buf` and
// the datagram is consumed), as on Windows; BSD would return the truncated length.
int recvfrom(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen) noexcept;
int send(SOCKET s, const char* buf, int len, int flags) noexcept;
int sendto(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) noexcept;
// The accepted socket has the listening socket's properties, as on Windows: non-blocking
// mode and WSAEventSelect selection.
SOCKET accept(SOCKET s, struct sockaddr* addr, int* addrlen) noexcept;
int getsockname(SOCKET s, struct sockaddr* name, int* namelen) noexcept;
int getpeername(SOCKET s, struct sockaddr* name, int* namelen) noexcept;

// --- Event objects and WSAEventSelect --------------------------------------------------------
// A WSAEVENT is an ordinary manual-reset event HANDLE (compat/Sync.h): it can be waited on
// with WaitForSingleObject/MsgWaitForMultipleObjectsEx and set/reset with SetEvent/ResetEvent,
// and an event from CreateEventW can be passed to WSAEventSelect.
typedef HANDLE WSAEVENT;
typedef WSAEVENT* LPWSAEVENT;
#define WSA_INVALID_EVENT (static_cast<WSAEVENT>(nullptr))
#define WSA_MAXIMUM_WAIT_EVENTS MAXIMUM_WAIT_OBJECTS
#define WSA_WAIT_EVENT_0 WAIT_OBJECT_0
#define WSA_WAIT_IO_COMPLETION WAIT_IO_COMPLETION
#define WSA_WAIT_TIMEOUT WAIT_TIMEOUT
#define WSA_WAIT_FAILED WAIT_FAILED
#define WSA_INFINITE INFINITE

#define FD_READ_BIT 0
#define FD_READ (1 << FD_READ_BIT)
#define FD_WRITE_BIT 1
#define FD_WRITE (1 << FD_WRITE_BIT)
#define FD_OOB_BIT 2
#define FD_OOB (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT 3
#define FD_ACCEPT (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT 4
#define FD_CONNECT (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT 5
#define FD_CLOSE (1 << FD_CLOSE_BIT)
#define FD_MAX_EVENTS 10
#define FD_ALL_EVENTS ((1 << FD_MAX_EVENTS) - 1)

WSAEVENT WSACreateEvent() noexcept;
// A socket still selected with the event keeps it alive (its own reference), as on Windows.
BOOL WSACloseEvent(WSAEVENT event) noexcept;
BOOL WSASetEvent(WSAEVENT event) noexcept;
BOOL WSAResetEvent(WSAEVENT event) noexcept;
// Returns WSA_WAIT_EVENT_0 + i, WSA_WAIT_TIMEOUT, WSA_WAIT_IO_COMPLETION (alertable wait
// that ran an APC) or WSA_WAIT_FAILED (WaitForMultipleObjectsEx).
DWORD WSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD timeoutMs,
                               BOOL alertable) noexcept;
// Associates `event` with the network events in `networkEvents` (0, or a null event, cancels
// the selection) and makes the socket non-blocking. Replaces any earlier selection; events
// whose condition already holds are signalled at once. FD_OOB is accepted, never signalled.
// Fails with WSAENOTSOCK for a descriptor that is not a socket, WSAEINVAL for a bad event.
int WSAEventSelect(SOCKET s, WSAEVENT event, long networkEvents) noexcept;

#endif // defined(__cplusplus)
