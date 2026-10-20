# sms-mod-templates

Templates to start a mod from, and complete example mods, for the [Super Mario Sunshine PC port](https://github.com/chasem-dev/sms-pc-port).
They are built with the [mod SDK](https://github.com/chasem-dev/sms-mod-sdk), whose [manual](https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md) has every event and function.

## Templates

Copy a template's folder out as a repository of your own. Each builds on its own, fetching the SDK at its version tag, and its GitHub workflow builds the mod for every platform and releases it as one zip when you push a tag.

| | |
| --- | --- |
| [`templates/cpp`](templates/cpp) | A native mod in C++ (with the SDK's `sms_mod.hpp`). |
| [`templates/c`](templates/c) | A native mod in C. |
| [`templates/lua`](templates/lua) | A Lua script mod: a `mod.ini` and a `main.lua`, nothing to build. Runs in the [lua mod](https://github.com/chasem-dev/sms-mod-sdk#the-lua-mod). |

All three start as the same small mod (Mario's speed in a panel, a key, a setting in the Mods menu), so they can be compared side by side.
In a template, `mod/` is what ships (`mod.ini` and any images or data) and `src/` is what is built.

## Examples

| | |
| --- | --- |
| [`examples/hello`](examples/hello) | C++, the smallest useful mod: names the stage, counts the coins taken in it, a key (H) and a setting. |
| [`examples/trainer-hud`](examples/trainer-hud) | C, most of the API: a speed panel and graph, a trail behind Mario and a landing ring in the world, coin pop-ups, a stage banner, a field of view setting, invincibility, moon jump (M), an image, options, keys (F3), and another mod's events. |
| [`examples/stage-timer`](examples/stage-timer) | C, made to sit beside others: a timer panel that stacks under the trainer HUD's (`after = trainer-hud`), Shine splits, damage watched at `MONITOR` priority, best times kept in its settings, and a custom event (`stage-timer.split`) other mods can hear. |
| [`examples/lua-challenges`](examples/lua-challenges) | Lua: a challenge against the clock each time a stage loads (coins, no damage, climbing, running, air time), with a panel, world markers, the game's sounds, settings, a key (R), a module of its own (`require`), and the C stage-timer's events read with `string.unpack`. |

They run together: their panels stack, their keys differ, and the Mods menu (F1) lists each with its settings.

## Building

```sh
cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods
cmake --build build
```

builds every example and template into the game's `mods/` folder; the next start of the game loads them.
Add `-DSMS_MOD_ARCH=32` for the default Linux game (`build/linux-32`), a 32-bit program. `-DFETCHCONTENT_SOURCE_DIR_SMS_MOD_SDK=<path>` builds against a checkout of the SDK instead of its tag.
The Lua ones need the lua mod installed (from the [SDK's releases](https://github.com/chasem-dev/sms-mod-sdk/releases), or a source checkout of the port's `python3 tools/mods/get.py lua`).

Prebuilt, for every platform: the zip on this repository's [releases](https://github.com/chasem-dev/sms-mod-templates/releases).

## License

[MIT](LICENSE): copy what you like into mods of your own, under any license.
