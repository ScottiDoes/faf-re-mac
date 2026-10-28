-- Bring-up capture hook (only init_shots.lua mounts it). In a replay started with
-- /shots <dir>, it snaps the world camera to a few places at set game ticks and dumps each
-- frame with dump_Frame <dir> (the engine's frame dump: render target readback + BMP).
-- Every step logs "SHOT <name> tick=<tick> ...".
local ShotsOriginalCreateUI = CreateUI

function CreateUI(isReplay)
    ShotsOriginalCreateUI(isReplay)
    local arg = GetCommandLineArg("/shots", 1)
    if not isReplay or not arg then
        return
    end
    local dir = arg[1]
    ForkThread(function()
        local cam = GetCamera('WorldCamera')
        local scenario = SessionGetScenarioInfo()
        local w, h = scenario.size[1], scenario.size[2]
        local s0 = cam:SaveSettings()
        LOG(string.format("SHOT setup map=%dx%d focus=(%.1f,%.1f,%.1f) zoom=%.1f pitch=%.3f heading=%.3f",
            w, h, s0.Focus[1], s0.Focus[2], s0.Focus[3], s0.Zoom, s0.Pitch, s0.Heading))
        local starts = {}
        local ok, result = pcall(function()
            return import('/lua/ui/maputil.lua').GetStartPositions(scenario)
        end)
        if ok and result then
            for name, pos in result do
                LOG(string.format("SHOT start %s (%.1f, %.1f)", name, pos[1], pos[2]))
                table.insert(starts, {name = name, x = pos[1], z = pos[2]})
            end
            table.sort(starts, function(a, b) return a.name < b.name end)
        end
        local function at(i, dx, dz)
            local p = starts[i]
            if p then return p.x + (dx or 0), p.z + (dz or 0) end
            return w / 2, h / 2
        end
        local plan = {
            { name = "acu",     tick = 100,  pos = { at(1) },          zoom = 35,  pitch = 0.9 },
            { name = "base1",   tick = 900,  pos = { at(1, 0, 10) },   zoom = 70,  pitch = 1.0 },
            { name = "base2",   tick = 2400, pos = { at(2, 0, -10) },  zoom = 90,  pitch = 1.0 },
            { name = "mid",     tick = 4800, pos = { w / 2, h * 0.45 }, zoom = 110, pitch = 1.1 },
            { name = "nowater", tick = 4800, pos = { w / 2, h * 0.45 }, zoom = 110, pitch = 1.1,
              before = 'ren_Water false', after = 'ren_Water true' },
            { name = "mid2",    tick = 5600, pos = { w / 2, h * 0.40 }, zoom = 70,  pitch = 0.8 },
            { name = "water",   tick = 5700, pos = { w * 0.30, h * 0.25 }, zoom = 60,  pitch = 0.5 },
            { name = "low",     tick = 6400, pos = { at(1, 0, 25) },   zoom = 60,  pitch = 0.45 },
            { name = "fog",     tick = 7000, pos = { w / 2, h / 2 },   zoom = s0.Zoom, pitch = s0.Pitch,
              before = function() SetFocusArmy(1) end, after = function() SetFocusArmy(-1) end },
            { name = "fogclose", tick = 7100, pos = { at(1, 0, 40) },  zoom = 120, pitch = 1.0,
              before = function() SetFocusArmy(1) end, after = function() SetFocusArmy(-1) end },
            { name = "end",     tick = 9000, pos = { w / 2, h / 2 },   zoom = s0.Zoom, pitch = s0.Pitch },
        }
        local function run(action)
            if type(action) == 'string' then
                ConExecute(action)
            elseif action then
                action()
            end
        end
        for _, step in plan do
            while GameTick() < step.tick do
                WaitSeconds(0.05)
            end
            run(step.before)
            cam:SnapTo(Vector(step.pos[1], s0.Focus[2], step.pos[2]), {s0.Heading, step.pitch, 0}, step.zoom)
            WaitSeconds(0.3)
            local s = cam:SaveSettings()
            LOG(string.format("SHOT %s tick=%d focus=(%.1f,%.1f,%.1f) zoom=%.1f pitch=%.3f", step.name, GameTick(),
                s.Focus[1], s.Focus[2], s.Focus[3], s.Zoom, s.Pitch))
            ConExecute('dump_Frame ' .. dir)
            WaitSeconds(0.3)
            run(step.after)
        end
        LOG("SHOT done")
    end)
end
