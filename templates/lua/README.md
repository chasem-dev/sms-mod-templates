# My Lua mod

A template for a Lua script mod for the [Super Mario Sunshine PC port](https://github.com/chasem-dev/sms-pc-port).
As it is, it shows Mario's speed in a panel at the bottom right while a stage is played; U hides it, and so does its switch in the Mods menu (F1).

![The Lua template as it is: Mario's speed in a panel at the bottom right while he runs](docs/screenshot.jpg)

*The template as it is, running alone in Bianco Hills. The panel takes its place with `hud_slot`, so other mods' panels there stack with it instead of overlapping. The speed is Mario's horizontal velocity in units a second.*

Script mods need nothing built: [`mod/`](mod) is the whole mod, the same on every computer.
They run in the **lua** mod, which is optional and not part of the game: players install it once from the [SDK's releases](https://github.com/chasem-dev/sms-mod-sdk/releases) (or a source checkout of the port's `python3 tools/mods/get.py lua`).

## Make it yours

1. Copy this folder out as a repository of your own (from [sms-mod-templates](https://github.com/chasem-dev/sms-mod-templates): `templates/lua`).
2. In [`mod/mod.ini`](mod/mod.ini), give it your own `id`, `name`, `author` and `description`.
3. Write the mod in [`mod/main.lua`](mod/main.lua); more files beside it load with `require`. The Lua API: [MODDING.md, "Lua scripts"](https://github.com/chasem-dev/sms-mod-sdk/blob/main/docs/MODDING.md#lua-scripts). [`examples/lua-challenges`](https://github.com/chasem-dev/sms-mod-templates/tree/main/examples/lua-challenges) is a complete script mod to borrow from.

## Try it

Copy `mod/` into the game's `mods/` folder as `mods/<id>/` (or `cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods && cmake --build build`), with the lua mod beside it.
Run the game with `SMS_NATIVE_MODS_HOT=1` and each save of a script loads it again.
Errors are in the game's log as `[mod:<id>]` lines, with the file and line.

## Release

Push a tag (`git tag v1.0.0 && git push --tags`): the workflow in [`.github/workflows/build.yml`](.github/workflows/build.yml) checks the scripts compile and releases a zip holding `<id>/`. Players unzip it into the game's `mods/` folder.
