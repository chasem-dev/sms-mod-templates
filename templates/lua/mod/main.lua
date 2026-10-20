-- My Lua mod: a template for a Lua script mod for the Super Mario Sunshine PC port.
--
-- It shows Mario's speed in a panel at the bottom right while a stage is
-- played. U (key.toggle in its settings) hides it, and so does a switch in
-- the Mods menu (F1). Make it your own: the Lua API is in the manual,
-- https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md#lua-scripts
--
-- Script mods run in the lua mod, which players install once (it is not part
-- of the game): https://github.com/chasem-dev/sms-mod-sdk/releases

sms.option{ key = "show", label = "Show the speed", type = "bool", default = true }

local toggle_key = sms.bind_key("toggle", "U")
local shown = true

-- a key of ours: returning true consumes it, so the game's controls never see it
sms.on("key", function(ev)
  if ev.scancode ~= toggle_key then return end
  if ev.down and not ev.repeated then shown = not shown end
  return true
end)

sms.on("draw", function(ev)
  local st = sms.state()
  if ev.mode ~= "stage" or st.paused or not shown or not sms.setting("show", true) then return end
  local mario = sms.mario()
  if not mario then return end
  -- velocity is in units a tick; a tick is 1/ticks_per_second seconds
  local v = mario.velocity
  local speed = math.sqrt(v.x * v.x + v.z * v.z) * st.ticks_per_second
  -- a place on screen that no other mod's panel (or the game's HUD) has
  local x, y, w, h = sms.draw.hud_slot("bottom_right", 110, 24)
  if not x then return end
  sms.draw.rect(x, y, w, h, sms.rgba(0, 0, 0, 160))
  sms.draw.text(x + 10, y + 6, 14, sms.rgb(255, 255, 255), ("Speed %.0f"):format(speed), sms.SHADOW)
end)

sms.log(("loaded; %s toggles it"):format(sms.key_name(toggle_key)))
