# Builds every example and template against the SDK, laid out as the game
# loads mods:
#
#   cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods   # -DSMS_MOD_ARCH=32 for the 32-bit Linux game
#   cmake --build build
#
# The SDK comes from its tag below; -DFETCHCONTENT_SOURCE_DIR_SMS_MOD_SDK=<path>
# uses a checkout of it instead. Each template also builds on its own: copy
# its folder out as a repository of your own.
cmake_minimum_required(VERSION 3.16)
project(sms_mod_templates C CXX)

include(FetchContent)
FetchContent_Declare(sms_mod_sdk
  GIT_REPOSITORY https://github.com/chasem-dev/sms-mod-sdk.git
  GIT_TAG v1.1.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sms_mod_sdk)

add_subdirectory(examples)
add_subdirectory(templates/c)
add_subdirectory(templates/cpp)
add_subdirectory(templates/lua)
