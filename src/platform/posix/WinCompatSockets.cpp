// Win32 on POSIX: Sockets (declarations in compat/Sockets.h).
//
// Winsock over BSD sockets. Most of it is translation (errno -> WSA codes, int* lengths,
// FIONBIO). The part with moving pieces is WSAEventSelect: Winsock sets an event object when
// a selected network event happens on a socket, and the engine waits on those events (the
// UDP connector's thread, the lobby's and sim driver's wait sets). Here one poller thread
// poll()s every selected socket and sets the events, following Winsock's re-enabling rules
// so that a socket the application has not serviced yet does not keep the poller spinning:
//
//   FD_READ    signalled once, then again only after recv/recvfrom on the socket
//   FD_ACCEPT  signalled once, then again only after accept on the socket
//   FD_WRITE   signalled once, then again only after a send/sendto that would block
//   FD_CONNECT signalled once per selection, when a connect completes (or fails)
//   FD_CLOSE   signalled once per selection, when the peer closes or resets
//
// An event is only disarmed when it has just been signalled, so the application, which
// must react to the signal with a re-enabling call (that is Winsock's contract too), always
// re-arms it: no wakeup is lost.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

namespace
{
  // --- errno -> WSA codes ----------------------------------------------------------------
  int WsaErrorFromErrno(const int e) noexcept
  {
    switch (e) {
    case 0:
      return 0;
    case EINTR:
      return WSAEINTR;
    case EBADF: // Winsock reports a closed or bogus SOCKET as not a socket
    case ENOTSOCK:
      return WSAENOTSOCK;
    case EACCES:
    case EPERM:
      return WSAEACCES;
    case EFAULT:
      return WSAEFAULT;
    case EINVAL:
      return WSAEINVAL;
    case EMFILE:
    case ENFILE:
      return WSAEMFILE;
    case EAGAIN: // == EWOULDBLOCK
      return WSAEWOULDBLOCK;
    case EINPROGRESS: // a non-blocking connect: Winsock reports WSAEWOULDBLOCK
      return WSAEWOULDBLOCK;
    case EALREADY:
      return WSAEALREADY;
    case EDESTADDRREQ:
      return WSAEDESTADDRREQ;
    case EMSGSIZE:
      return WSAEMSGSIZE;
    case EPROTOTYPE:
      return WSAEPROTOTYPE;
    case ENOPROTOOPT:
      return WSAENOPROTOOPT;
    case EPROTONOSUPPORT:
      return WSAEPROTONOSUPPORT;
    case ESOCKTNOSUPPORT:
      return WSAESOCKTNOSUPPORT;
    case EOPNOTSUPP:
#if defined(ENOTSUP) && ENOTSUP != EOPNOTSUPP
    case ENOTSUP:
#endif
      return WSAEOPNOTSUPP;
    case EPFNOSUPPORT:
      return WSAEPFNOSUPPORT;
    case EAFNOSUPPORT:
      return WSAEAFNOSUPPORT;
    case EADDRINUSE:
      return WSAEADDRINUSE;
    case EADDRNOTAVAIL:
      return WSAEADDRNOTAVAIL;
    case ENETDOWN:
      return WSAENETDOWN;
    case ENETUNREACH:
      return WSAENETUNREACH;
    case ENETRESET:
      return WSAENETRESET;
    case ECONNABORTED:
      return WSAECONNABORTED;
    case ECONNRESET:
    case EPIPE: // sending on a connection the peer has closed
      return WSAECONNRESET;
    case ENOBUFS:
    case ENOMEM:
      return WSAENOBUFS;
    case EISCONN:
      return WSAEISCONN;
    case ENOTCONN:
      return WSAENOTCONN;
    case ESHUTDOWN:
      return WSAESHUTDOWN;
    case ETOOMANYREFS:
      return WSAETOOMANYREFS;
    case ETIMEDOUT:
      return WSAETIMEDOUT;
    case ECONNREFUSED:
      return WSAECONNREFUSED;
    case ELOOP:
      return WSAELOOP;
    case ENAMETOOLONG:
      return WSAENAMETOOLONG;
    case EHOSTDOWN:
      return WSAEHOSTDOWN;
    case EHOSTUNREACH:
      return WSAEHOSTUNREACH;
    case ENOTEMPTY:
      return WSAENOTEMPTY;
#if defined(EPROCLIM)
    case EPROCLIM:
      return WSAEPROCLIM;
#endif
    case EUSERS:
      return WSAEUSERS;
    case EDQUOT:
      return WSAEDQUOT;
    case ESTALE:
      return WSAESTALE;
    case EREMOTE:
      return WSAEREMOTE;
    default:
      return e;
    }
  }

  // Keeps errno across bookkeeping that follows a failed call, so WSAGetLastError() sees
  // the failure.
  class ErrnoGuard
  {
  public:
    ErrnoGuard() noexcept
      : mSaved(errno)
    {}
    ~ErrnoGuard() { errno = mSaved; }
    void Set(const int e) noexcept { mSaved = e; }

  private:
    int mSaved;
  };

  bool SetNonBlocking(const int fd, const bool on) noexcept
  {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) {
      return false;
    }
    const int wanted = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return wanted == flags || ::fcntl(fd, F_SETFL, wanted) == 0;
  }

  bool IsNonBlocking(const int fd) noexcept
  {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && (flags & O_NONBLOCK) != 0;
  }

  // Windows has no SIGPIPE: a write to a closed connection fails with an error there. Keep
  // a handler someone installed on purpose; only the default (terminate) is replaced.
  void IgnoreSigPipeOnce() noexcept
  {
    static std::once_flag sOnce;
    std::call_once(sOnce, [] {
      struct sigaction current{};
      if (::sigaction(SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL) {
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        ::sigaction(SIGPIPE, &ignore, nullptr);
      }
    });
  }

  constexpr long kSupportedEvents = FD_READ | FD_WRITE | FD_ACCEPT | FD_CONNECT | FD_CLOSE;

  // --- WSAEventSelect ----------------------------------------------------------------------
  struct Selection
  {
    // The selection's own handle to the event object (DuplicateHandle): Winsock holds a
    // kernel reference while a socket is selected, so the application may close its handle
    // first; and a stale handle value can never signal an unrelated, newer object.
    HANDLE event = nullptr;
    long mask = 0;
    std::uint64_t id = 0;     // tells a selection from a later one on a reused descriptor
    bool stream = false;      // SOCK_STREAM: FD_CONNECT/FD_CLOSE apply
    bool listening = false;   // FD_ACCEPT applies (and FD_READ/FD_WRITE/FD_CONNECT do not)
    bool readArmed = true;    // FD_READ / FD_ACCEPT / FD_CLOSE may be signalled
    bool writeArmed = true;   // FD_WRITE / FD_CONNECT may be signalled
    bool connectDone = false; // FD_CONNECT already recorded
    bool closeDone = false;   // FD_CLOSE already recorded
  };

  class EventSelector
  {
  public:
    static EventSelector& Get() noexcept
    {
      // Never destroyed: the poller thread may still be in poll() at process exit.
      static EventSelector* const sSelector = new EventSelector();
      return *sSelector;
    }

    int Select(const SOCKET s, const WSAEVENT event, const long networkEvents) noexcept
    {
      int type = 0;
      socklen_t typeLen = sizeof(type);
      if (::getsockopt(s, SOL_SOCKET, SO_TYPE, &type, &typeLen) != 0) {
        return SOCKET_ERROR; // errno: EBADF/ENOTSOCK -> WSAENOTSOCK
      }
      // Winsock makes the socket non-blocking whatever the event mask.
      if (!SetNonBlocking(s, true)) {
        return SOCKET_ERROR;
      }
      IgnoreSigPipeOnce();

      if (event == nullptr || (networkEvents & kSupportedEvents) == 0) {
        Forget(s);
        return 0;
      }

      HANDLE own = nullptr;
      if (!Duplicate(event, own)) {
        errno = EINVAL; // Winsock: WSAEINVAL for a bad event handle
        return SOCKET_ERROR;
      }
      return Install(s, type, own, networkEvents & kSupportedEvents);
    }

    // accept: the new socket gets the listening socket's selection, as on Windows.
    void Inherit(const SOCKET listener, const SOCKET accepted) noexcept
    {
      if (mCount.load(std::memory_order_relaxed) == 0) {
        return;
      }
      HANDLE own = nullptr;
      long networkEvents = 0;
      {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto it = mSelections.find(listener);
        if (it == mSelections.end() || !Duplicate(it->second.event, own)) {
          return;
        }
        networkEvents = it->second.mask;
      }
      int type = 0;
      socklen_t typeLen = sizeof(type);
      if (::getsockopt(accepted, SOL_SOCKET, SO_TYPE, &type, &typeLen) != 0 || !SetNonBlocking(accepted, true)) {
        ::CloseHandle(own);
        return;
      }
      (void)Install(accepted, type, own, networkEvents);
    }

  private:
    static bool Duplicate(const HANDLE event, HANDLE& own) noexcept
    {
      return ::DuplicateHandle(::GetCurrentProcess(), event, ::GetCurrentProcess(), &own, 0, FALSE,
                               DUPLICATE_SAME_ACCESS) != FALSE;
    }

    // Takes ownership of `own`.
    int Install(const SOCKET s, const int type, const HANDLE own, const long networkEvents) noexcept
    {
      Selection selection{};
      selection.event = own;
      selection.mask = networkEvents;
      selection.stream = type == SOCK_STREAM;
      if (selection.stream) {
        sockaddr_storage peer{};
        socklen_t peerLen = sizeof(peer);
        const bool connected = ::getpeername(s, reinterpret_cast<sockaddr*>(&peer), &peerLen) == 0;
        // FD_ACCEPT is for listening sockets. macOS has no getsockopt(SO_ACCEPTCONN), and
        // WSAEventSelect may come before listen(): a stream without a peer selected for
        // FD_ACCEPT is taken as one. (A socket accepted from a listener inherits FD_ACCEPT,
        // and has a peer.)
        selection.listening = !connected && (selection.mask & FD_ACCEPT) != 0;
        // A socket that is already connected has no FD_CONNECT to come.
        selection.connectDone = connected;
      }

      {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!StartLocked()) {
          ::CloseHandle(selection.event);
          errno = ENOBUFS;
          return SOCKET_ERROR;
        }
        selection.id = mNextId++;
        const auto [it, inserted] = mSelections.try_emplace(s, selection);
        if (!inserted) {
          ::CloseHandle(it->second.event);
          it->second = selection;
        }
        mCount.store(mSelections.size(), std::memory_order_relaxed);
      }
      Wake();
      return 0;
    }

  public:
    // closesocket, or a selection cancelled with a zero mask or null event.
    void Forget(const SOCKET s) noexcept
    {
      if (mCount.load(std::memory_order_relaxed) == 0) {
        return;
      }
      bool erased = false;
      {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto it = mSelections.find(s);
        if (it != mSelections.end()) {
          EraseLocked(it);
          erased = true;
        }
      }
      if (erased) {
        Wake();
      }
    }

    // recv, recvfrom and accept re-enable FD_READ / FD_ACCEPT (and the FD_CLOSE check).
    void ReenableRead(const SOCKET s) noexcept { Reenable(s, true); }
    // A send that would block re-enables FD_WRITE.
    void ReenableWrite(const SOCKET s) noexcept { Reenable(s, false); }

    bool IsSelected(const SOCKET s) noexcept
    {
      if (mCount.load(std::memory_order_relaxed) == 0) {
        return false;
      }
      std::lock_guard<std::mutex> lock(mMutex);
      return mSelections.find(s) != mSelections.end();
    }

  private:
    using SelectionMap = std::unordered_map<int, Selection>;

    EventSelector() = default;

    void EraseLocked(const SelectionMap::iterator it) noexcept
    {
      ::CloseHandle(it->second.event);
      mSelections.erase(it);
      mCount.store(mSelections.size(), std::memory_order_relaxed);
    }

    void Reenable(const SOCKET s, const bool read) noexcept
    {
      if (mCount.load(std::memory_order_relaxed) == 0) {
        return;
      }
      bool changed = false;
      {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto it = mSelections.find(s);
        if (it != mSelections.end()) {
          bool& armed = read ? it->second.readArmed : it->second.writeArmed;
          changed = !armed;
          armed = true;
        }
      }
      if (changed) {
        Wake();
      }
    }

    bool StartLocked() noexcept
    {
      if (mStarted) {
        return true;
      }
      int fds[2] = {-1, -1};
      if (::pipe(fds) != 0) {
        return false;
      }
      for (const int fd : fds) {
        SetNonBlocking(fd, true);
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
      }
      mWakeRead = fds[0];
      mWakeWrite = fds[1];

      pthread_t thread{};
      if (::pthread_create(&thread, nullptr, &EventSelector::ThreadMain, this) != 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        mWakeRead = -1;
        mWakeWrite = -1;
        return false;
      }
      ::pthread_detach(thread);
      mStarted = true;
      return true;
    }

    void Wake() noexcept
    {
      const ErrnoGuard keepErrno;
      const char byte = 0;
      // A full pipe already holds a wakeup.
      (void)::write(mWakeWrite, &byte, 1);
    }

    static void* ThreadMain(void* const self) noexcept
    {
#if defined(__APPLE__)
      ::pthread_setname_np("FAF WSAEventSelect");
#else
      ::pthread_setname_np(::pthread_self(), "FAF wsaevents");
#endif
      static_cast<EventSelector*>(self)->Run();
      return nullptr;
    }

    static short Interest(const Selection& sel) noexcept
    {
      short events = 0;
      if (sel.readArmed) {
        const bool wantsRead = sel.listening
          ? (sel.mask & FD_ACCEPT) != 0
          : (sel.mask & FD_READ) != 0 || (sel.stream && (sel.mask & FD_CLOSE) != 0 && !sel.closeDone);
        if (wantsRead) {
          events |= POLLIN;
        }
      }
      if (sel.writeArmed && !sel.listening) {
        if ((sel.mask & FD_WRITE) != 0 || (sel.stream && (sel.mask & FD_CONNECT) != 0 && !sel.connectDone)) {
          events |= POLLOUT;
        }
      }
      return events;
    }

    // The socket polled readable (or hung up / failed).
    static long ReadEvents(const int fd, Selection& sel) noexcept
    {
      if (sel.listening) {
        return sel.mask & FD_ACCEPT;
      }
      if (!sel.stream) {
        return sel.mask & FD_READ;
      }
      // A stream is readable for data, for the peer's close, or for an error: peek to tell.
      char probe = 0;
      ssize_t peeked = 0;
      do {
        peeked = ::recv(fd, static_cast<void*>(&probe), 1, MSG_PEEK | MSG_DONTWAIT);
      } while (peeked < 0 && errno == EINTR);
      if (peeked > 0 || (peeked < 0 && errno == EAGAIN)) {
        // Data (or a race with the application's own recv, which sorts itself out).
        return sel.mask & FD_READ;
      }
      // Orderly close (0) or reset/error (-1).
      if ((sel.mask & FD_CLOSE) != 0 && !sel.closeDone) {
        sel.closeDone = true;
        return FD_CLOSE;
      }
      return 0;
    }

    // The socket polled writable (or hung up / failed).
    static long WriteEvents(Selection& sel) noexcept
    {
      long fired = sel.mask & FD_WRITE;
      if (sel.stream && (sel.mask & FD_CONNECT) != 0 && !sel.connectDone) {
        sel.connectDone = true;
        fired |= FD_CONNECT;
      }
      return fired;
    }

    void Run() noexcept
    {
      std::vector<pollfd> polled;
      std::vector<std::uint64_t> ids;
      bool reportedPollFailure = false;
      for (;;) {
        polled.clear();
        ids.clear();
        polled.push_back(pollfd{mWakeRead, POLLIN, 0});
        ids.push_back(0);
        {
          std::lock_guard<std::mutex> lock(mMutex);
          for (const auto& [fd, sel] : mSelections) {
            const short events = Interest(sel);
            if (events != 0) {
              polled.push_back(pollfd{fd, events, 0});
              ids.push_back(sel.id);
            }
          }
        }

        const int ready = ::poll(polled.data(), static_cast<nfds_t>(polled.size()), -1);
        if (ready < 0) {
          if (errno != EINTR && !reportedPollFailure) {
            reportedPollFailure = true;
            std::fprintf(stderr, "WSAEventSelect poller: poll() failed: %s\n", std::strerror(errno));
          }
          if (errno != EINTR) {
            ::usleep(1000);
          }
          continue;
        }

        if (polled[0].revents != 0) {
          char drain[64];
          while (::read(mWakeRead, drain, sizeof(drain)) > 0) {
          }
        }

        std::lock_guard<std::mutex> lock(mMutex);
        for (std::size_t i = 1; i < polled.size(); ++i) {
          const short revents = polled[i].revents;
          if (revents == 0) {
            continue;
          }
          const auto it = mSelections.find(polled[i].fd);
          if (it == mSelections.end() || it->second.id != ids[i]) {
            continue; // deselected, or closed and the descriptor reused, while polling
          }
          Selection& sel = it->second;
          if ((revents & POLLNVAL) != 0) {
            // Closed without closesocket: nothing more will happen on it.
            EraseLocked(it);
            continue;
          }

          long fired = 0;
          const short asked = polled[i].events;
          if ((asked & POLLIN) != 0 && (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            fired |= ReadEvents(polled[i].fd, sel);
            sel.readArmed = false;
          }
          if ((asked & POLLOUT) != 0 && (revents & (POLLOUT | POLLHUP | POLLERR)) != 0) {
            fired |= WriteEvents(sel);
            sel.writeArmed = false;
          }
          if (fired != 0) {
            // Under the lock, so a selection that closesocket or WSAEventSelect has just
            // cancelled is never signalled afterwards.
            ::SetEvent(sel.event);
          }
        }
      }
    }

    std::mutex mMutex;
    SelectionMap mSelections;
    std::atomic<std::size_t> mCount{0}; // lets recv & co. skip the lock when nothing is selected
    std::uint64_t mNextId = 1;
    int mWakeRead = -1;
    int mWakeWrite = -1;
    bool mStarted = false;
  };

  bool IsSocket(const int fd) noexcept
  {
    struct stat st{};
    return ::fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode);
  }
} // namespace

// --- Startup ---------------------------------------------------------------------------------
int WSAStartup(const WORD versionRequested, const LPWSADATA data) noexcept
{
  if (data == nullptr) {
    return WSAEFAULT;
  }
  const BYTE major = LOBYTE(versionRequested);
  const BYTE minor = HIBYTE(versionRequested);
  if (major == 0) {
    return WSAVERNOTSUPPORTED;
  }
  // Winsock 2.2 answers a request for a later version with 2.2.
  const bool aboveHighest = major > 2 || (major == 2 && minor > 2);

  std::memset(data, 0, sizeof(*data));
  data->wVersion = aboveHighest ? MAKEWORD(2, 2) : versionRequested;
  data->wHighVersion = MAKEWORD(2, 2);
  std::snprintf(data->szDescription, sizeof(data->szDescription), "WinSock 2.0");
  std::snprintf(data->szSystemStatus, sizeof(data->szSystemStatus), "Running");

  IgnoreSigPipeOnce();
  return 0;
}

int WSACleanup() noexcept
{
  return 0;
}

int WSAGetLastError() noexcept
{
  return WsaErrorFromErrno(errno);
}

// --- Socket calls ------------------------------------------------------------------------------
int closesocket(const SOCKET s) noexcept
{
  // Winsock fails on a handle that is not a socket instead of closing whatever it is.
  if (s < 0 || !IsSocket(s)) {
    errno = ENOTSOCK;
    return SOCKET_ERROR;
  }
  EventSelector::Get().Forget(s);
  if (::close(s) != 0 && errno != EINTR) { // the descriptor is released even on EINTR
    return SOCKET_ERROR;
  }
  return 0;
}

int ioctlsocket(const SOCKET s, const long cmd, u_long* const argp) noexcept
{
  if (argp == nullptr) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  const unsigned long request = static_cast<unsigned long>(cmd);
  if (request == static_cast<unsigned long>(FIONBIO)) {
    if (*argp == 0 && EventSelector::Get().IsSelected(s)) {
      errno = EINVAL; // Winsock: no blocking mode while WSAEventSelect is active
      return SOCKET_ERROR;
    }
    if (!SetNonBlocking(s, *argp != 0)) {
      return SOCKET_ERROR;
    }
    IgnoreSigPipeOnce();
    return 0;
  }
  if (request == static_cast<unsigned long>(FIONREAD)) {
    int readable = 0;
    if (::ioctl(s, FIONREAD, &readable) != 0) {
      return SOCKET_ERROR;
    }
    *argp = static_cast<u_long>(readable);
    return 0;
  }
  errno = EINVAL;
  return SOCKET_ERROR;
}

int recv(const SOCKET s, char* const buf, const int len, const int flags) noexcept
{
  if (len < 0) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  ssize_t received = 0;
  do {
    received = ::recv(s, static_cast<void*>(buf), static_cast<std::size_t>(len), flags);
  } while (received < 0 && errno == EINTR);
  {
    const ErrnoGuard keepErrno;
    EventSelector::Get().ReenableRead(s);
  }
  return received < 0 ? SOCKET_ERROR : static_cast<int>(received);
}

int recvfrom(const SOCKET s, char* const buf, const int len, const int flags, sockaddr* const from,
             int* const fromlen) noexcept
{
  if (len < 0 || (from != nullptr && (fromlen == nullptr || *fromlen < 0))) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  iovec iov{static_cast<void*>(buf), static_cast<std::size_t>(len)};
  msghdr message{};
  message.msg_name = from;
  message.msg_namelen = from != nullptr ? static_cast<socklen_t>(*fromlen) : 0;
  message.msg_iov = &iov;
  message.msg_iovlen = 1;

  ssize_t received = 0;
  do {
    received = ::recvmsg(s, &message, flags);
  } while (received < 0 && errno == EINTR);

  ErrnoGuard keepErrno;
  if (received >= 0) {
    if (from != nullptr) {
      *fromlen = static_cast<int>(message.msg_namelen);
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
      // Winsock: the datagram did not fit -- the buffer holds its start, the rest is lost.
      keepErrno.Set(EMSGSIZE);
      received = -1;
    }
  }
  EventSelector::Get().ReenableRead(s);
  return received < 0 ? SOCKET_ERROR : static_cast<int>(received);
}

int send(const SOCKET s, const char* const buf, const int len, const int flags) noexcept
{
  if (len < 0) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  // A blocking Winsock send returns once everything is buffered; a POSIX one can stop short
  // when a signal arrives, so carry on with the rest.
  std::size_t total = 0;
  const std::size_t wanted = static_cast<std::size_t>(len);
  for (;;) {
    const ssize_t sent = ::send(s, static_cast<const void*>(buf + total), wanted - total, flags);
    if (sent < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (total > 0) {
        break;
      }
      if (errno == EAGAIN) {
        const ErrnoGuard keepErrno;
        EventSelector::Get().ReenableWrite(s);
      }
      return SOCKET_ERROR;
    }
    total += static_cast<std::size_t>(sent);
    if (total >= wanted || IsNonBlocking(s)) {
      break;
    }
  }
  return static_cast<int>(total);
}

int sendto(const SOCKET s, const char* const buf, const int len, const int flags, const sockaddr* const to,
           const int tolen) noexcept
{
  if (len < 0 || tolen < 0) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  ssize_t sent = 0;
  do {
    sent = ::sendto(s, static_cast<const void*>(buf), static_cast<std::size_t>(len), flags, to,
                    static_cast<socklen_t>(tolen));
  } while (sent < 0 && errno == EINTR);
  if (sent < 0) {
    if (errno == EAGAIN || errno == ENOBUFS) {
      const ErrnoGuard keepErrno;
      EventSelector::Get().ReenableWrite(s);
    }
    return SOCKET_ERROR;
  }
  return static_cast<int>(sent);
}

SOCKET accept(const SOCKET s, sockaddr* const addr, int* const addrlen) noexcept
{
  if (addr != nullptr && (addrlen == nullptr || *addrlen < 0)) {
    errno = EFAULT;
    return INVALID_SOCKET;
  }
  socklen_t length = addr != nullptr ? static_cast<socklen_t>(*addrlen) : 0;
  int accepted = -1;
  do {
    accepted = ::accept(s, addr, addr != nullptr ? &length : nullptr);
  } while (accepted < 0 && errno == EINTR);

  const ErrnoGuard keepErrno;
  EventSelector& selector = EventSelector::Get();
  selector.ReenableRead(s);
  if (accepted < 0) {
    return INVALID_SOCKET;
  }
  if (addr != nullptr) {
    *addrlen = static_cast<int>(length);
  }
  // Winsock: the new socket has the listening socket's properties.
  if (IsNonBlocking(s)) {
    SetNonBlocking(accepted, true);
  }
  selector.Inherit(s, accepted);
  return accepted;
}

int getsockname(const SOCKET s, sockaddr* const name, int* const namelen) noexcept
{
  if (name == nullptr || namelen == nullptr || *namelen < 0) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  socklen_t length = static_cast<socklen_t>(*namelen);
  if (::getsockname(s, name, &length) != 0) {
    return SOCKET_ERROR;
  }
  *namelen = static_cast<int>(std::min<socklen_t>(length, INT_MAX));
  return 0;
}

int getpeername(const SOCKET s, sockaddr* const name, int* const namelen) noexcept
{
  if (name == nullptr || namelen == nullptr || *namelen < 0) {
    errno = EFAULT;
    return SOCKET_ERROR;
  }
  socklen_t length = static_cast<socklen_t>(*namelen);
  if (::getpeername(s, name, &length) != 0) {
    return SOCKET_ERROR;
  }
  *namelen = static_cast<int>(std::min<socklen_t>(length, INT_MAX));
  return 0;
}

// --- Event objects and WSAEventSelect ------------------------------------------------------
WSAEVENT WSACreateEvent() noexcept
{
  // Winsock's events are manual-reset and start non-signalled.
  const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  return event != nullptr ? event : WSA_INVALID_EVENT;
}

BOOL WSACloseEvent(const WSAEVENT event) noexcept
{
  // A socket still selected with the event keeps its own reference, as on Windows.
  return ::CloseHandle(event);
}

BOOL WSASetEvent(const WSAEVENT event) noexcept
{
  return ::SetEvent(event);
}

BOOL WSAResetEvent(const WSAEVENT event) noexcept
{
  return ::ResetEvent(event);
}

DWORD WSAWaitForMultipleEvents(const DWORD count, const WSAEVENT* const events, const BOOL waitAll,
                               const DWORD timeoutMs, const BOOL alertable) noexcept
{
  // WSA_WAIT_* have the values of WAIT_*; an alertable wait runs queued APCs.
  return ::WaitForMultipleObjectsEx(count, events, waitAll, timeoutMs, alertable);
}

int WSAEventSelect(const SOCKET s, const WSAEVENT event, const long networkEvents) noexcept
{
  return EventSelector::Get().Select(s, event, networkEvents);
}
