// POSIX stand-in for moho/net/NetlinkPeers.cpp: the macOS build doesn't link src/netlink (libjuice,
// libdatachannel) yet, so in-engine links (net_InEngineLinks) and native mode (net_NativeNet,
// /nativenet) stay off and the game talks to the FAF client's ICE adapter, as the shipped game does.
#include "moho/net/NetlinkPeers.h"

#include "gpg/core/utils/Logging.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/NetConVars.h"

namespace moho
{
  void NETLINK_SayHello(CGpgNetInterface&)
  {
    if (net_InEngineLinks != 0) {
      gpg::Warnf("net_InEngineLinks: in-engine links are not in the macOS build; the ICE adapter links the game");
    }
  }

  bool NETLINK_HandleCommand(const msvc8::string&, const msvc8::vector<SNetCommandArg>&)
  {
    return false;
  }

  void NETLINK_Shutdown() {}

  bool NATIVENET_Enabled()
  {
    return false;
  }

  void NATIVENET_Start()
  {
    if (net_NativeNet != 0 || CFG_GetArgOption("/nativenet", 0u, nullptr)) {
      gpg::Warnf("net_NativeNet: native mode is not in the macOS build; the ICE adapter links the game");
    }
  }

  bool NATIVENET_HandleCommand(const msvc8::string&, msvc8::vector<SNetCommandArg>&)
  {
    return false;
  }

  void NATIVENET_OnGameMessage(const char*) {}
} // namespace moho
