
-- macOS native build: the engine appends this to /lua/maui/layouthelpers.lua each time
-- the module loads (the /fafengine-macos hook folder, StartupHelpers.cpp
-- AppendMacosHookFolder). On a scaled Mac desktop the game draws in backing-store pixels,
-- which macOS shrinks to the panel's (6016 to 3840 for a 4K panel that "looks like"
-- 3008x1692), so the player's UI Scale is multiplied by FafUIScaleCorrection() - backing
-- pixels per panel pixel, 1 when they are the same - and 100% is the UI at its native size
-- on the real panel (src/platform/posix/shell/Panel.cpp).
do
    local correction = rawget(_G, "FafUIScaleCorrection")
    local factor = correction and correction() or 1
    if factor > 1.001 then
        SetPixelScaleFactor(GetPixelScaleFactor() * factor)
        LOG(string.format("macOS: UI scale %.4g (UI Scale x %.4g for the display panel)", GetPixelScaleFactor(), factor))
    end
end
