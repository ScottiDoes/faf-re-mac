-- macOS native build: the engine appends this to /lua/user/prefs.lua each time the module loads
-- (the /fafengine-macos hook folder, StartupHelpers.cpp AppendMacosHookFolder). FAF reads the UI
-- Scale from the current profile alone (layouthelpers, effecthelpers and hotstats: `ui_scale or
-- 1`), but CreateProfile copies only the display adapters into a new profile. A UI Scale chosen in
-- FAF Native's setup window before the first game is the option's default until then (CScApp.cpp,
-- ApplySetupWindowSettings), so the lobby and the Options screen used it while the game, laid out
-- after FAF's lobby had made the profile, fell back to 100%. A new profile now takes the option's
-- UI Scale as well.
do
    local FafCreateProfile = CreateProfile
    function CreateProfile(name)
        local created = FafCreateProfile(name)
        if created then
            local options = GetFromCurrentProfile('options') or {}
            if options.ui_scale == nil then
                options.ui_scale = GetOption('ui_scale')
                SetToCurrentProfile('options', options)
                SavePreferences()
            end
        end
        return created
    end
end
