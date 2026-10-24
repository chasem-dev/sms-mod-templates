# Splash Battle (C, server mod)

A game mode for the port's multiplayer: a water fight. Everyone who joins is in the arena (Gelato Beach) with FLUDD, and FLUDD's water hits the other players. Whoever is hit reacts as Shadow Mario does when Mario sprays him in his chases: his splash and hit sound, his flush, and his water meter over their head. Empty it and they go down as he does, out of the round. The last one standing wins.

It is a **server mod** (`side = server` in its `mod.ini`): it runs on the host of the game, a dedicated server as a rule, and **players join with the plain game**, nothing installed. The mod keeps the meters and decides who is out; each player's game shows it all with its own models, sounds and HUD.

![Bo's game: Luigi (Bo) flushed pink in Ana's spray, Shadow Mario's water splash on him, his meter over his head at 39%, Ana's full; the board at the left, the round's clock at the bottom](docs/splash.jpg)

*Bo's game in the middle of a round: Ana's water hits him (Shadow Mario's splash on his back), his model flushed as Shadow Mario's is, his water meter over his head down to 39%. The board at the left keeps the standings; the HUD's health shows his meter too.*

![Luigi thrown back onto the sand, still flushed, as his meter runs out](docs/down.jpg)

*His meter empty: thrown back onto the sand, as Shadow Mario falls when his is. Two seconds later everyone watches the winner.*

![Ana's game at "GO!": both players on the ring round the arena's middle, each with a full meter over their head](docs/start.jpg)

*"GO!": everyone was put on a ring round the arena's middle, facing it, and held until now. Each player's meter shows over their head.*

## A round

| | What players see | What the server does |
| --- | --- | --- |
| **Lobby** | "waiting for players (1 of 2 here)", free to play | counts the players in the arena; the stage's enemies are hidden and its Shines gone, and nobody can leave the arena |
| **Countdown** | "SPLASH BATTLE IN 8", the clock counting down | three seconds before the start: everyone onto a ring round the arena's middle, facing it, held; full FLUDD tanks, the spray nozzle |
| **Battle** | "Ready... Go!", Shadow Mario's chase music, the clock counting down from the time limit; a board of everyone's meter | water hits the other players (`SMS_RULE_WATER_HITS`); each drop that hits takes one off the meter; FLUDD is kept full; players collide |
| **The zone** | a ring of red arrows round the arena's middle, closing in | outside it, the meter drains a few drops a second, with a splash each second and "Outside the zone: get back in!" |
| **Out** | "Ana soaked Bo! 1 left"; the one soaked is thrown back, then watches whoever soaked them | a meter at 0 is out; so is a player who falls out of the arena (a lost life) or leaves |
| **Winner** | "Ana WINS!" in their colour, the race fanfare; everyone watching the winner | the last one standing wins; at the time limit, the fullest meter (a tie is a draw) |
| **Restart** | the arena afresh | loads the arena again for everyone: a new session, the lobby |

A player who joins during a battle watches it and plays the next round.

## Settings

On the host: its Mods menu (F1), or `settings.ini` in the mod's data folder.

| | |
| --- | --- |
| **Players a round needs** (`min_players`) | 2 |
| **Countdown** (`countdown`) | 15 seconds |
| **Water meter** (`meter`) | 300 drops: about three seconds of a spray held on someone close by |
| **Time limit** (`time_limit`) | 180 seconds |
| **Winner shown** (`winner_seconds`) | 10 seconds |
| **Closing zone** (`zone`), from and to (`zone_radius`, `zone_end`), drain (`zone_drain`) | on, 3000 to 600 units, 3 drops a second |
| **FLUDD kept full** (`refill`) | on |
| **Players collide** (`collision`) | on (off: the host's own Player Collision setting) |
| **Arena** (`area`, `episode`) | 4, 0: Gelato Beach. The middle is where the stage puts its players |

## Running a server

Build the mod (below), put it in a folder of mods for the server, and start the game as a dedicated server:

```sh
SMS_HEADLESS=1 SMS_AUDIO=0 SMS_TEXTURE_PACKS=0 \
SMS_MULTIPLAYER=host SMS_MULTIPLAYER_DEDICATED=1 \
SMS_NATIVE_MODS_DIR=/path/to/server-mods \
./sms GMSE01.iso
```

or put it in [sms-server](https://github.com/chasem-dev/sms-server)'s `mods/`. It goes straight from the title to the arena; players join it as any game. A player's own game can host it too (Host Game with the mod installed): it plays along.

Players' games need mod API 1.5 beta 5 or later (`client_api = 1.5`; an older 1.5 beta takes no water hits).

## Building

```sh
cmake -S . -B build -DSMS_MODS_OUT=/path/to/server-mods   # -DSMS_MOD_ARCH=32 for the 32-bit Linux game
cmake --build build
```

## How it is made

The whole mod is [`src/splash_battle.c`](src/splash_battle.c): plain C against the SDK's `sms_mod.h`, API 1.5 beta 5 (water fights, docs/MODDING.md).

- **Water hits are the players' games' word:** with `SMS_RULE_WATER_HITS` each game stops its own player's drops at the other players and tells the host; the mod hears `SMS_EV_NET_PLAYER_SPLASHED` (who, by whom, how many drops) and takes them off that player's meter.
- **The reaction is every game's:** `SMS_CMD_SPLASH` to everyone, with the meter left, shows Shadow Mario's splash, sound, flush and meter on that player in every game; `SMS_SPLASH_DOWN` when it is empty throws them back. `SMS_PLAYER_HEALTH` puts the meter on their HUD's health as well.
- **Collision:** `net->set_collision(1)` for the arena's sessions, so nobody hides inside anyone and a stomp lands.
- **The arena, in every game:** `ACTORS` hides the stage's enemies and kills its Shines, `STAY` keeps everyone there, `RULES` keeps damage off; all **sticky**, so a player arriving later gets them too. The zone is eight `MARKER`s on the ground (`game->ground` finds it on the server's own copy of the stage).
- **The start, lined up across the players:** at three seconds to go it sends `WARP` and `HOLD` now, and `HOLD` off, `RULES` with water hits, `TIMER`, the start's sound and the music with a `delay_ms` of the time left; `READY_GO` gets "Go!" there too.
- **Events:** `SMS_EV_NET_SESSION` (the arena loaded: set it up, the lobby), `SMS_EV_NET_PLAYER_SPLASHED` (a hit), `SMS_EV_NET_PLAYER_ARRIVED` (someone in the arena: mid-battle, they watch), `SMS_EV_NET_PLAYER_LEFT`, `SMS_EV_NET_PLAYER_JOINED` (a welcome), and `SMS_EV_FRAME` for the rest.
