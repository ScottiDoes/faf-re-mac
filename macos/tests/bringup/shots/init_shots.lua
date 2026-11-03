-- Bring-up frame captures (macos/porting/bringup.md): the replay benchmark's init (FAF's
-- init.lua + the benchhook that holds /benchspeed) plus the shotshook mount, whose
-- gamemain.lua hook moves the world camera at set game ticks and dumps the frame with the
-- engine's own dump_Frame command (/shots <dir>). Pass /init <this file>; the run folder stays
-- the working directory.
local here = InitFileDir
InitFileDir = '/Users/scottharris/Gaming/.faf/faf-re-bin/run-native'
dofile(InitFileDir .. '/init_bench.lua')
table.insert(path, { dir = here .. '/mount', mountpoint = '/' })
table.insert(hook, '/shotshook')
