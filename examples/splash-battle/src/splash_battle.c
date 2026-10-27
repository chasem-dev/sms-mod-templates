/* Splash Battle: a server mod (side = server) for the Super Mario Sunshine PC
 * port's multiplayer: a water fight, last one standing wins.
 *
 * It runs on the host only, a dedicated server as a rule, and players join
 * with the plain game: nothing to install. Everyone is in the arena (Gelato
 * Beach by default) with FLUDD, and FLUDD's water hits the other players
 * (SMS_RULE_WATER_HITS). Whoever is hit reacts as Shadow Mario does when
 * Mario sprays him in his chases (SMS_CMD_SPLASH): his splash and hit sound,
 * his flush, and his water meter over their head. Empty it and they go
 * down as he does, out of the round, watching the others.
 *
 *   lobby      the players wait in the arena, free to play, until enough are in;
 *   countdown  the clock counts down; three seconds before the start every
 *              player is put on a ring round the arena's middle, facing it,
 *              held, and "Ready... Go!";
 *   battle     water hits; each hit takes a drop off the meter, the HUD's
 *              health shows what is left; FLUDD starts a fifth full, uses
 *              water fast and fills up only from the capsules spread over the
 *              arena (each there a while, then elsewhere; a taken one back
 *              after a cooldown); the arena's
 *              edge, a red line on the ground, closes in. The last one
 *              standing wins; at the time limit, the fullest meter;
 *   winner     everyone sees who won, and watches them, for a while;
 *   restart    the arena loads afresh for everyone: the lobby again.
 *
 * Players collide in the battle (net->set_collision), so nobody hides inside
 * anyone and a stomp lands. All of it is the host's: the meters, the zone, who
 * is out. Players' games only carry out what it tells them.
 *
 * Settings (the host's Mods menu, F1, or settings.ini in the mod's data
 * folder): players a round needs, the countdown, the meter, the time limit,
 * the arena's edge and its closing, water (at the start, its use, refills,
 * pickups), collision, and the arena (area and episode).
 */
#include <sms_mod.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const SmsApi* api;
static SmsMod* self;

/* ------------------------------------------------------------------ the arena */

#define LIVES_FLAG      0x20001
#define SHINE_TYPE      0x20000013
#define ENEMY_FIRST     0x10000001 /* the stage's enemies: out of the way */
#define ENEMY_LAST      0x1000FFFF
#define MUSIC_BATTLE    ((int32_t)0x80010016u) /* MSD_BGM_KAGEMARIO: Shadow Mario's chase */
#define MUSIC_GELATO    ((int32_t)0x80010003u) /* MSD_BGM_MAMMA: Gelato Beach's own */
#define SOUND_OUT       0x4823                 /* SMS_SOUND_DAMAGE */
#define SOUND_START     0x4851                 /* a race's start */
#define RING_RADIUS     700.0f                 /* the line-up's ring round the middle */
#define MOST_PICKUPS    8                      /* markers 0..7 */
#define MOST_SPAWNS     110 /* bottles a session may make: the game keeps 128 spawns a stage */
#define PICKUP_LIFE     20  /* seconds an untaken capsule stays */
#define SOUND_WATER     0x4801                 /* SMS_SOUND_HEAL: water back */
#define PI_F            3.14159265f

static const double kLineUp = 3.0; /* seconds before the start: on the ring, held */

/* ------------------------------------------------------------------ the round */

enum Phase { LOBBY, COUNTDOWN, BATTLE, WINNER, RESTART };
static const char* const kPhaseName[] = {"lobby", "countdown", "battle", "winner", "restart"};

static enum Phase phase = LOBBY;
static double ends;            /* core->time() when the phase is over */
static double started;         /* when the battle under way began */
static int round_no;
static int winner = -1;        /* slot; -1: nobody (a draw) */
static int lined_up;
static int last_second = -1;
static uint32_t last_here = 0xFFFFFFFFu;
static int set_up;
static double restart_asked;
static double next_board;
static double decided_at;      /* the round's last fall: the winner is called a moment after, for all to see it */

static SmsVec3 middle;         /* the arena's middle: where the stage starts its players */
static int have_middle;
static SmsVec3 lobby_at;       /* the waiting lobby's middle */
static int edge_out;           /* the edge is out for this session, and players are brought inside it */
static float zone_radius;      /* the arena's edge as last sent: where it goes */
static float zone_from;        /* and where it glides from, from when, for how long */
static double zone_since, zone_secs;
static int zone_closing;       /* the closing glide is out */

/* the water pickups: where each is, whether it is up, when it comes back */
static struct Pickup {
	SmsVec3 at;
	int up;
	int id;        /* the bottle's spawn id (SMS_CMD_SPAWN's i[3]) */
	double back_at;
	double gone_at; /* when it goes, untaken, to come back elsewhere */
} pickups[MOST_PICKUPS];
static float pickup_spin; /* this round's turn of the capsules' slices of the arena */
static int next_spawn_id = 1; /* each bottle a new one: a taken one is gone for good */
static unsigned rand_state = 0x2545F491u;
static double rand_unit(void)
{
	rand_state ^= rand_state << 13;
	rand_state ^= rand_state >> 17;
	rand_state ^= rand_state << 5;
	return (double)(rand_state % 100000u) / 100000.0;
}

/* each player in this round */
static int fighting[SMS_NET_MAX_PLAYERS]; /* in the round, not out */
static int in_round[SMS_NET_MAX_PLAYERS]; /* started the round (fighting or out) */
static int meter[SMS_NET_MAX_PLAYERS];
static int soaked[SMS_NET_MAX_PLAYERS];   /* players they put out */
static double down_at[SMS_NET_MAX_PLAYERS]; /* when they went down (to watch someone after) */
static int down_by[SMS_NET_MAX_PLAYERS];
static int watching[SMS_NET_MAX_PLAYERS];
static int place_of[SMS_NET_MAX_PLAYERS]; /* 1 the winner; 2 the last one out... */

static int min_players(void) { return api->config->get_int(self, "min_players", 2); }
static double countdown_s(void) { return api->config->get_int(self, "countdown", 15); }
static int full_meter(void) { return api->config->get_int(self, "meter", 300); }
static double limit_s(void) { return api->config->get_int(self, "time_limit", 180); }
static double show_s(void) { return api->config->get_int(self, "winner_seconds", 10); }
static int zone_on(void) { return api->config->get_int(self, "zone", 1); }
static float zone_start(void) { return (float)api->config->get_int(self, "zone_radius", 3600); }
static float zone_end(void) { return (float)api->config->get_int(self, "zone_end", 600); }
static int refill_on(void) { return api->config->get_int(self, "refill", 0); }
static int start_water(void) { return api->config->get_int(self, "start_water", 19); }
static int water_use(void) { return api->config->get_int(self, "water_use", 300); }
static int pickup_count(void)
{
	const int n = api->config->get_int(self, "pickups", 8);
	return n < MOST_PICKUPS ? n : MOST_PICKUPS;
}
static int pickup_water(void) { return api->config->get_int(self, "pickup_water", 12); }
static double pickup_cooldown(void) { return api->config->get_int(self, "pickup_cooldown", 30); }
static double pickup_life(void) { return api->config->get_int(self, "pickup_life", PICKUP_LIFE); }
static int collide_on(void) { return api->config->get_int(self, "collision", 1); }
static int arena_area(void) { return api->config->get_int(self, "area", 4); }
static int stage_music_on(void) { return api->config->get_int(self, "stage_music", 0); }
/* the arena's middle on the map: on Gelato Beach's sand, its widest stretch
   (round it, 3800 units of beach and dunes, 800 from the sea); 0, 0: where
   the stage starts its players */
static int middle_x(void) { return api->config->get_int(self, "middle_x", 1800); }
static int middle_z(void) { return api->config->get_int(self, "middle_z", 2800); }
static int arena_episode(void) { return api->config->get_int(self, "episode", 0); }
/* the waiting lobby: on Gelato Beach the sandbar where the stage puts its
   players (dry sand 900 units round it); 0, 0: where the stage puts them */
static int lobby_x(void) { return api->config->get_int(self, "lobby_x", 10600); }
static int lobby_z(void) { return api->config->get_int(self, "lobby_z", -3600); }
static int lobby_radius(void) { return api->config->get_int(self, "lobby_radius", 850); }
static int markers_on(void) { return api->config->get_int(self, "pickup_markers", 0); }

static double now(void) { return api->core->time(); }

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

/* The stage's music off in a player's game, a moment after they arrive (the
   stage starts it as they come in). A command stops only music a command
   started, so it starts the stage's own (the same, playing on) and stops
   that. Gelato Beach's: an arena elsewhere keeps its stage's music. */
static void stage_music_off(uint32_t to)
{
	if (stage_music_on() || arena_area() != 4)
		return;
	one(to, SMS_CMD_MUSIC, MUSIC_GELATO, 2000);
	one(to, SMS_CMD_MUSIC, 0, 2200);
}

/* What the players may do, kept for the session (newcomers get it too). */
static void rules(uint32_t bits, uint32_t delay_ms)
{
	SmsCmd c   = cmd(SMS_CMD_RULES);
	c.flags    = SMS_CMD_STICKY;
	c.delay_ms = delay_ms;
	c.i[0]     = (int32_t)bits;
	api->net->send(SMS_NET_ALL, &c);
}

static void player_cmd(int slot, int action, int value)
{
	SmsCmd c = cmd(SMS_CMD_PLAYER);
	c.i[0]   = action;
	c.i[1]   = value;
	api->net->send(SMS_NET_SLOT(slot), &c);
}

/* Shadow Mario's splash on `slot`, in every game, their meter as it is now. */
static void splash(int slot, const SmsVec3* at, uint32_t bits)
{
	SmsCmd c = cmd(SMS_CMD_SPLASH);
	c.i[0]   = slot;
	c.i[1]   = meter[slot];
	c.i[2]   = full_meter();
	c.i[3]   = (int32_t)bits;
	c.f[0]   = at->x, c.f[1] = at->y, c.f[2] = at->z;
	api->net->send(SMS_NET_ALL, &c);
}

static void board_row(int row, SmsColor color, const char* text)
{
	SmsCmd c = cmd(SMS_CMD_BOARD);
	c.flags  = SMS_CMD_STICKY;
	c.i[0]   = row;
	c.i[1]   = (int32_t)color;
	c.text   = text;
	api->net->send(SMS_NET_ALL, &c);
}

/* ------------------------------------------------------------------ players */

static int player(int slot, SmsNetPlayer* p)
{
	memset(p, 0, sizeof *p);
	p->size = sizeof *p;
	return api->net->player(slot, p);
}

/* Who plays: players (not the server) in the arena now. */
static uint32_t here(void)
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

static uint32_t standing(void)
{
	uint32_t bits = 0;
	int slot;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (fighting[slot])
			bits |= SMS_NET_SLOT(slot);
	return bits;
}

static const char* name_of(int slot, char* out, size_t size)
{
	SmsNetPlayer p;
	snprintf(out, size, "%s", player(slot, &p) ? p.name : "Someone");
	return out;
}

static SmsColor color_of(int slot)
{
	SmsNetPlayer p;
	return player(slot, &p) ? p.color : SMS_RGB(255, 255, 255);
}

/* The HUD's health shows the meter: 8 full, 1 the last drops. */
static void health_shows(int slot)
{
	const int full = full_meter();
	int h          = (meter[slot] * 8 + full - 1) / (full > 0 ? full : 1);
	player_cmd(slot, SMS_PLAYER_HEALTH, h < 1 ? 1 : h > 8 ? 8 : h);
}

/* ------------------------------------------------------------------ the board */

static void board(void)
{
	char text[64], name[24];
	int slot, row = 0;
	const int full = full_meter();
	board_row(row++, SMS_RGB(120, 200, 255), phase == BATTLE ? "SPLASH BATTLE" : "SPLASH BATTLE: last round");
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS && row < 8; slot++) {
		if (!in_round[slot])
			continue;
		if (fighting[slot])
			snprintf(text, sizeof text, "%.16s  %3d%%", name_of(slot, name, sizeof name),
			         full > 0 ? meter[slot] * 100 / full : 0);
		else if (place_of[slot] == 1)
			snprintf(text, sizeof text, "%.16s  WINNER", name_of(slot, name, sizeof name));
		else
			snprintf(text, sizeof text, "%.16s  soaked", name_of(slot, name, sizeof name));
		if (soaked[slot] && strlen(text) + 8 < sizeof text)
			snprintf(text + strlen(text), sizeof text - strlen(text), "  x%d", soaked[slot]);
		board_row(row++, fighting[slot] || place_of[slot] == 1 ? color_of(slot) : SMS_RGB(130, 130, 140), text);
	}
	for (; row < 8; row++)
		board_row(row, 0, "");
}

static void board_clear(void)
{
	SmsCmd c = cmd(SMS_CMD_BOARD);
	c.flags  = SMS_CMD_STICKY;
	c.i[0]   = -1;
	c.text   = "";
	api->net->send(SMS_NET_ALL, &c);
}

/* ------------------------------------------------------------------ the arena */

/* The arena's edge in every game (SMS_CMD_BOUNDS): players are kept inside
   it, and it is drawn as a red line on the ground. Kept for the session, so
   a player arriving later has it too; `seconds` glides it there. */
static void edge(const SmsVec3* at, float radius, double seconds)
{
	SmsCmd c = cmd(SMS_CMD_BOUNDS);
	c.flags  = SMS_CMD_STICKY;
	c.i[0]   = SMS_BOUNDS_RING;
	c.i[1]   = (int32_t)SMS_RGB(235, 30, 30);
	c.f[0]   = at->x, c.f[1] = at->y, c.f[2] = at->z;
	c.f[3]   = radius;
	c.f2[0]  = (float)seconds;
	api->net->send(SMS_NET_ALL, &c);
}

/* The arena's edge at time t, as players' games glide it (in a line). */
static float zone_at(double t)
{
	if (zone_secs <= 0.0 || t >= zone_since + zone_secs)
		return zone_radius;
	if (t <= zone_since)
		return zone_from;
	return zone_from + (zone_radius - zone_from) * (float)((t - zone_since) / zone_secs);
}

static void arena(float radius, double seconds)
{
	const double t = now();
	zone_from      = zone_radius > 0.0f ? zone_at(t) : radius;
	zone_since     = t;
	zone_secs      = seconds;
	zone_radius    = radius;
	edge(&middle, radius, seconds);
}

/* The HUD's parts hidden in every game: those a fight has no use for, and
   `more` (FLUDD's water while nobody wears FLUDD). */
static void hud(uint32_t more, uint32_t delay_ms)
{
	SmsCmd c   = cmd(SMS_CMD_HUD);
	c.flags    = SMS_CMD_STICKY;
	c.delay_ms = delay_ms;
	c.i[0]     = (int32_t)(SMS_HUD_SHINES | SMS_HUD_COINS | SMS_HUD_BLUE_COINS | SMS_HUD_RED_COINS | SMS_HUD_LIVES | more);
	api->net->send(SMS_NET_ALL, &c);
}

/* The waiting lobby: a small ring where the stage puts its players (on
   Gelato Beach the sandbar they come in on), FLUDD off everyone's back. */
static void lobby_edge(void)
{
	SmsCmd c = cmd(SMS_CMD_PLAYER);
	edge(&lobby_at, (float)lobby_radius(), 0);
	c.flags = SMS_CMD_STICKY; /* players arriving later too */
	c.i[0]  = SMS_PLAYER_FLUDD;
	c.i[1]  = 0;
	api->net->send(SMS_NET_ALL, &c);
}

/* Over the battle the arena closes from its start to its end radius: the
   first fifth of the time limit at full size, then gliding in until
   seven-eighths of it, pushing everyone before it. */
static void zone_tick(double t)
{
	const double grace = 0.2, closed = 0.85;
	if (!zone_on() || !have_middle || zone_closing || t - started < grace * limit_s())
		return;
	zone_closing = 1;
	arena(zone_end(), (closed - grace) * limit_s());
	notice(SMS_NET_ALL, "The arena is closing in!");
}

/* ------------------------------------------------------------------ water */

/* Another capsule up within `apart` of `at`. */
static int pickup_near(int k, const SmsVec3* at, float apart)
{
	int j;
	for (j = 0; j < MOST_PICKUPS; j++) {
		float dx, dz;
		if (j == k || !pickups[j].up)
			continue;
		dx = pickups[j].at.x - at->x;
		dz = pickups[j].at.z - at->z;
		if (dx * dx + dz * dz < apart * apart)
			return 1;
	}
	return 0;
}

/* A spot in the arena for capsule k: on the ground (not in the water),
   inside the edge as it will be when the capsule moves on (the edge closing
   in meanwhile), spread over all of it: each capsule has its own slice of
   the arena (as a cake is cut, turned each round), anywhere from near the
   middle out to near the edge, and keeps clear of the others. */
static int pickup_spot(int k, SmsVec3* at)
{
	const float slice  = 2.0f * PI_F / (float)(pickup_count() > 0 ? pickup_count() : 1);
	const float radius = zone_at(now() + pickup_life());
	int tries;
	for (tries = 0; tries < 24; tries++) {
		/* its slice first; then anywhere; the last tries anywhere at all */
		const float a = tries < 12 ? pickup_spin + ((float)k + (float)rand_unit()) * slice
		                           : (float)rand_unit() * 2.0f * PI_F;
		const float r = (float)sqrt(0.02 + 0.98 * rand_unit()) * radius * 0.85f;
		SmsGround g;
		at->x = middle.x + r * sinf(a);
		at->z = middle.z + r * cosf(a);
		at->y = middle.y + 600.0f;
		if (tries < 20 && pickup_near(k, at, radius * 0.3f))
			continue;
		memset(&g, 0, sizeof g);
		g.size = sizeof g;
		if (!api->game->ground(at, &g) || g.floor < middle.y - 800.0f)
			continue;
		if (g.has_water && g.water_y > g.floor + 20.0f)
			continue; /* in the water: somewhere else */
		at->y = g.floor;
		return 1;
	}
	return 0;
}

/* A capsule: one of the stage's big water bottles (SMS_CMD_SPAWN: the stage
   has them, so their model is there), shrunk to a capsule, and with the
   setting a WATER marker over it. Its own refill is off under the battle's
   SMS_RULE_NO_REFILL: whoever reaches it gets the server's share. */
static void pickup_show(int k, double t)
{
	char kind[16];
	SmsCmd c;
	if (next_spawn_id > MOST_SPAWNS) {
		pickups[k].back_at = t + 1e9; /* this session's spawns are used up */
		return;
	}
	if (!pickup_spot(k, &pickups[k].at)) {
		pickups[k].back_at = t + 5.0; /* try again soon */
		return;
	}
	pickups[k].up      = 1;
	pickups[k].id      = next_spawn_id++;
	pickups[k].gone_at = t + pickup_life();
	c             = cmd(SMS_CMD_SPAWN);
	c.flags       = SMS_CMD_STICKY; /* a player arriving later sees it too */
	c.text        = "bottle_large";
	c.i[3]        = pickups[k].id;
	c.f[0] = pickups[k].at.x, c.f[1] = pickups[k].at.y, c.f[2] = pickups[k].at.z;
	c.f[3]        = (float)(rand_unit() * 360.0);
	api->net->send(SMS_NET_ALL, &c);
	snprintf(kind, sizeof kind, "@%d", pickups[k].id);
	c       = cmd(SMS_CMD_OBJECT);
	c.flags = SMS_CMD_STICKY;
	c.i[1]  = (int32_t)0xFFFFFFFFu;
	c.i[3]  = SMS_OBJECT_SCALE;
	c.text  = kind;
	c.f[0] = c.f[1] = c.f[2] = 0.6f; /* a capsule, not the whole bottle */
	api->net->send(SMS_NET_ALL, &c);
	if (markers_on()) {
		c       = cmd(SMS_CMD_MARKER);
		c.flags = SMS_CMD_STICKY;
		c.i[0]  = k;
		c.i[1]  = -1;
		c.i[2]  = (int32_t)SMS_RGB(70, 170, 255);
		c.f[0] = pickups[k].at.x, c.f[1] = pickups[k].at.y + 140.0f, c.f[2] = pickups[k].at.z;
		c.text  = "WATER";
		api->net->send(SMS_NET_ALL, &c);
	}
}

/* Taken, gone untaken, or the round over: gone from every game, and
   forgotten for those arriving later. */
static void pickup_hide(int k)
{
	char kind[16];
	SmsCmd c;
	pickups[k].up = 0;
	snprintf(kind, sizeof kind, "@%d", pickups[k].id);
	c       = cmd(SMS_CMD_SPAWN);
	c.flags = SMS_CMD_UNSTICK;
	c.i[3]  = pickups[k].id;
	api->net->send(SMS_NET_ALL, &c);
	c       = cmd(SMS_CMD_OBJECT);
	c.flags = SMS_CMD_UNSTICK; /* its scale */
	c.i[1]  = (int32_t)0xFFFFFFFFu;
	c.i[3]  = SMS_OBJECT_SCALE;
	c.text  = kind;
	api->net->send(SMS_NET_ALL, &c);
	c       = cmd(SMS_CMD_OBJECT);
	c.i[1]  = (int32_t)0xFFFFFFFFu;
	c.i[3]  = SMS_OBJECT_KILL;
	c.text  = kind;
	api->net->send(SMS_NET_ALL, &c);
	if (markers_on()) {
		c       = cmd(SMS_CMD_MARKER);
		c.flags = SMS_CMD_STICKY;
		c.i[0]  = k;
		c.i[1]  = -1;
		c.i[3]  = 1;
		c.text  = "";
		api->net->send(SMS_NET_ALL, &c);
	}
}

static void pickups_clear(void)
{
	int k;
	for (k = 0; k < MOST_PICKUPS; k++)
		if (pickups[k].up)
			pickup_hide(k);
}

/* Whoever reaches a capsule first takes its water; it comes back elsewhere
   after its cooldown. One nobody takes goes after a while and is at once
   somewhere else, so they don't sit where nobody goes. (Players' games up to
   the stable game of 2026.10.26 drop a spawned bottle on their own after
   five seconds, flashing, as a dropped item; later ones keep it until it is
   killed. Each capsule is a new spawn, and a stage takes 128 of them.) */
static void pickups_tick(double t)
{
	int k, slot;
	SmsNetPlayer p;
	char text[48];
	const int count = pickup_count();
	for (k = 0; k < count; k++) {
		if (!pickups[k].up) {
			if (t >= pickups[k].back_at)
				pickup_show(k, t);
			continue;
		}
		if (t >= pickups[k].gone_at) {
			pickup_hide(k);
			pickups[k].back_at = t + 1.0;
			continue;
		}
		for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
			float dx, dy, dz;
			if (!fighting[slot] || !player(slot, &p) || !(p.flags & SMS_NETP_VISIBLE))
				continue;
			dx = p.position.x - pickups[k].at.x;
			dy = p.position.y - pickups[k].at.y;
			dz = p.position.z - pickups[k].at.z;
			if (dx * dx + dz * dz > 150.0f * 150.0f || dy < -150.0f || dy > 250.0f)
				continue;
			player_cmd(slot, SMS_PLAYER_ADD_WATER, pickup_water());
			one(SMS_NET_SLOT(slot), SMS_CMD_SOUND, SOUND_WATER, 0);
			snprintf(text, sizeof text, "+%d%% water", pickup_water());
			notice(SMS_NET_SLOT(slot), text);
			pickup_hide(k);
			pickups[k].back_at = t + pickup_cooldown();
			break;
		}
	}
}

/* ------------------------------------------------------------------ phases */

static void probe_status(void)
{
	char text[64], name[24];
	switch (phase) {
	case LOBBY:
		snprintf(text, sizeof text, "Splash Battle: waiting for players (%d of %d)", count(here()), min_players());
		break;
	case COUNTDOWN: snprintf(text, sizeof text, "Splash Battle: about to start"); break;
	case BATTLE: snprintf(text, sizeof text, "Splash Battle: %d standing", count(standing())); break;
	case WINNER:
		if (winner >= 0)
			snprintf(text, sizeof text, "Splash Battle: %.23s won", name_of(winner, name, sizeof name));
		else
			snprintf(text, sizeof text, "Splash Battle: a draw");
		break;
	default: snprintf(text, sizeof text, "Splash Battle: next round soon"); break;
	}
	api->net->set_status(text);
}

static void enter(enum Phase next, double seconds)
{
	phase = next;
	ends  = now() + seconds;
	api->core->log(self, SMS_LOG_INFO, "round %d: %s (%d here, %d standing)", round_no, kPhaseName[next],
	               count(here()), count(standing()));
	probe_status();
}

static void lobby(void)
{
	int slot;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
		fighting[slot] = in_round[slot] = soaked[slot] = place_of[slot] = 0;
		watching[slot] = 0;
		meter[slot]    = full_meter();
	}
	lined_up    = 0;
	last_second = -1;
	winner      = -1;
	last_here   = 0xFFFFFFFFu;
	timer(SMS_NET_ALL, SMS_TIMER_HIDE, 0, 0);
	banner(SMS_NET_ALL, 0, "");
	one(SMS_NET_ALL, SMS_CMD_HOLD, 0, 0);
	one(SMS_NET_ALL, SMS_CMD_WATCH, -1, 0);
	rules(SMS_RULE_NO_DAMAGE, 0); /* water hits nobody yet */
	enter(LOBBY, 0);
}

/* Their place on the ring round the middle: by their order among those
   starting, evenly round. */
static void ring_spot(int k, int n, SmsVec3* at, float* yaw)
{
	const float a = 2.0f * PI_F * (float)k / (float)(n > 0 ? n : 1);
	SmsGround g;
	at->x = middle.x + RING_RADIUS * sinf(a);
	at->z = middle.z + RING_RADIUS * cosf(a);
	at->y = middle.y + 400.0f;
	memset(&g, 0, sizeof g);
	g.size = sizeof g;
	if (SMS_API_HAS(api->game, ground) && api->game->ground(at, &g) && g.floor > middle.y - 500.0f
	    && !(g.has_water && g.water_y > g.floor + 60.0f))
		at->y = g.floor;
	else /* no floor there (a wall, the sea): nearer the middle */
		at->x = middle.x + RING_RADIUS * 0.4f * sinf(a), at->z = middle.z + RING_RADIUS * 0.4f * cosf(a),
		at->y = middle.y;
	/* facing the middle (SMS_CMD_WARP's degrees: 0 faces +z) */
	*yaw = (float)(atan2(middle.x - at->x, middle.z - at->z) * 180.0 / PI_F);
}

static void line_up(double left)
{
	const uint32_t to_go = (uint32_t)(left * 1000.0);
	const uint32_t bits  = here();
	const int n          = count(bits);
	int slot, k = 0;
	lined_up = 1;
	banner(SMS_NET_ALL, 0, "");
	arena(zone_start(), 0); /* the arena's edge now: before anyone is put inside it */
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
		SmsCmd c = cmd(SMS_CMD_WARP);
		SmsVec3 at;
		float yaw;
		if (!(bits & SMS_NET_SLOT(slot)))
			continue;
		ring_spot(k++, n, &at, &yaw);
		c.f[0] = at.x, c.f[1] = at.y, c.f[2] = at.z, c.f[3] = yaw;
		api->net->send(SMS_NET_SLOT(slot), &c);
		one(SMS_NET_SLOT(slot), SMS_CMD_HOLD, 1, 0);
		in_round[slot] = 1;
		meter[slot]    = full_meter();
		player_cmd(slot, SMS_PLAYER_WATER_USE, water_use());
		health_shows(slot);
	}
	/* "Go!": FLUDD on everyone's back, the spray nozzle, a little water */
	{
		SmsCmd c   = cmd(SMS_CMD_PLAYER);
		c.flags    = SMS_CMD_STICKY;
		c.delay_ms = to_go;
		c.i[0]     = SMS_PLAYER_FLUDD;
		c.i[1]     = 1;
		api->net->send(SMS_NET_ALL, &c);
		c.flags = 0;
		c.i[0]  = SMS_PLAYER_NOZZLE;
		c.i[1]  = 0;
		api->net->send(bits, &c);
		c.i[0] = SMS_PLAYER_WATER;
		c.i[1] = start_water();
		api->net->send(bits, &c);
		hud(0, to_go); /* FLUDD's water gauge back */
	}
	one(SMS_NET_ALL, SMS_CMD_READY_GO, (int32_t)to_go, 0);
	one(SMS_NET_ALL, SMS_CMD_HOLD, 0, to_go);
	/* from "Go!": water hits, and FLUDD fills up only from the pickups */
	rules(SMS_RULE_NO_DAMAGE | SMS_RULE_WATER_HITS | (refill_on() ? 0u : SMS_RULE_NO_REFILL), to_go);
	timer(SMS_NET_ALL, SMS_TIMER_DOWN, limit_s(), to_go);
	one(SMS_NET_ALL, SMS_CMD_SOUND, SOUND_START, to_go);
	one(SMS_NET_ALL, SMS_CMD_MUSIC, MUSIC_BATTLE, to_go);
}

static void round_over(int slot)
{
	char text[96], name[24];
	int other;
	SmsNetPlayer p;
	winner = slot;
	rules(SMS_RULE_NO_DAMAGE, 0);
	timer(SMS_NET_ALL, SMS_TIMER_HIDE, 0, 0);
	one(SMS_NET_ALL, SMS_CMD_MUSIC, SMS_MUSIC_FANFARE_RACE, 0);
	if (slot >= 0) {
		place_of[slot] = 1;
		snprintf(text, sizeof text, "%s WINS!", name_of(slot, name, sizeof name));
		banner(SMS_NET_ALL & ~SMS_NET_SLOT(slot), color_of(slot), text);
		banner(SMS_NET_SLOT(slot), color_of(slot), "YOU WIN!");
		one(SMS_NET_SLOT(slot), SMS_CMD_HOLD, 1, 0);
		for (other = 0; other < SMS_NET_MAX_PLAYERS; other++)
			if (other != slot && player(other, &p) && !p.server)
				one(SMS_NET_SLOT(other), SMS_CMD_WATCH, slot, 0);
		api->core->log(self, SMS_LOG_INFO, "round %d won by %s (%d soaked)", round_no, name, soaked[slot]);
	} else {
		banner(SMS_NET_ALL, SMS_RGB(255, 255, 255), "A DRAW!");
		one(SMS_NET_ALL, SMS_CMD_HOLD, 1, 0);
	}
	pickups_clear();
	{
		SmsCmd c = cmd(SMS_CMD_PLAYER); /* FLUDD as it was */
		c.i[0]   = SMS_PLAYER_WATER_USE;
		c.i[1]   = 100;
		api->net->send(SMS_NET_ALL, &c);
	}
	board();
	enter(WINNER, show_s());
}

/* Who wins now, if the round is over: the last one standing (-1 when nobody
   is left); -2 while it goes on. */
static int decided(void)
{
	const uint32_t bits = standing();
	int slot;
	if (count(bits) > 1)
		return -2;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (bits & SMS_NET_SLOT(slot))
			return slot;
	return -1;
}

/* Time's up: the fullest meter wins. */
static int fullest(void)
{
	int slot, best = -1, tie = 0;
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
		if (!fighting[slot])
			continue;
		if (best < 0 || meter[slot] > meter[best]) {
			best = slot;
			tie  = 0;
		} else if (meter[slot] == meter[best]) {
			tie = 1;
		}
	}
	return tie ? -1 : best;
}

/* Out of the round: their meter is empty (or they fell out of the arena). */
static void put_out(int slot, int by, const char* how)
{
	char text[96], a[24], b[24];
	int left;
	if (!fighting[slot])
		return;
	fighting[slot] = 0;
	down_at[slot]  = now();
	down_by[slot]  = by;
	left           = count(standing());
	place_of[slot] = left + 1;
	if (by >= 0 && by != slot) {
		soaked[by]++;
		snprintf(text, sizeof text, "%s soaked %s! %d left", name_of(by, a, sizeof a), name_of(slot, b, sizeof b), left);
	} else {
		snprintf(text, sizeof text, "%s %s! %d left", name_of(slot, b, sizeof b), how, left);
	}
	notice(SMS_NET_ALL, text);
	one(SMS_NET_SLOT(slot), SMS_CMD_SOUND, SOUND_OUT, 0);
	one(SMS_NET_SLOT(slot), SMS_CMD_RUMBLE, 1, 0);
	api->core->log(self, SMS_LOG_INFO, "%s", text);
	board();
	probe_status();
}

/* Someone to watch for a player out: whoever put them out, while they
   stand, else anyone standing. */
static int to_watch(int slot)
{
	int other;
	if (down_by[slot] >= 0 && fighting[down_by[slot]])
		return down_by[slot];
	for (other = 0; other < SMS_NET_MAX_PLAYERS; other++)
		if (fighting[other])
			return other;
	return -1;
}


/* ------------------------------------------------------------------ each frame */

static void battle(double t)
{
	int slot;
	const uint32_t bits = here();
	/* gone from the arena (left, or loading after a fall): out */
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (fighting[slot] && !(bits & SMS_NET_SLOT(slot)))
			put_out(slot, -1, "left the arena");
	/* down a while: watching someone standing */
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (in_round[slot] && !fighting[slot] && (bits & SMS_NET_SLOT(slot)) && t - down_at[slot] > 2.5) {
			const int w = to_watch(slot);
			if (w >= 0 && w != watching[slot] - 1) {
				watching[slot] = w + 1;
				one(SMS_NET_SLOT(slot), SMS_CMD_WATCH, w, 0);
			}
		}
	pickups_tick(t);
	zone_tick(t);
	for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
		if (fighting[slot] && meter[slot] <= 0)
			put_out(slot, down_by[slot], "was washed out");
	if (t >= next_board) {
		next_board = t + 1.0;
		board();
	}
	slot = decided();
	if (slot != -2) {
		if (decided_at <= 0)
			decided_at = t;
		if (t - decided_at >= 2.0)
			round_over(slot);
	} else if (t >= ends) {
		notice(SMS_NET_ALL, "Time's up: the fullest meter wins");
		round_over(fullest());
	}
}

static void frame(void)
{
	const double t      = now();
	const uint32_t bits = here();
	char text[96];
	SmsGameState st;
	SmsNetPlayer p;
	int slot;
	if (api->net->role() != SMS_NET_HOST)
		return;
	if (!set_up) {
		set_up = 1;
		api->net->set_mode("splash-battle");
		api->net->load_stage(arena_area(), arena_episode());
		probe_status();
		api->core->log(self, SMS_LOG_INFO, "hosting the Splash Battle in area %d: %d players start a round",
		               arena_area(), min_players());
	}
	memset(&st, 0, sizeof st);
	st.size = sizeof st;
	api->game->state(&st);
	if (st.area != arena_area() || !st.stage_ready)
		return;
	if (api->game->lives() < 5)
		api->game->flag_set(LIVES_FLAG, 9);
	/* the arena's middle: the setting's place on the ground, or (0, 0) where
	   the stage puts its players (the first one in) */
	if (!have_middle && (middle_x() || middle_z())) {
		SmsGround g;
		memset(&g, 0, sizeof g);
		g.size   = sizeof g;
		middle.x = (float)middle_x();
		middle.z = (float)middle_z();
		middle.y = 3000.0f;
		if (api->game->ground(&middle, &g)) {
			middle.y    = g.floor;
			have_middle = 1;
		}
	}
	if (!have_middle && !middle_x() && !middle_z())
		for (slot = 0; slot < SMS_NET_MAX_PLAYERS && !have_middle; slot++)
			if ((bits & SMS_NET_SLOT(slot)) && player(slot, &p) && (p.flags & SMS_NETP_VISIBLE)) {
				middle      = p.position;
				have_middle = 1;
			}
	if (have_middle && !edge_out) {
		SmsGround g;
		edge_out = 1;
		memset(&g, 0, sizeof g);
		g.size     = sizeof g;
		lobby_at.x = lobby_x() || lobby_z() ? (float)lobby_x() : middle.x;
		lobby_at.z = lobby_x() || lobby_z() ? (float)lobby_z() : middle.z;
		lobby_at.y = 3000.0f;
		lobby_at.y = api->game->ground(&lobby_at, &g) ? g.floor : middle.y;
		if (phase == LOBBY || phase == COUNTDOWN)
			lobby_edge();
		api->core->log(self, SMS_LOG_INFO, "the arena's middle: %.0f %.0f %.0f; the lobby's: %.0f %.0f %.0f", middle.x,
		               middle.y, middle.z, lobby_at.x, lobby_at.y, lobby_at.z);
	}
	if (bits != last_here && (phase == LOBBY || phase == COUNTDOWN)) {
		last_here = bits;
		if (phase == LOBBY)
			snprintf(text, sizeof text, "Splash Battle: waiting for players (%d of %d here)", count(bits), min_players());
		else
			snprintf(text, sizeof text, "%d in the battle: spray the others, last one dry wins", count(bits));
		status(text);
		probe_status();
	}
	switch (phase) {
	case LOBBY:
		if (count(bits) >= min_players() && have_middle) {
			round_no++;
			timer(SMS_NET_ALL, SMS_TIMER_DOWN, countdown_s(), 0);
			notice(SMS_NET_ALL, "Splash Battle! Spray the others with FLUDD: last one standing wins");
			enter(COUNTDOWN, countdown_s());
			last_here = 0xFFFFFFFFu;
		}
		break;
	case COUNTDOWN: {
		const double left = ends - t;
		if (count(bits) < min_players() && !lined_up) {
			lobby();
			break;
		}
		if (left > kLineUp && (int)ceil(left) != last_second) {
			last_second = (int)ceil(left);
			snprintf(text, sizeof text, "SPLASH BATTLE IN %d", last_second);
			banner(SMS_NET_ALL, SMS_RGB(90, 180, 255), text);
		}
		if (left <= kLineUp && !lined_up)
			line_up(left > 0 ? left : 0);
		if (left <= 0) {
			started = ends;
			for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++) {
				fighting[slot] = in_round[slot];
				down_by[slot]  = -1;
			}
			decided_at   = 0;
			next_board   = t + 1.0;
			zone_closing = 0;
			pickup_spin = (float)rand_unit() * 2.0f * PI_F;
			for (slot = 0; slot < MOST_PICKUPS; slot++)
				pickups[slot].back_at = t; /* out at once */
			status("");
			enter(BATTLE, limit_s());
			board();
			for (slot = 0; slot < SMS_NET_MAX_PLAYERS; slot++)
				if (fighting[slot] && player(slot, &p))
					splash(slot, &p.position, SMS_SPLASH_QUIET); /* everyone's meter, full */
			if (count(standing()) < 2) { /* someone left as it started */
				round_over(decided() >= 0 ? decided() : -1);
				break;
			}
		}
		break;
	}
	case BATTLE: battle(t); break;
	case WINNER:
		if ((int)ceil(ends - t) != last_second && ends - t > 0) {
			last_second = (int)ceil(ends - t);
			snprintf(text, sizeof text, "Next round in %d", last_second);
			status(text);
		}
		if (t >= ends) {
			enter(RESTART, 0);
			restart_asked = t;
			api->net->load_stage(arena_area(), arena_episode());
		}
		break;
	case RESTART:
		if (t - restart_asked > 15.0) {
			restart_asked = t;
			api->net->load_stage(arena_area(), arena_episode());
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

/* A hit: off their meter, Shadow Mario's splash for everyone. */
static int on_splashed(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetSplash* s = (const SmsEvNetSplash*)ev->data;
	(void)mod;
	(void)user;
	if (phase != BATTLE || s->slot < 0 || s->slot >= SMS_NET_MAX_PLAYERS || s->by < 0 || s->by >= SMS_NET_MAX_PLAYERS
	    || !fighting[s->slot] || !fighting[s->by])
		return SMS_CONTINUE;
	meter[s->slot] -= s->drops;
	if (meter[s->slot] <= 0) {
		meter[s->slot]   = 0;
		down_by[s->slot] = s->by;
	}
	splash(s->slot, &s->position, meter[s->slot] <= 0 ? SMS_SPLASH_DOWN : 0);
	health_shows(s->slot);
	if (meter[s->slot] <= 0)
		put_out(s->slot, s->by, "was washed out");
	return SMS_CONTINUE;
}

/* A new session: the arena afresh (the lobby), or somewhere else (back to
   the arena). */
static int on_session(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetSession* s = (const SmsEvNetSession*)ev->data;
	(void)mod;
	(void)user;
	if (s->area != arena_area()) {
		api->net->load_stage(arena_area(), arena_episode());
		return SMS_CONTINUE;
	}
	/* a fight's arena: the stage's own fights out of the way, its Shines
	   too (a Shine would take everyone out), and nobody leaves */
	{
		SmsCmd c = cmd(SMS_CMD_ACTORS);
		c.flags  = SMS_CMD_STICKY;
		c.i[0]   = ENEMY_FIRST;
		c.i[1]   = ENEMY_LAST;
		c.i[2]   = SMS_ACTORS_HIDE;
		api->net->send(SMS_NET_ALL, &c);
		c.i[0] = c.i[1] = SHINE_TYPE;
		c.i[2]          = SMS_ACTORS_KILL;
		api->net->send(SMS_NET_ALL, &c);
		c      = cmd(SMS_CMD_STAY);
		c.flags = SMS_CMD_STICKY;
		c.i[0]  = 1;
		api->net->send(SMS_NET_ALL, &c);
		/* the HUD shows what a fight needs: the health (the water meter) and,
		   once FLUDD is on, its water; not the Shines, coins, blue or red
		   coins and lives */
		hud(SMS_HUD_WATER, 0);
	}
	if (SMS_API_HAS(api->net, set_collision))
		api->net->set_collision(collide_on() ? 1 : -1);
	have_middle  = 0; /* where this load starts its players */
	edge_out     = 0;
	zone_closing  = 0;
	next_spawn_id = 1;
	memset(pickups, 0, sizeof pickups); /* the last session's bottles and markers went with it */
	board_clear();
	lobby();
	return SMS_CONTINUE;
}

/* A player in the arena: one arriving during a battle (just joined, or back
   after falling out) waits for the next round, watching. */
static int on_arrived(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetPlayer* p = (const SmsEvNetPlayer*)ev->data;
	const double t          = now();
	int w;
	(void)mod;
	(void)user;
	api->core->log(self, SMS_LOG_INFO, "%s (slot %d) in the arena (%s)", p->name, p->slot, kPhaseName[phase]);
	stage_music_off(SMS_NET_SLOT(p->slot));
	switch (phase) {
	case COUNTDOWN:
		timer(SMS_NET_SLOT(p->slot), SMS_TIMER_DOWN, ends - t > 0 ? ends - t : 0, 0);
		break;
	case BATTLE:
		if (fighting[p->slot])
			put_out(p->slot, -1, "fell out of the arena");
		notice(SMS_NET_SLOT(p->slot), "A battle is on: you are in the next round");
		w = to_watch(p->slot);
		if (w >= 0)
			one(SMS_NET_SLOT(p->slot), SMS_CMD_WATCH, w, 0);
		timer(SMS_NET_SLOT(p->slot), SMS_TIMER_DOWN, ends - t > 0 ? ends - t : 0, 0);
		break;
	default:
		break;
	}
	return SMS_CONTINUE;
}

static int on_left(SmsMod* mod, SmsEvent* ev, void* user)
{
	const SmsEvNetPlayer* p = (const SmsEvNetPlayer*)ev->data;
	(void)mod;
	(void)user;
	if (phase == BATTLE && p->slot >= 0 && p->slot < SMS_NET_MAX_PLAYERS)
		put_out(p->slot, -1, "left");
	if (p->slot >= 0 && p->slot < SMS_NET_MAX_PLAYERS)
		in_round[p->slot] = 0;
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
	snprintf(text, sizeof text, "Welcome to the Splash Battle, %s! Spray the others, last one dry wins", p->name);
	notice(SMS_NET_SLOT(p->slot), text);
	return SMS_CONTINUE;
}

static void add_option(int type, const char* key, const char* label, int def, int min, int max, int step)
{
	SmsOption o;
	memset(&o, 0, sizeof o);
	o.size  = sizeof o;
	o.key   = key;
	o.label = label;
	o.type  = type;
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
	if (!SMS_API_HAS(api, net) || !SMS_API_HAS(api->net, set_collision) || !SMS_API_HAS(api->game, ground)
	    || (api->major == 1 && api->minor == 5 && api->size >= sizeof *api && api->prerelease && api->prerelease < 7)) {
		api->core->log(mod, SMS_LOG_ERROR, "needs a game with mod API 1.5 (water fights, arenas, water pickups)");
		return 1;
	}
	add_option(SMS_OPTION_INT, "min_players", "Players a round needs", 2, 2, 8, 1);
	add_option(SMS_OPTION_INT, "countdown", "Countdown (seconds)", 15, 5, 60, 5);
	add_option(SMS_OPTION_INT, "meter", "Water meter (drops)", 300, 50, 2000, 50);
	add_option(SMS_OPTION_INT, "time_limit", "Time limit (seconds)", 180, 60, 600, 30);
	add_option(SMS_OPTION_INT, "winner_seconds", "Winner shown (seconds)", 10, 3, 30, 1);
	add_option(SMS_OPTION_INT, "zone_radius", "Arena's edge (units from the middle)", 3600, 1000, 8000, 200);
	add_option(SMS_OPTION_INT, "middle_x", "Arena's middle, x (0, 0: the stage's start)", 1800, -30000, 30000, 100);
	add_option(SMS_OPTION_INT, "middle_z", "Arena's middle, z", 2800, -30000, 30000, 100);
	add_option(SMS_OPTION_BOOL, "zone", "The arena closes in", 1, 0, 1, 1);
	add_option(SMS_OPTION_INT, "zone_end", "Closed to (units)", 600, 200, 3000, 100);
	add_option(SMS_OPTION_BOOL, "refill", "FLUDD fills up by itself", 0, 0, 1, 1);
	add_option(SMS_OPTION_INT, "start_water", "Water at the start (%)", 19, 0, 100, 1);
	add_option(SMS_OPTION_INT, "water_use", "Water use (% of FLUDD's own)", 300, 50, 600, 25);
	add_option(SMS_OPTION_INT, "pickups", "Water capsules", 8, 0, MOST_PICKUPS, 1);
	add_option(SMS_OPTION_INT, "pickup_water", "Water a capsule gives (%)", 12, 1, 100, 1);
	add_option(SMS_OPTION_INT, "pickup_cooldown", "Taken capsule back after (seconds)", 30, 5, 180, 5);
	add_option(SMS_OPTION_INT, "pickup_life", "Untaken capsule moves after (seconds)", PICKUP_LIFE, 5, 120, 1);
	add_option(SMS_OPTION_BOOL, "pickup_markers", "WATER markers over the pickups", 0, 0, 1, 1);
	add_option(SMS_OPTION_INT, "lobby_x", "Lobby's middle, x (0, 0: where the stage starts)", 10600, -30000, 30000, 100);
	add_option(SMS_OPTION_INT, "lobby_z", "Lobby's middle, z", -3600, -30000, 30000, 100);
	add_option(SMS_OPTION_INT, "lobby_radius", "Lobby's edge (units)", 850, 200, 4000, 50);
	add_option(SMS_OPTION_BOOL, "collision", "Players collide", 1, 0, 1, 1);
	add_option(SMS_OPTION_INT, "area", "Arena (area)", 4, 1, 60, 1);
	add_option(SMS_OPTION_BOOL, "stage_music", "Gelato Beach's music", 0, 0, 1, 1);
	add_option(SMS_OPTION_INT, "episode", "Arena (episode)", 0, 0, 7, 1);
	api->core->subscribe(mod, SMS_EV_FRAME, on_frame, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_SESSION, on_session, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_ARRIVED, on_arrived, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_JOINED, on_joined, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_LEFT, on_left, NULL, SMS_PRIORITY_NORMAL);
	api->core->subscribe(mod, SMS_EV_NET_PLAYER_SPLASHED, on_splashed, NULL, SMS_PRIORITY_NORMAL);
	return 0;
}
