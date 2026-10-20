/* Squid Race: a server mod (side = server) for the Super Mario Sunshine PC
 * port's multiplayer.
 *
 * It runs on the host only, a dedicated server as a rule, and players join
 * with the plain game: nothing to install. Everyone races Ricco Harbor's
 * Blooper course (area 30, the second episode's secret course):
 *
 *   lobby      the players wait at the start until enough are in the course;
 *   countdown  the HUD's clock counts down; three seconds before the start
 *              each player is put on their place on the start line and held;
 *   start      every game puts its player on a Blooper at the same moment;
 *   racing     the host follows each player round the course: out of the
 *              start's area (the course's fast cube A), through B, through C
 *              and back into A; the first one round wins;
 *   winner     everyone sees who won, and watches them, for a while;
 *   restart    the course loads afresh for everyone: the lobby again.
 *
 * All of that is the host's: the rules, the clock, where everyone is round
 * the course. Players' games only carry out what it tells them (api->net's
 * commands): a banner now, the clock counting, this player to the start line,
 * onto a Blooper there. Their game draws everything with its own models and
 * HUD, as Minecraft's server tells the game what and where, and the game
 * draws it.
 *
 * Settings (the host's Mods menu, F1, or settings.ini in the mod's data
 * folder): players a race needs, the countdown, how long the winner is shown,
 * and a race's time limit.
 */
#include <sms_mod.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const SmsApi* api;
static SmsMod* self;

/* ------------------------------------------------------------------ the course */

enum { COURSE_AREA = 30, COURSE_EPISODE = 0 };
#define BLOOPER_FIRST 0x400000C5 /* red, yellow, green */
#define BLOOPER_LAST  0x400000C7
#define SHINE_TYPE    0x20000013
#define START_SOUND   0x4851     /* the course script's own: the start, and back home */
#define GOAL_SOUND    0x4852
#define LIVES_FLAG    0x20001
static const char kScript[] = "/scene/map/sp/gessoride.sb"; /* its time trial: stopped, the race is ours */

/* The start line across the start's shallows (floor at 10, water at 100),
   the middle first, facing up the channel (the Bloopers' own way). */
static const float kStartX[7] = {12000.0f, 11700.0f, 12300.0f, 11400.0f, 12600.0f, 11100.0f, 12900.0f};
static const float kStartZ = -5800.0f, kWaterY = 100.0f, kFacing = 180.0f;
static const double kLineUp = 3.0; /* seconds before the start: on the line, held */

/* ------------------------------------------------------------------ the race */

enum Phase { LOBBY, COUNTDOWN, RACING, WINNER, RESTART };
static const char* const kPhaseName[] = {"lobby", "countdown", "racing", "winner", "restart"};

static enum Phase phase = LOBBY;
static double ends;        /* core->time() when the phase is over */
static double started;     /* when the race under way began */
static int round_no;
static int winner = -1;    /* slot; -1: none (time ran out) */
static double winner_time;
static int lined_up;       /* the line-up and the start's commands are out */
static int last_second = -1;
static uint32_t last_racers = 0xFFFFFFFFu;
static int set_up;         /* the mode said, the course asked for */
static double restart_asked;

/* each player's way round: 0 at the start (in A), 1 out of A, 2 in B, 3 out
   of B, 4 in C, 5 out of C, 6 home */
static int checkpoint[SMS_NET_MAX_PLAYERS];
static double finished[SMS_NET_MAX_PLAYERS];

static int min_racers(void) { return api->config->get_int(self, "min_players", 2); }
static double countdown_s(void) { return api->config->get_int(self, "countdown", 20); }
static double show_s(void) { return api->config->get_int(self, "winner_seconds", 10); }
static double limit_s(void) { return api->config->get_int(self, "time_limit", 180); }

static double now(void) { return api->core->time(); }

/* "1:02.35" */
static void format_time(char* out, size_t size, double seconds)
{
	const unsigned cs = (unsigned)(seconds * 100.0 + 0.5);
	snprintf(out, size, "%u:%02u.%02u", cs / 6000, cs / 100 % 60, cs % 100);
}

/* ------------------------------------------------------------------ commands */

static SmsCmd cmd(uint32_t type)
{
	SmsCmd c;
	memset(&c, 0, sizeof c);
	c.size = sizeof c;
	c.type = type;
	return c;
}

static void banner(uint32_t to, SmsColor color, const char* text)
{
	SmsCmd c = cmd(SMS_CMD_BANNER);
	c.i[0]   = (int32_t)color;
	c.text   = text;
	api->net->send(to, &c);
}

static void notice(uint32_t to, const char* text)
{
	SmsCmd c = cmd(SMS_CMD_NOTICE);
	c.text   = text;
	api->net->send(to, &c);
}

/* The standing line everyone sees: kept, so a player arriving later has it. */
static void status(const char* text)
{
	SmsCmd c = cmd(SMS_CMD_STATUS);
	c.flags  = SMS_CMD_STICKY;
	c.text   = text;
	api->net->send(SMS_NET_ALL, &c);
}

static void timer(uint32_t to, int mode, double seconds, uint32_t delay_ms)
{
	SmsCmd c   = cmd(SMS_CMD_TIMER);
	c.delay_ms = delay_ms;
	c.i[0]     = mode;
	c.i[1]     = (int32_t)(seconds * 100.0);
	api->net->send(to, &c);
}

static void one(uint32_t to, uint32_t type, int32_t value, uint32_t delay_ms)
{
	SmsCmd c   = cmd(type);
	c.delay_ms = delay_ms;
	c.i[0]     = value;
	api->net->send(to, &c);
}

static void place(uint32_t type, int slot, int line, uint32_t delay_ms)
{
	SmsCmd c   = cmd(type);
	c.delay_ms = delay_ms;
	c.i[0]     = BLOOPER_FIRST;
	c.i[1]     = BLOOPER_LAST;
	c.f[0]     = kStartX[line];
	c.f[1]     = kWaterY;
	c.f[2]     = kStartZ;
	c.f[3]     = kFacing;
	api->net->send(SMS_NET_SLOT(slot), &c);
}

/* The course as a race needs it, in every game (the host's too): its own
   time trial stopped, the Bloopers out of reach until the start, no Shine to
   get mid-race, and nobody wandering off. Sticky: replayed to each player as
   they arrive, a player back after falling in too. */
static void course_setup(void)
{
	SmsCmd c = cmd(SMS_CMD_STOP_SCRIPT);
	c.flags  = SMS_CMD_STICKY;
	c.text   = kScript;
	api->net->send(SMS_NET_ALL, &c);
	c        = cmd(SMS_CMD_ACTORS);
	c.flags  = SMS_CMD_STICKY;
	c.i[0]   = BLOOPER_FIRST;
	c.i[1]   = BLOOPER_LAST;
	c.i[2]   = SMS_ACTORS_HIDE;
	api->net->send(SMS_NET_ALL, &c);
	c.i[0]   = SHINE_TYPE;
	c.i[1]   = SHINE_TYPE;
	c.i[2]   = SMS_ACTORS_KILL;
	api->net->send(SMS_NET_ALL, &c);
	c        = cmd(SMS_CMD_STAY);
	c.flags  = SMS_CMD_STICKY;
	c.i[0]   = 1;
	api->net->send(SMS_NET_ALL, &c);
}

/* ------------------------------------------------------------------ players */

static int player(int slot, SmsNetPlayer* p)
{
	memset(p, 0, sizeof *p);
	p->size = sizeof *p;
	return api->net->player(slot, p);
}

/* Who races: players (not the server) in the course now. */
static uint32_t racers(void)
{
	uint32_t bits = 0;
	int slot;
	SmsNetPlayer p;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (player(slot, &p) && !p.server && p.in_stage)
			bits |= SMS_NET_SLOT(slot);
	return bits;
}

static int count(uint32_t bits)
{
	int n = 0;
	for (; bits; bits &= bits - 1)
		n++;
	return n;
}

/* Their place on the start line: by slot, the server's left out. */
static int line_of(int slot)
{
	const int i = api->net->dedicated() ? slot - 1 : slot;
	return ((i % 7) + 7) % 7;
}

static const char* name_of(int slot, char* out, size_t size)
{
	SmsNetPlayer p;
	snprintf(out, size, "%s", player(slot, &p) ? p.name : "Someone");
	return out;
}

/* ------------------------------------------------------------------ phases */

static void probe_status(void)
{
	char text[64], name[24];
	switch (phase) {
	case LOBBY:
		snprintf(text, sizeof text, "Squid Race: waiting for players (%d of %d)", count(racers()), min_racers());
		break;
	case COUNTDOWN: snprintf(text, sizeof text, "Squid Race: about to start"); break;
	case RACING: snprintf(text, sizeof text, "Squid Race: racing"); break;
	case WINNER:
		if (winner >= 0)
			snprintf(text, sizeof text, "Squid Race: %.23s won", name_of(winner, name, sizeof name));
		else
			snprintf(text, sizeof text, "Squid Race: time ran out");
		break;
	default: snprintf(text, sizeof text, "Squid Race: next race soon"); break;
	}
	api->net->set_status(text);
}

static void enter(enum Phase next, double seconds)
{
	phase = next;
	ends  = now() + seconds;
	api->core->log(self, SMS_LOG_INFO, "race %d: %s (%d racing)", round_no, kPhaseName[next], count(racers()));
	probe_status();
}

static void lobby(void)
{
	int slot;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		checkpoint[slot] = 0;
	lined_up    = 0;
	last_second = -1;
	winner      = -1;
	last_racers = 0xFFFFFFFFu; /* the status line again */
	timer(SMS_NET_ALL, SMS_TIMER_HIDE, 0, 0);
	banner(SMS_NET_ALL, 0, "");
	one(SMS_NET_ALL, SMS_CMD_HOLD, 0, 0);
	enter(LOBBY, 0);
}

/* Three seconds to go: everyone on the line and held, "Ready... Go!", and the
   start itself sent now, to be carried out in every game at once. */
static void line_up(double left)
{
	const uint32_t to_go = (uint32_t)(left * 1000.0);
	const uint32_t bits  = racers();
	int slot;
	lined_up = 1;
	banner(SMS_NET_ALL, 0, "");
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
		if (!(bits & SMS_NET_SLOT(slot)))
			continue;
		place(SMS_CMD_WARP, slot, line_of(slot), 0);
		one(SMS_NET_SLOT(slot), SMS_CMD_HOLD, 1, 0);
		place(SMS_CMD_MOUNT, slot, line_of(slot), to_go);
	}
	one(SMS_NET_ALL, SMS_CMD_READY_GO, (int32_t)to_go, 0);
	one(SMS_NET_ALL, SMS_CMD_HOLD, 0, to_go);
	timer(SMS_NET_ALL, SMS_TIMER_UP, 0, to_go);
	one(SMS_NET_ALL, SMS_CMD_SOUND, START_SOUND, to_go);
	one(SMS_NET_ALL, SMS_CMD_MUSIC, SMS_MUSIC_EVENT, to_go);
}

static void race_won(int slot, double seconds)
{
	char text[96], t[16], name[24];
	SmsNetPlayer p;
	const SmsColor color = player(slot, &p) ? p.color : SMS_RGB(255, 255, 255);
	int other;
	winner      = slot;
	winner_time = seconds;
	format_time(t, sizeof t, seconds);
	snprintf(text, sizeof text, "%s WINS!  %s", name_of(slot, name, sizeof name), t);
	banner(SMS_NET_ALL & ~SMS_NET_SLOT(slot), color, text);
	snprintf(text, sizeof text, "YOU WIN!  %s", t);
	banner(SMS_NET_SLOT(slot), color, text);
	timer(SMS_NET_ALL, SMS_TIMER_SHOW, seconds, 0);
	one(SMS_NET_ALL, SMS_CMD_MUSIC, SMS_MUSIC_FANFARE_RACE, 0);
	one(SMS_NET_SLOT(slot), SMS_CMD_HOLD, 1, 0);
	/* everyone else rides along with the winner */
	for (other = 0; other < SMS_NET_MAX_PLAYERS; other++)
		if (other != slot && player(other, &p) && !p.server)
			one(SMS_NET_SLOT(other), SMS_CMD_WATCH, slot, 0);
	api->core->log(self, SMS_LOG_INFO, "race %d won by %s in %s", round_no, name, t);
	enter(WINNER, show_s());
}

static void time_up(void)
{
	winner = -1;
	banner(SMS_NET_ALL, SMS_RGB(255, 255, 255), "TIME'S UP!");
	one(SMS_NET_ALL, SMS_CMD_HOLD, 1, 0);
	enter(WINNER, show_s());
}

/* The host's own word on where a player is round the course: the cubes the
   course's script tested, a little above the feet, as it did. */
static int in_cube(int set, const SmsVec3* at)
{
	SmsVec3 p = *at;
	p.y += 10.0f;
	return api->game->in_cube(set, 0, &p) == 1;
}

static void follow(int slot, const SmsNetPlayer* p)
{
	int* c = &checkpoint[slot];
	char t[16], text[48];
	switch (*c) {
	case 0: if (!in_cube(SMS_CUBE_FAST_A, &p->position)) *c = 1; break;
	case 1: if (in_cube(SMS_CUBE_FAST_B, &p->position)) *c = 2; break;
	case 2:
		if (!in_cube(SMS_CUBE_FAST_B, &p->position)) {
			*c = 3;
			notice(SMS_NET_SLOT(slot), "Halfway! On to the last stretch");
		}
		break;
	case 3: if (in_cube(SMS_CUBE_FAST_C, &p->position)) *c = 4; break;
	case 4:
		if (!in_cube(SMS_CUBE_FAST_C, &p->position)) {
			*c = 5;
			notice(SMS_NET_SLOT(slot), "Home straight: back to the start!");
		}
		break;
	case 5:
		if (in_cube(SMS_CUBE_FAST_A, &p->position)) {
			*c             = 6;
			finished[slot] = now() - started;
			format_time(t, sizeof t, finished[slot]);
			snprintf(text, sizeof text, "ROUND IN %s", t);
			one(SMS_NET_SLOT(slot), SMS_CMD_SOUND, GOAL_SOUND, 0);
			timer(SMS_NET_SLOT(slot), SMS_TIMER_SHOW, finished[slot], 0);
			banner(SMS_NET_SLOT(slot), SMS_RGB(255, 255, 255), text);
			one(SMS_NET_SLOT(slot), SMS_CMD_HOLD, 1, 0);
			api->core->log(self, SMS_LOG_INFO, "slot %d round in %s", slot, t);
			if (phase == RACING && winner < 0)
				race_won(slot, finished[slot]);
		}
		break;
	default: break;
	}
}

/* ------------------------------------------------------------------ each frame */

static void frame(void)
{
	const double t = now();
	const uint32_t bits = racers();
	char text[96];
	int slot;
	SmsGameState st;
	if (api->net->role() != SMS_NET_HOST)
		return;
	if (!set_up) {
		set_up = 1;
		api->net->set_mode("squid-race");
		api->net->load_stage(COURSE_AREA, COURSE_EPISODE);
		probe_status();
		api->core->log(self, SMS_LOG_INFO, "hosting the Squid Race: %d players start a race", min_racers());
	}
	memset(&st, 0, sizeof st);
	st.size = sizeof st;
	api->game->state(&st);
	if (st.area != COURSE_AREA || !st.stage_ready)
		return;
	/* falling in costs a life: the host keeps them up, so a race has no game over */
	if (api->game->lives() < 5)
		api->game->flag_set(LIVES_FLAG, 9);
	if (bits != last_racers && (phase == LOBBY || phase == COUNTDOWN)) {
		last_racers = bits;
		if (phase == LOBBY)
			snprintf(text, sizeof text, "Squid Race: waiting for players (%d of %d here)", count(bits), min_racers());
		else
			snprintf(text, sizeof text, "%d racing: everyone starts on a Blooper at once", count(bits));
		status(text);
		probe_status();
	}
	switch (phase) {
	case LOBBY:
		if (count(bits) >= min_racers()) {
			round_no++;
			timer(SMS_NET_ALL, SMS_TIMER_DOWN, countdown_s(), 0);
			enter(COUNTDOWN, countdown_s());
			last_racers = 0xFFFFFFFFu;
		}
		break;
	case COUNTDOWN: {
		const double left = ends - t;
		if (count(bits) < min_racers() && !lined_up) {
			lobby(); /* someone left: wait again */
			break;
		}
		if (left > kLineUp && (int)ceil(left) != last_second) {
			last_second = (int)ceil(left);
			snprintf(text, sizeof text, "RACE STARTS IN %d", last_second);
			banner(SMS_NET_ALL, SMS_RGB(255, 208, 32), text);
		}
		if (left <= kLineUp && !lined_up)
			line_up(left > 0 ? left : 0);
		if (left <= 0) {
			started = ends;
			for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
				checkpoint[slot] = 0;
			status("");
			enter(RACING, limit_s());
		}
		break;
	}
	case RACING: {
		SmsNetPlayer p;
		for (slot = 0; slot < SMS_NET_MAX_PLAYERS && phase == RACING; slot++)
			if ((bits & SMS_NET_SLOT(slot)) && player(slot, &p) && (p.flags & SMS_NETP_VISIBLE))
				follow(slot, &p);
		if (phase == RACING && t >= ends)
			time_up();
		break;
	}
	case WINNER:
		if ((int)ceil(ends - t) != last_second && ends - t > 0) {
			last_second = (int)ceil(ends - t);
			snprintf(text, sizeof text, "Next race in %d", last_second);
			status(text);
		}
		if (t >= ends) {
			enter(RESTART, 0);
			restart_asked = t;
			api->net->load_stage(COURSE_AREA, COURSE_EPISODE);
		}
		break;
	case RESTART:
		if (t - restart_asked > 15.0) { /* not under way yet (a fade): again */
			restart_asked = t;
			api->net->load_stage(COURSE_AREA, COURSE_EPISODE);
		}
		break;
	}
}

/* ------------------------------------------------------------------ events */

static int on_frame(SmsMod* mod, SmsEvent* ev, void* user)
{
	(void)mod;
	(void)ev;
	(void)user;
	frame();
	return SMS_CONTINUE;
}

/* A new stage session: the course afresh (the lobby), or somewhere else (back
   to the course). */
static int on_session(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetSession* s = (const SmsEvNetSession*)ev->data;
	(void)mod;
	(void)user;
	if (s->area != COURSE_AREA) {
		api->net->load_stage(COURSE_AREA, COURSE_EPISODE);
		return SMS_CONTINUE;
	}
	course_setup();
	lobby();
	return SMS_CONTINUE;
}

/* A player in the course: one joining mid-countdown sees the clock; one
   arriving during the race (just joined, or back after falling in) goes
   round from the start, on a Blooper there, the race's clock running. */
static int on_arrived(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetPlayer* p = (const SmsEvNetPlayer*)ev->data;
	const double t          = now();
	(void)mod;
	(void)user;
	api->core->log(self, SMS_LOG_INFO, "%s (slot %d) in the course (%s)", p->name, p->slot, kPhaseName[phase]);
	switch (phase) {
	case COUNTDOWN:
		timer(SMS_NET_SLOT(p->slot), SMS_TIMER_DOWN, ends - t > 0 ? ends - t : 0, 0);
		break;
	case RACING:
		checkpoint[p->slot] = 0;
		place(SMS_CMD_MOUNT, p->slot, line_of(p->slot), 0);
		one(SMS_NET_SLOT(p->slot), SMS_CMD_HOLD, 0, 0);
		timer(SMS_NET_SLOT(p->slot), SMS_TIMER_UP, t - started, 0);
		break;
	default:
		break;
	}
	return SMS_CONTINUE;
}

static int on_joined(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetPlayer* p = (const SmsEvNetPlayer*)ev->data;
	char text[96];
	(void)mod;
	(void)user;
	if (api->net->role() != SMS_NET_HOST)
		return SMS_CONTINUE;
	snprintf(text, sizeof text, "Welcome to the Squid Race, %s!", p->name);
	notice(SMS_NET_SLOT(p->slot), text);
	return SMS_CONTINUE;
}

static void add_number(const char* key, const char* label, int def, int min, int max, int step)
{
	SmsOption o;
	memset(&o, 0, sizeof o);
	o.size  = sizeof o;
	o.key   = key;
	o.label = label;
	o.type  = SMS_OPTION_INT;
	o.def   = (float)def;
	o.min   = (float)min;
	o.max   = (float)max;
	o.step  = (float)step;
	api->config->add_option(self, &o);
}

SMS_MOD_DEFINE()

SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* a)
{
	api  = a;
	self = mod;
	if (!SMS_API_HAS(api, net) || !SMS_API_HAS(api->game, in_cube)) {
		api->core->log(mod, SMS_LOG_ERROR, "needs a game with mod API 1.2 (multiplayer servers)");
		return 1;
	}
	add_number("min_players", "Players a race needs", 2, 1, 8, 1);
	add_number("countdown", "Countdown (seconds)", 20, 5, 60, 5);
	add_number("winner_seconds", "Winner shown (seconds)", 10, 3, 30, 1);
	add_number("time_limit", "Time limit (seconds)", 180, 60, 600, 30);
	api->core->subscribe(mod, SMS_EV_FRAME, on_frame, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_SESSION, on_session, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_ARRIVED, on_arrived, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_JOINED, on_joined, NULL, SMS_PRIORITY_NORMAL);
	return 0;
}
