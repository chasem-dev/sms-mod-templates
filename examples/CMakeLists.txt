# The examples (built from the top-level CMakeLists.txt, which gets the SDK).
# Each is laid out as a template is: mod/ ships, src/ is built.
sms_add_mod(hello/mod hello/src/hello.cpp)
sms_add_mod(trainer-hud/mod trainer-hud/src/trainer_hud.c)
sms_add_mod(stage-timer/mod stage-timer/src/stage_timer.c)
sms_add_mod(squid-race/mod squid-race/src/squid_race.c)
if(NOT WIN32)
  target_link_libraries(smsmod_trainer_hud PRIVATE m)
  target_link_libraries(smsmod_squid_race PRIVATE m)
endif()
foreach(t smsmod_hello smsmod_trainer_hud smsmod_stage_timer smsmod_squid_race)
  set_target_properties(${t} PROPERTIES C_STANDARD 99 CXX_STANDARD 11)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -O2)
  endif()
endforeach()

# Lua script mods: nothing to build
sms_add_script_mod(lua-challenges/mod)
