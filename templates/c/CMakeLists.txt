# Builds this mod for the Super Mario Sunshine PC port:
#
#   cmake -S . -B build -DSMS_MODS_OUT=/path/to/the/game/mods   # -DSMS_MOD_ARCH=32 for the 32-bit Linux game
#   cmake --build build
#
# The mod is then in <SMS_MODS_OUT>/<id>/ (the id in mod/mod.ini), as the game
# loads it; without SMS_MODS_OUT, in build/mods/.
cmake_minimum_required(VERSION 3.16)
project(my_c_mod C)

# The SDK, at the mod API version this mod is built against (its releases:
# https://github.com/chasem-dev/sms-mod-sdk/releases). A checkout of it
# instead: -DFETCHCONTENT_SOURCE_DIR_SMS_MOD_SDK=<path>
include(FetchContent)
FetchContent_Declare(sms_mod_sdk
  GIT_REPOSITORY https://github.com/chasem-dev/sms-mod-sdk.git
  GIT_TAG v1.1.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(sms_mod_sdk)

# mod/ is what ships (mod.ini, and images or data the mod reads); src/ is built
sms_add_mod(mod src/my_mod.c)
set_target_properties(${SMS_MOD_TARGET} PROPERTIES C_STANDARD 99)
if(MSVC)
  target_compile_options(${SMS_MOD_TARGET} PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(${SMS_MOD_TARGET} PRIVATE -Wall -Wextra -O2)
endif()
if(NOT WIN32)
  target_link_libraries(${SMS_MOD_TARGET} PRIVATE m) # sqrtf
endif()
