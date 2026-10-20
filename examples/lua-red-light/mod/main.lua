-- Red Light, Green Light: a Lua server mod for the port's multiplayer.
--
-- It runs on the host only (side = server); players join with the plain game.
-- On a green light everyone runs up Bianco Hills' path for the goal; on a red
-- light everyone must stand still, and the server sends anyone it sees moving
-- back to where they started. The first to the goal wins, everyone watches
-- them for a moment, and a new round starts.
--
-- It is all the server's word (sms.net.send): the banners, the sounds, who is
-- caught and where they go. Where each player is comes from sms.net.player,
-- as their game last said. The manual's "Server mods" section explains it.

local AREA, EPISODE = 2, 0                         -- Bianco Hills, episode 1
local GOAL = { x = -6018, y = 3748, z = 14511 }    -- up the path from the start
local GOAL_RADIUS = 400
local GRACE = 0.7       -- seconds after "RED LIGHT" before moving counts (reaction, and the network)
local STILL = 80        -- units a player may drift on red

local GREEN, RED = sms.rgb(90, 230, 110), sms.rgb(255, 80, 70)

sms.option{ key = "min_players", label = "Players a round needs", type = "int", default = 1, min = 1, max = 8 }

local phase = "waiting"   -- waiting, green, red, winner
local ends = 0            -- sms.time() when the phase is over
local starts = {}         -- slot -> where they started (their arrival)
local frozen = {}         -- slot -> where they stood when red began to count
local red_since = 0       -- when the red light came on
local caught_at = {}      -- slot -> when they were last sent back (their update takes a moment to show it)
local set_up = false

local function send(to, cmd) sms.net.send(to, cmd) end

local function players_in_stage()
  local list = {}
  for _, slot in ipairs(sms.net.players()) do
    local p = sms.net.player(slot)
    if p and not p.server and p.in_stage then list[#list + 1] = p end
  end
  return list
end

local function distance(a, b)
  local dx, dz = a.x - b.x, a.z - b.z
  return math.sqrt(dx * dx + dz * dz)
end

local function light(colour, text, sound)
  send("all", { type = "banner", color = colour, text = text })
  send("all", { type = "sound", id = sound })
end

local function green()
  phase, ends = "green", sms.time() + 2 + math.random() * 3
  light(GREEN, "GREEN LIGHT", sms.SOUND_DECIDE)
end

local function red()
  phase, ends = "red", sms.time() + 2 + math.random() * 2
  red_since, frozen = sms.time(), {}
  light(RED, "RED LIGHT", sms.SOUND_CANCEL)
end

local function new_round()
  phase, ends = "waiting", sms.time() + 3
  send("all", { type = "banner", text = "" })
  send("all", { type = "watch", slot = -1 })
  send("all", { type = "status", sticky = true,
                text = "Red Light, Green Light: run for the goal on green, freeze on red" })
end

-- caught moving on red: back to where they started
local function caught(p)
  caught_at[p.slot] = sms.time()
  send(p.slot, { type = "warp", position = starts[p.slot], yaw = 0 })
  send(p.slot, { type = "notice", text = "Caught moving! Back to the start" })
  send(p.slot, { type = "sound", id = sms.SOUND_DAMAGE })
  sms.log(("%s caught moving"):format(p.name))
end

local function won(p)
  phase, ends = "winner", sms.time() + 6
  send("all", { type = "banner", color = p.color, text = p.name .. " WINS!" })
  send("all", { type = "music", id = sms.MUSIC_FANFARE_RACE })
  for _, q in ipairs(players_in_stage()) do
    if q.slot ~= p.slot then send(q.slot, { type = "watch", slot = p.slot }) end
  end
  sms.net.set_status("Red Light, Green Light: " .. p.name .. " won")
  sms.log(p.name .. " reached the goal")
end

sms.on("frame", function()
  if sms.net.role() ~= "host" then return end
  if not set_up then
    set_up = true
    sms.net.set_mode("red-light")
    sms.net.load_stage(AREA, EPISODE)
  end
  local players = players_in_stage()
  local now = sms.time()
  -- where each player starts: where they are first seen in play here
  for _, p in ipairs(players) do
    if not starts[p.slot] and p.visible then starts[p.slot] = p.position end
  end
  if phase == "waiting" then
    if #players >= sms.setting("min_players", 1) and now >= ends then green() end
  elseif phase == "green" or phase == "red" then
    for _, p in ipairs(players) do
      if distance(p.position, GOAL) < GOAL_RADIUS then won(p) return end
    end
    if phase == "red" and now > red_since + GRACE then
      for _, p in ipairs(players) do
        local was = frozen[p.slot]
        if caught_at[p.slot] and now - caught_at[p.slot] < 1.5 then
          frozen[p.slot] = nil -- just sent back: where they stand next counts
        elseif not was then
          frozen[p.slot] = p.position
        elseif distance(p.position, was) > STILL and starts[p.slot] then
          caught(p)
          frozen[p.slot] = starts[p.slot]
        end
      end
    end
    if now >= ends then
      if phase == "green" then red() else green() end
    end
  elseif phase == "winner" and now >= ends then
    sms.net.load_stage(AREA, EPISODE) -- the stage afresh: a new round
    phase, ends = "restart", now + 15
  elseif phase == "restart" and now >= ends then
    sms.net.load_stage(AREA, EPISODE)
    ends = now + 15
  end
end)

-- a new session of the stage: everyone stays in it, and a round begins
sms.on("net_session", function(ev)
  if ev.area ~= AREA then sms.net.load_stage(AREA, EPISODE) return end
  starts = {}
  send("all", { type = "stay", on = true, sticky = true })
  new_round()
end)

-- a player in the stage (just joined, or back after a lost life): their start
-- is wherever they are first seen in play (the frame handler)
sms.on("net_player_arrived", function(ev)
  starts[ev.slot], frozen[ev.slot] = nil, nil
  send(ev.slot, { type = "notice", text = "Red Light, Green Light! Run up the path on green, freeze on red" })
end)
