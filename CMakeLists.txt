cmake_minimum_required(VERSION 3.16)
project(llavero_de_mundos C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT MSVC)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_library(LLV_MATH m)

# Motor portable: no depende de ninguna plataforma.
add_library(llv_core STATIC
  src/core/engine.c src/core/state.c src/core/input.c src/core/gfx.c src/core/font.c
  src/core/timeutil.c src/core/rng.c
  src/core/garden.c src/core/garden_data.c src/core/pet.c src/core/art.c
  src/core/phrases.c src/core/phrases_data.c src/core/talk.c src/core/director.c src/core/habits.c
  src/core/cast.c src/core/overlays.c src/core/music.c
  src/core/worlds/reloj.c src/core/worlds/jardin.c src/core/worlds/cobertizo.c
  src/core/worlds/sala.c src/core/worlds/musica.c src/core/worlds/placeholder.c)
target_include_directories(llv_core PUBLIC src/core)
if(LLV_MATH)
  target_link_libraries(llv_core PUBLIC ${LLV_MATH})
endif()

# Salida de imágenes (PNG/PPM) para el simulador y la herramienta headless.
add_library(llv_util STATIC src/util/imgout.c src/util/wav.c src/util/musicstats.c)
target_link_libraries(llv_util PUBLIC llv_core)

# Tests (CTest).
enable_testing()
add_executable(llv_tests tests/test_main.c)
target_link_libraries(llv_tests PRIVATE llv_core)
add_test(NAME llv_tests COMMAND llv_tests)
add_executable(llv_test_music tests/test_music.c)
target_link_libraries(llv_test_music PRIVATE llv_util)
add_test(NAME llv_test_music COMMAND llv_test_music)

# Frases (spec §5): data/frases/*.txt -> src/core/phrases_data.c (se versiona ya generado).
# `cmake --build build --target phrases` lo regenera; ctest comprueba que está al día.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(phrases COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMENT "Regenerando src/core/phrases_data.c")
  add_test(NAME phrases_tool COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py --selftest)
  add_test(NAME phrases_data_al_dia COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py --check)
endif()

# Herramienta headless: sin SDL.
add_executable(llv_headless src/platform/headless/main.c)
target_link_libraries(llv_headless PRIVATE llv_util)

# Simulador SDL2, solo si hay SDL2.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
  endif()
endif()
if(TARGET SDL2::SDL2)
  set(LLV_SDL_TARGET SDL2::SDL2)
elseif(TARGET PkgConfig::SDL2)
  set(LLV_SDL_TARGET PkgConfig::SDL2)
endif()
if(LLV_SDL_TARGET)
  add_executable(llv_sim src/platform/sdl/main.c src/platform/sdl/hal_sdl.c src/hal/boot.c)
  target_link_libraries(llv_sim PRIVATE llv_util ${LLV_SDL_TARGET})
  message(STATUS "SDL2 encontrado: se construye llv_sim")
else()
  message(STATUS "SDL2 no encontrado: se omite llv_sim")
endif()
