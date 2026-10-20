// My C++ mod: a template for a native mod for the Super Mario Sunshine PC port.
//
// It shows Mario's speed in a panel at the bottom right while a stage is
// played. P (key.toggle in its settings) hides it, and so does a switch in
// the Mods menu (F1). Make it your own: every event and function is in the
// manual, https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md
#include <sms_mod.hpp>

#include <math.h>
#include <stdio.h>

struct MyMod : sms::Mod {
	using sms::Mod::Mod;

	bool shown = true;
	int toggle = -1;

	int load() override
	{
		toggle = bindKey("toggle", "P");
		optionBool("show", "Show the speed", true);

		// a key of ours: consumed, so the game's controls never see it
		on<SmsEvKey>(SMS_EV_KEY, [this](SmsEvKey& k) {
			if (k.scancode != toggle)
				return SMS_CONTINUE;
			if (k.down && !k.repeat)
				shown = !shown;
			return SMS_CANCEL;
		});

		on<SmsEvDraw>(SMS_EV_DRAW, [this](SmsEvDraw& d) {
			const SmsGameState st = state();
			if (d.mode != SMS_MODE_STAGE || st.paused || !shown || !setting("show", 1))
				return;
			const SmsPlayer m = mario();
			if (!m.valid)
				return;
			// velocity is in units a tick; a tick is 1/ticks_per_second seconds
			const float speed = sqrtf(m.velocity.x * m.velocity.x + m.velocity.z * m.velocity.z) * st.ticks_per_second;
			char line[64];
			snprintf(line, sizeof line, "Speed %.0f", speed);
			// a place on screen that no other mod's panel (or the game's HUD) has
			SmsRect r;
			if (!draw().hud_slot(handle(), SMS_ANCHOR_BOTTOM_RIGHT, 110, 24, &r))
				return;
			draw().rect(r.x, r.y, r.w, r.h, SMS_RGBA(0, 0, 0, 160));
			draw().text(r.x + 10, r.y + 6, 14, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, line);
		});

		info("loaded; %s toggles it", input().key_name(toggle));
		return 0;
	}
};

SMS_MOD_CLASS(MyMod)
