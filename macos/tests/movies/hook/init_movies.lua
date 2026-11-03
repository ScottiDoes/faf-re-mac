-- Movie test hook (package movies; notes: macos/porting/movies.md): FAF's init plus the
-- movieshook mount, whose /lua/ui/menus/main.lua hook plays test movies over the main menu
-- through the engine's own movie API (the Lua Movie control: CMauiMovie, CMovie, the SFD
-- player); the list is fixed in the hook. Pass /init <this file> to macos/run_game.sh or
-- macos/tests/movies/run_av_game.sh (INIT), which write build/run_folder.lua: it sets
-- InitFileDir to the run folder and FafClientDir to the FAF client's folder.
local here = InitFileDir
dofile(here .. '/../../../../build/run_folder.lua')
dofile(InitFileDir .. '/init_faf.lua')
table.insert(path, { dir = here .. '/mount', mountpoint = '/' })
-- FAF's own movies (portraits, campaign briefings, coop), which FAF's init leaves to the
-- featured mods that use them.
table.insert(path, { dir = FafClientDir .. '/movies', mountpoint = '/movies' })
table.insert(hook, '/movieshook')
