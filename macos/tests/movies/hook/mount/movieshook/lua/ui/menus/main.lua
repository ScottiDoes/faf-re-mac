
-- movieshook (macos/tests/movies): appended to /lua/ui/menus/main.lua. Two seconds after the
-- main menu is up, plays these movies one after another in a Movie control over the whole
-- window, logging "MOVIETEST" lines: e3_demo_cut.sfd (the one movie with embedded audio,
-- which plays through DirectSound), A01_B01.sfd (a campaign briefing: special composition,
-- which the player refuses, as faf-re does on Windows), a FAF coop movie without Sofdec's
-- header (refused too), a FAF portrait, Abasi.sfd (an FA portrait) and UEF_load.sfd (the
-- UEF loading-screen movie). init_movies.lua mounts ~/.faforever/movies (read only).
local MovieTestCreateUI = CreateUI
function CreateUI(...)
    local result = MovieTestCreateUI(unpack(arg))
    ForkThread(function()
        WaitSeconds(2)
        local Movie = import("/lua/maui/movie.lua").Movie
        local LayoutHelpers = import("/lua/maui/layouthelpers.lua")
        local parent = GetFrame(0)
        -- The menu's ambient sound (XACT) is stopped while e3_demo_cut plays, so that what
        -- the capture holds then is the movie's own audio (DirectSound, not XACT).
        local list = {
            { '/movies/e3_demo_cut.sfd', mute = true },
            { '/movies/A01_B01.sfd' },
            { '/movies/FAF_Coop_Operation_Tight_Spot_M2Choice.sfd' },
            { '/movies/A01_Rhiza_M01_00747.sfd' },
            { '/movies/Abasi.sfd' },
            { '/movies/UEF_load.sfd' },
        }
        local movie = Movie(parent)
        LayoutHelpers.FillParentPreserveAspectRatio(movie, parent)
        movie.Depth:Set(function() return parent.Depth() + 1000 end)
        local index = 0
        local busy = false
        local function nextMovie()
            index = index + 1
            local entry = list[index]
            if not entry then
                LOG('MOVIETEST done')
                movie:Destroy()
                return
            end
            busy = true
            -- The menu's ambient loop (XACT) stops while a "mute" movie plays.
            if entry.mute and StopAmbient then
                StopAmbient()
            elseif StartAmbient then
                StartAmbient()
            end
            LOG('MOVIETEST set ' .. entry[1] .. (entry.mute and ' (menu ambient stopped)' or ''))
            movie:Set(entry[1], entry[2] and Sound(entry[2]))
        end
        movie.OnLoaded = function(self)
            LOG('MOVIETEST loaded ' .. list[index][1] .. ' frames=' .. tostring(self:GetNumFrames())
                .. ' fps=' .. tostring(self:GetFrameRate()))
            self:Play()
        end
        movie.OnFinished = function(self)
            LOG('MOVIETEST finished ' .. list[index][1])
            busy = false
            self:Stop()
            ForkThread(nextMovie)
        end
        movie.OnStopped = function(self)
            if busy then
                LOG('MOVIETEST stopped ' .. list[index][1] .. ' (not playable)')
                busy = false
                ForkThread(nextMovie)
            end
        end
        nextMovie()
    end)
    return result
end
