-- Bring-up frame captures (macos/porting/bringup.md): the replay benchmark's init (FAF's
-- init.lua + the benchhook that holds /benchspeed) plus the shotshook mount, whose
-- gamemain.lua hook moves the world camera at set game ticks and dumps the frame with the
-- engine's own dump_Frame command (/shots <dir>). Pass /init <this file> to macos/run_game.sh
-- (INIT), which writes build/run_folder.lua: it sets InitFileDir to the run folder.
local here = InitFileDir
dofile(here .. '/../../../../build/run_folder.lua')
dofile(InitFileDir .. '/init_bench.lua')
table.insert(path, { dir = here .. '/mount', mountpoint = '/' })
table.insert(hook, '/shotshook')
