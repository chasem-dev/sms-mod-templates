# Hello (C++)

The smallest useful native mod. As each stage loads, it says which one it is, and it counts the coins Mario picks up there. Both go in a small panel at the bottom left.

![Hello's panel at the bottom left: "Hello from BIANCO HILLS, episode 1!  Coins: 0"](docs/screenshot.jpg)

*Hello on its own in Bianco Hills, episode 1, with Mario running up the path.*

## Controls and settings

| | |
| --- | --- |
| **H** | hides and shows the panel (`key.toggle` in its settings picks another key) |
| Mods menu (F1) | **Show the greeting**: on or off |

## What it shows

It is written with the SDK's C++ layer, `sms_mod.hpp`: one class, `struct Hello : sms::Mod`, with lambdas for its handlers.

- `SMS_EV_STAGE_LOADED` resets the count and names the stage with `game().area_name()`.
- `SMS_EV_ITEM_COLLECTED` counts the coins.
- `SMS_EV_KEY`: its key is consumed (`SMS_CANCEL`), so the game's controls never see H.
- `SMS_EV_DRAW`: it draws only while a stage is played and not paused, in a place it asks for with `draw().hud_slot()`, so other mods' panels at the bottom left stack with it.
- `bindKey()` and `optionBool()` give it a key the player can change and a switch in the Mods menu.

The whole mod is [`src/hello.cpp`](src/hello.cpp), and [`mod/mod.ini`](mod/mod.ini) names it. To start a C++ mod of your own, copy [`templates/cpp`](../../templates/cpp), which has its own build and release workflow.

## Build and install

From the repository's root, which builds every example and template:

```sh
cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods   # -DSMS_MOD_ARCH=32 for the 32-bit Linux game
cmake --build build
```

It then sits in `mods/hello/`, and the next start of the game loads it. A prebuilt copy for every platform is in the zip on the repository's [releases](https://github.com/chasem-dev/sms-mod-templates/releases).

Every event and function: [MODDING.md](https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md).
