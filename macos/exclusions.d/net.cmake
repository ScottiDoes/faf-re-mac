# Package "net" (src/moho/net, compat area Sockets): every net TU builds against
# platform/posix/compat/Sockets.h (Winsock over BSD sockets); see macos/porting/net.md.

list(APPEND FAF_MACOS_REPLACED_SOURCES
  # In-engine peer links and native mode, on src/netlink (libjuice, libdatachannel), which this
  # build doesn't link yet. Replaced by src/platform/posix/stubs/net/NetlinkPeersPosix.cpp: both
  # stay off (they are off by default) and the FAF client's ICE adapter links the game.
  "^moho/net/NetlinkPeers\\.cpp$=>platform/posix/stubs/net/NetlinkPeersPosix.cpp"
)
