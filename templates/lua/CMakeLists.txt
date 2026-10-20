# A script mod has nothing to build: mod/ is the whole mod. This only copies
# it to its place, for those who install mods with CMake:
#
#   cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods
#   cmake --build build
#
# (or copy mod/ to the game's mods/ folder as mods/<id>/ yourself).
cmake_minimum_required(VERSION 3.16)
project(my_lua_mod NONE)

include(FetchContent)
FetchContent_Declare(sms_mod_sdk
  GIT_REPOSITORY https://github.com/chasem-dev/sms-mod-sdk.git
  GIT_TAG v1.1.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sms_mod_sdk)

sms_add_script_mod(mod)
