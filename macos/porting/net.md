# Porting notes: net (Winsock -> BSD sockets)

Owns `src/moho/net/**` and the compat area Sockets
(`src/platform/posix/compat/Sockets.h`, `src/platform/posix/WinCompatSockets.cpp`).
Nothing is excluded or stubbed (`macos/exclusions.d/net.cmake` only says so).

## Status

35 of 39 `src/moho/net` TUs compile. The other four are blocked outside this package:
CNetUDPConnector.cpp needs one CRT function (`_dupenv_s`), and three need wx headers.

| TU | Blocked by |
|---|---|
| `CNetUDPConnector.cpp` | `_dupenv_s` (CRT; no compat area declares it yet) |
| `CGpgNetInterface.cpp` | `wx/defs.h` (wx package, via `moho/app/WxRuntimeTypes.h`); behind it: `CreateProcessA`/`STARTUPINFOA`/`PROCESS_INFORMATION`, `ShellExecuteExW`, `platform::SetX87PrecisionControl` |
| `CLobby.cpp` | `std::unary_function` in boost 1.34 `algorithm/string` (removed from libc++ C++17), then `wx/defs.h` (via `moho/ui/UiRuntimeTypes.h`) |
| `IClientMgrUIInterface.cpp` | `wx/defs.h` (via `moho/ui/UiRuntimeTypes.h`) |

## compat/Sockets.h: what the engine gets

- **Types.** `SOCKET` is the POSIX descriptor (`int`); `INVALID_SOCKET` and `SOCKET_ERROR`
  are -1. `SOCKADDR`, `SOCKADDR_IN`, `IN_ADDR`, `ADDRINFOA`, `HOSTENT`, `TIMEVAL`, `WSADATA`,
  `SD_*` (= `SHUT_*`) are aliases of the BSD types and values.
- **`u_long` is 32 bits**, as on Windows. `<sys/types.h>` typedefs it as `unsigned long`
  (64-bit), so after the BSD headers `u_long` is a macro for `faf_posix_u_long`
  (`std::uint32_t`). This is a wire-format fix, not cosmetics. `CLobby::OnNewPeer` reads
  the peer address with `BinaryReader::ReadExact(u_long&)`. With 8 bytes it would desync
  the lobby protocol with Windows peers. Every other value the net package reads or writes
  was already fixed-width (checked: CClientBase, CReplayClient, Common, CLobby,
  CDiscoveryService, CGpgNetInterface, CClientManagerImpl's marshaller). `u_short`,
  `u_char` and `u_int` are the BSD typedefs, which have the Windows sizes.
- **`htons`/`ntohs`/`htonl`/`ntohl`.** The BSD macros break Winsock-style `::htons(x)`, so
  they are `#undef`'d and replaced by `constexpr` functions with Winsock's signatures.
- **Errors.** `WSAGetLastError()` maps the thread's `errno` to the WSA code Windows
  reports. EAGAIN becomes WSAEWOULDBLOCK, and so does EINPROGRESS, because Winsock
  reports a non-blocking `connect` as WSAEWOULDBLOCK, which the engine tests for. EBADF
  becomes WSAENOTSOCK and EPIPE becomes WSAECONNRESET. Codes with no Winsock counterpart
  pass through unchanged (`NET_GetWinsockErrorString` prints "UNKNOWN").
- **Winsock-signature overloads.** These are C++ overloads beside the BSD functions:
  `recv(SOCKET, char*, int, int)`, `recvfrom(..., int* fromlen)`, `send`, `sendto`,
  `accept(..., int*)`, `getsockname(..., int*)` and `getpeername(..., int*)`. Overload
  resolution picks them for Winsock-style calls (char buffer, `int` length). They retry
  EINTR, because Winsock calls are not interrupted by signals. They re-enable
  WSAEventSelect events (see below). A blocking `send` finishes the whole buffer, as a
  blocking Winsock send does. `recvfrom` fails with **WSAEMSGSIZE** on a datagram larger
  than the buffer, as Winsock does; the datagram is consumed (it is read with `recvmsg` +
  `MSG_TRUNC`). BSD would hand back a truncated packet, which the UDP connector would then
  parse. `accept` gives the new socket the listener's non-blocking mode and selection.
  - Caveat: a call that passes a `size_t` length (`sizeof buf`) together with a `char*`
    buffer is ambiguous between the two `recv`/`send` overloads, and compilation fails. All
    engine calls cast to `int`, as the Winsock signature makes MSVC require.
  - `socket`, `bind`, `listen`, `connect`, `setsockopt`, `shutdown`, `getaddrinfo` and
    `getnameinfo` are the BSD functions themselves. Their signatures accept the engine's
    arguments.
- **`closesocket`** cancels the selection and then calls `close()`. It refuses a
  descriptor that is not a socket (WSAENOTSOCK) instead of closing a file. On macOS 27,
  `close()` and `shutdown()` both wake a thread blocked in `recv` on the socket, as on
  Windows; this was tested.
- **`ioctlsocket`.** `FIONBIO` maps to `O_NONBLOCK`. Turning it off while WSAEventSelect is
  active fails with WSAEINVAL, as on Windows. `FIONREAD` is also supported.
- **`WSAStartup`/`WSACleanup`.** Nothing needs initialising. WSAStartup fills `WSADATA`
  (version up to 2.2) and sets SIGPIPE to ignored if it is still at the default (Windows
  has no SIGPIPE: writing to a closed connection must fail, not kill the process).
  `NET_Init` runs WSAStartup before any engine socket exists.
- **Event objects.** A `WSAEVENT` is an ordinary Sync-area manual-reset event HANDLE
  (`WSACreateEvent` = `CreateEventW(nullptr, TRUE, FALSE, nullptr)`), so CWaitHandleSet,
  `WaitForSingleObject` and `SetEvent`/`ResetEvent` all work on it. `CLobby` passes a
  `CreateEventW` event to `WSAEventSelect`, which works too. `WSAWaitForMultipleEvents` is
  `WaitForMultipleObjectsEx`; alertable waits run APCs.
- **`WSAEventSelect`** is fully implemented for FD_READ, FD_WRITE, FD_ACCEPT, FD_CONNECT
  and FD_CLOSE. FD_OOB is accepted and never signalled; the engine does not use it. One
  poller thread ("FAF WSAEventSelect") `poll()`s every selected socket plus a wake pipe,
  and sets the event with Winsock's re-enabling rules:
  - FD_READ: once, then again only after `recv`/`recvfrom`.
  - FD_ACCEPT: once, then again only after `accept`.
  - FD_WRITE: once, then again after a send that would block.
  - FD_CONNECT and FD_CLOSE: once per selection.

  A selection is disarmed only when it has just signalled. The application then reacts
  with a re-enabling call (Winsock's contract), so no wakeup is lost and an unserviced
  socket does not spin the poller. The engine depends on exactly this: CNetUDPConnector
  drains in batches and relies on "recvfrom re-arms while data remains", and CLobby resets
  its event with plain `ResetEvent` before pulling. Each selection holds its own
  `DuplicateHandle` reference to the event, like Winsock's kernel reference, so the
  application may close its handle first, and a stale handle value cannot signal a newer
  object. Other details:
  - Selecting makes the socket non-blocking.
  - A null event or a zero mask cancels the selection.
  - An already connected socket gets no FD_CONNECT.
  - A stream selected for FD_ACCEPT without a peer counts as listening. macOS has no
    `getsockopt(SO_ACCEPTCONN)` (it returns ENOPROTOOPT), and WSAEventSelect may come
    before `listen()`.
  - FD_CLOSE is detected by a `MSG_PEEK` returning 0 or an error.

### Tests (scratch, not in the tree)

A runtime test linked WinCompatSockets/Sync/Threads (with a stand-in for Errors, which
another package was editing). It covered:

- UDP FD_WRITE/FD_READ re-arming, WSAEMSGSIZE, FIONBIO/FIONREAD.
- 0.000 s CPU over 0.5 s idle with an unread datagram (no spinning).
- Single- and multi-event waits, and closing the event while it is still selected.
- TCP FD_ACCEPT/FD_CONNECT/FD_READ/FD_CLOSE, a refused connect (SO_ERROR -> WSAECONNREFUSED),
  inherited selection and non-blocking mode on accept, WSAEventSelect before `listen()`.
- WSAENOTSOCK on non-sockets, getaddrinfo through ADDRINFOA, htons/ntohl.

A stress test ran a CNetUDPConnector-shaped loop (batch drain, wait, reset): 80,000
datagrams from 4 bursty senders. It also ran a CLobby-shaped loop (`ResetEvent`, then
accept/recv): 8 MB from 8 TCP clients. Both completed with no lost wakeups, three times
each.

## Guarded edits to engine files (`#if defined(FAF_POSIX)` / `#else`, Windows unchanged)

- `Common.cpp`:
  - `NET_Init` and `NET_GetWinsockErrorString`: `#if defined(_WIN32)` became
    `#if defined(_WIN32) || defined(FAF_POSIX)`. Upstream's non-Windows branch made
    `NET_Init` return false, which disabled networking.
  - `NET_MakeUDPConnector`: `sin_addr.S_un.S_addr` becomes `sin_addr.s_addr` (BSD
    `in_addr` has no `S_un`).
- `CNetUDPConnector.cpp`:
  - Ten `_WIN32` guards gain `|| defined(FAF_POSIX)`: socket event signalling, the
    destructor's `closesocket`, and `SetEvent(mSelectedEvent)`. Without them the POSIX
    build never closed the socket or woke the connector thread.
  - `SendData` gets the same `S_un` fix as Common.cpp.
  - Left as upstream: the `errno` branch in `ReceiveData`, which works as is, and
    `SetThreadPriority(GetCurrentThread(), 2)` (still `_WIN32`; the Threads area only
    records priorities, so this is a scheduling hint with no effect).
- `CNetUDPConnection.cpp`: eight `_WIN32` guards gain `|| defined(FAF_POSIX)`. These cover
  connector wakeups, `SetEvent(mSelectedEvent)`, and the `SendPacket` address setup, whose
  `#else` was `#error "Implement me on non-Windows platform!"`.
- `CNetTCPBuf.cpp` (`Read`'s non-blocking test and `Select`): the zero-timeout
  `select(1, ...)` on a Winsock `fd_set` (`fd_count`/`fd_array`) becomes `poll(POLLIN, 0)`.
  This covers the GPGNet link, and `poll()` has no FD_SETSIZE limit.
- `CNetTCPConnection.cpp` (`Pull`, connecting state): fills a BSD `fd_set` and calls
  `select(mSocket + 1, ...)`. If the socket comes back writable, it reads `SO_ERROR`: POSIX
  reports a finished non-blocking connect as writable whether it succeeded or failed,
  while Winsock puts a failure in `exceptfds`. A failure is therefore moved to `exceptFds`,
  so the shared code after it is unchanged.
- `CHostManager.cpp` (`NET_GetAddrInfo` failure log): POSIX `getaddrinfo` returns EAI_*
  codes, which FormatMessage does not know, so the log text comes from `gai_strerror`.

## Open issues

- `CNetTCPConnection::Pull` still uses `select` + `FD_SET`. That is correct while
  descriptors stay below FD_SETSIZE (1024). The process open-file limit (soft limit 256 by
  default on macOS) keeps them there unless someone raises it past 1024. The CNetTCPBuf
  sites use `poll()`.
- The packet log (`net_LogPackets`) uses `%TEMP%`, which is not normally set on macOS, so
  the log turns itself off with the engine's own message. It could map to `$TMPDIR` in
  whichever area implements `_dupenv_s`.
- A TCP listener bound to a port that still has TIME_WAIT connections fails on BSD
  without SO_REUSEADDR. Windows' default is more lenient. This only affects
  `NET_MakeTCPConnector` and `NET_CreateTCPServer`, which are rarely used: FAF plays over
  UDP and connects out to GPGNet. Not changed.
- Upstream `src/platform/Platform.h` defines `LODWORD(x)`/`HIDWORD(x)` through
  `unsigned long*`, which is 8 bytes on macOS. This is not net code, so it is only flagged
  here.

## Lead requests

1. `_dupenv_s(char** buffer, size_t* count, const char* name)` (CRT environment; used by
   CNetUDPConnector and moho/misc/CrtRuntimeHelpers.cpp) in whichever area owns the CRT
   (Strings or Misc). Return 0 with `*buffer = nullptr` when the variable is unset; the
   buffer must be `free()`-able. Optionally map `TEMP`/`TMP` to `$TMPDIR`.
2. boost 1.34 `boost/algorithm/string` uses `std::unary_function`/`std::binary_function`,
   which libc++ removed in C++17. Either define
   `_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION` in `macos/CMakeLists.txt` or
   extend `dependencies/patches/boost_1_34_1_faf_macos.patch`. This blocks CLobby.cpp.
3. wx package: CGpgNetInterface.cpp, CLobby.cpp and IClientMgrUIInterface.cpp reach
   `platform/WxWidgets.h` through `moho/app/WxRuntimeTypes.h` and
   `moho/ui/UiRuntimeTypes.h`.
4. Threads/Misc area: `CreateProcessA` (+ `STARTUPINFOA`, `PROCESS_INFORMATION`) and
   `ShellExecuteExW` for CGpgNetInterface's `LaunchGPGNet` path (the game starting its own
   lobby client). FAF's normal path is `/gpgnet host:port` with `NET_TCPConnect` and does
   not use them.
