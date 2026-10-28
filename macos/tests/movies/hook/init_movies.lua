-- Movie test hook (package movies; notes: macos/porting/movies.md): FAF's init plus the
-- movieshook mount, whose /lua/ui/menus/main.lua hook plays test movies over the main menu
-- through the engine's own movie API (the Lua Movie control: CMauiMovie, CMovie, the SFD
-- player); the list is fixed in the hook. Pass /init <this file> through the
-- ~/Gaming/faf-re link (no space in the path); the run folder stays the working directory.
local here = InitFileDir
InitFileDir = '/Users/scottharris/Gaming/faf-re-bin/run-native'
dofile(InitFileDir .. '/init_faf.lua')
table.insert(path, { dir = here .. '/mount', mountpoint = '/' })
-- FAF's own movies (portraits, campaign briefings, coop), which FAF's init leaves to the
-- featured mods that use them.
table.insert(path, { dir = '/Users/scottharris/.faforever/movies', mountpoint = '/movies' })
table.insert(hook, '/movieshook')
