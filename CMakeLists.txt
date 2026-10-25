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
  src/core/cast.c src/core/overlays.c src/core/music.c src/core/studio.c
  src/core/fish.c src/core/fish_data.c src/core/fish_art.c
  src/core/worlds/reloj.c src/core/worlds/jardin.c src/core/worlds/cobertizo.c
  src/core/worlds/sala.c src/core/worlds/musica.c src/core/worlds/pesca.c)
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
add_executable(llv_test_studio tests/test_studio.c)
target_link_libraries(llv_test_studio PRIVATE llv_util)
add_test(NAME llv_test_studio COMMAND llv_test_studio)
add_executable(llv_test_pesca tests/test_pesca.c)
target_link_libraries(llv_test_pesca PRIVATE llv_core)
add_test(NAME llv_test_pesca COMMAND llv_test_pesca)

# Frases (spec §5): data/frases/*.txt -> src/core/phrases_data.c (se versiona ya generado).
# `cmake --build build --target phrases` lo regenera; ctest comprueba que está al día.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(phrases COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMENT "Regenerando src/core/phrases_data.c")
  add_test(NAME phrases_tool COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py --selftest)
  add_test(NAME phrases_data_al_dia COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/phrases2bin.py --check)
endif()

# Salida de audio común (hal_audio_pump): del motor a la salida de muestras de cada plataforma.
add_library(llv_hal_audio STATIC src/hal/audio.c)
target_link_libraries(llv_hal_audio PUBLIC llv_core)

# Herramienta headless: sin SDL (su audio es de mentira: no suena, pero se puede grabar a WAV).
add_executable(llv_headless src/platform/headless/main.c src/platform/headless/hal_audio_stub.c)
target_link_libraries(llv_headless PRIVATE llv_util llv_hal_audio)

# La alfa (fase 5): 14 días de uso realista por los 6 mundos (con una lombriz extra al día para ejercitar la
# pesca), con ausencias, guardado a mitad y comprobación de invariantes. Falla si alguna se rompe.
add_test(NAME alfa_14_dias COMMAND llv_headless --date "2026-10-22 08:00" --seed 7 --out ${CMAKE_BINARY_DIR}
         alpha 14 ${CMAKE_BINARY_DIR}/informe_alfa.txt stock)
add_test(NAME alfa_14_dias_invierno COMMAND llv_headless --date "2027-02-03 08:00" --seed 21 --out ${CMAKE_BINARY_DIR}
         alpha 14 ${CMAKE_BINARY_DIR}/informe_alfa_invierno.txt)

# Banco de pruebas de la música: renderiza semillas a WAV, mide y juzga (make music).
add_executable(llv_music src/platform/headless/music_tool.c)
target_link_libraries(llv_music PRIVATE llv_util)
add_custom_target(music COMMAND llv_music --seeds 200 --out ${CMAKE_BINARY_DIR}/musica --baseline --sweep 65536
                  COMMAND llv_music --matrix 100 --examples --first 1000 --out ${CMAKE_BINARY_DIR}/musica
                          --report informe_estudio.txt
                  COMMAND_EXPAND_LISTS COMMENT "Renderizando semillas y midiendo la música (y las combinaciones del Estudio)")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/musica)

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
  target_link_libraries(llv_sim PRIVATE llv_util llv_hal_audio ${LLV_SDL_TARGET})
  message(STATUS "SDL2 encontrado: se construye llv_sim")
else()
  message(STATUS "SDL2 no encontrado: se omite llv_sim")
endif()
