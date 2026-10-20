/* My C mod: a template for a native mod for the Super Mario Sunshine PC port.
 *
 * It shows Mario's speed in a panel at the bottom right while a stage is
 * played. O (key.toggle in its settings) hides it, and so does a switch in
 * the Mods menu (F1). Make it your own: every event and function is in the
 * manual, https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md
 */
#include <sms_mod.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const SmsApi* api;
static int toggle_key = -1;
static int shown      = 1;

/* a key of ours: consumed, so the game's controls never see it */
static int on_key(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvKey* k = (const SmsEvKey*)ev->data;
	(void)mod;
	(void)user;
	if (k->scancode != toggle_key)
		return SMS_CONTINUE;
	if (k->down && !k->repeat)
		shown = !shown;
	return SMS_CANCEL;
}

static int on_draw(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvDraw* d = (const SmsEvDraw*)ev->data;
	SmsGameState st;
	SmsPlayer m;
	SmsRect r;
	char line[64];
	float speed;
	(void)user;

	memset(&st, 0, sizeof st);
	st.size = sizeof st;
	api->game->state(&st);
	if (d->mode != SMS_MODE_STAGE || st.paused || !shown || !api->config->get_int(mod, "show", 1))
		return SMS_CONTINUE;
	memset(&m, 0, sizeof m);
	m.size = sizeof m;
	if (!api->player->get(&m))
		return SMS_CONTINUE;

	/* velocity is in units a tick; a tick is 1/ticks_per_second seconds */
	speed = sqrtf(m.velocity.x * m.velocity.x + m.velocity.z * m.velocity.z) * (float)st.ticks_per_second;
	snprintf(line, sizeof line, "Speed %.0f", speed);
	/* a place on screen that no other mod's panel (or the game's HUD) has */
	if (!api->draw->hud_slot(mod, SMS_ANCHOR_BOTTOM_RIGHT, 110, 24, &r))
		return SMS_CONTINUE;
	api->draw->rect(r.x, r.y, r.w, r.h, SMS_RGBA(0, 0, 0, 160));
	api->draw->text(r.x + 10, r.y + 6, 14, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, line);
	return SMS_CONTINUE;
}

SMS_MOD_DEFINE()

SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* a)
{
	SmsOption show;
	api = a;

	api->core->subscribe(mod, SMS_EV_KEY, on_key, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_DRAW, on_draw, NULL, SMS_PRIORITY_NORMAL);
	toggle_key = api->input->bind_key(mod, "toggle", "O");

	memset(&show, 0, sizeof show);
	show.size  = sizeof show;
	show.key   = "show";
	show.label = "Show the speed";
	show.type  = SMS_OPTION_BOOL;
	show.def   = 1;
	api->config->add_option(mod, &show);

	api->core->log(mod, SMS_LOG_INFO, "loaded; %s toggles it", api->input->key_name(toggle_key));
	return 0;
}
