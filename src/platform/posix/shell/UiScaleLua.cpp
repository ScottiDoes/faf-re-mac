// FafUIScaleCorrection() for FAF's UI Lua (Shell.h, UiScaleCorrection): the factor the
// macOS hook in macos/fafengine-macos multiplies the player's UI Scale by whenever FAF's
// layout module loads (FA rebuilds the UI's Lua state for the front end and each game), so
// 100% is the UI at its native size on the display panel however the desktop is scaled.

#include "platform/posix/shell/Shell.h"

#include "lua/LuaObject.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/lua/CScrLuaInitForm.h"

namespace
{
  [[nodiscard]] moho::CScrLuaInitFormSet& UserLuaInitSet()
  {
    if (moho::CScrLuaInitFormSet* const set = moho::SCR_FindLuaInitFormSet("User"); set != nullptr) {
      return *set;
    }

    static moho::CScrLuaInitFormSet fallbackSet("User");
    return fallbackSet;
  }

  int cfunc_FafUIScaleCorrection(lua_State* const L)
  {
    lua_pushnumber(L, static_cast<lua_Number>(faf_posix::shell::UiScaleCorrection()));
    return 1;
  }

  moho::CScrLuaInitForm* func_FafUIScaleCorrection_LuaFuncDef()
  {
    static moho::CScrLuaBinder binder(
      UserLuaInitSet(),
      "FafUIScaleCorrection",
      &cfunc_FafUIScaleCorrection,
      nullptr,
      "<global>",
      "FafUIScaleCorrection() -> backing-store pixels per panel pixel on the game's display (macOS; at least 1)"
    );
    return &binder;
  }

  // Names the binder definition so it runs at static initialization (see MapImager.cpp's
  // bootstrap for why a source line has to).
  struct UiScaleLuaFuncDefBootstrap
  {
    UiScaleLuaFuncDefBootstrap()
    {
      (void)func_FafUIScaleCorrection_LuaFuncDef();
    }
  };

  const UiScaleLuaFuncDefBootstrap gUiScaleLuaFuncDefBootstrap{};
} // namespace
