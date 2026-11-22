cmake_minimum_required(VERSION 3.16)

project(SoakTheBun VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SOAK_BUILD_GAME "Build the SDL2 game" ON)
option(SOAK_BUILD_TESTS "Build the level generator tests" ON)
option(SOAK_FETCH_SDL "Download and build SDL2 when it is not installed" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- core (no SDL)
add_library(soak_core STATIC
  src/core/level.cpp
  src/core/generator.cpp
  src/core/solver.cpp
)
target_include_directories(soak_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(soak_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------- art (no SDL)
add_library(soak_art STATIC
  src/gfx/canvas.cpp
  src/gfx/art.cpp
  src/gfx/font.cpp
)
target_include_directories(soak_art PUBLIC src)

# ---------------------------------------------------------------- tests
if(SOAK_BUILD_TESTS)
  enable_testing()
  add_executable(soak_tests tests/test_generator.cpp)
  target_link_libraries(soak_tests PRIVATE soak_core)
  add_test(NAME generator_levels_1_40 COMMAND soak_tests 40)
endif()

# ---------------------------------------------------------------- game
if(SOAK_BUILD_GAME)
  # SDL2 from the system / vcpkg / Homebrew if available, otherwise built from source.
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    if(NOT SOAK_FETCH_SDL)
      message(FATAL_ERROR "SDL2 not found. Install it (e.g. libsdl2-dev, brew install sdl2, vcpkg) "
                          "or configure with -DSOAK_FETCH_SDL=ON.")
    endif()
    message(STATUS "SDL2 not found - downloading and building it")
    include(FetchContent)
    FetchContent_Declare(SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.9
      GIT_SHALLOW TRUE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
  endif()

  if(TARGET SDL2::SDL2)
    set(SOAK_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(SOAK_SDL_TARGET SDL2::SDL2-static)
  elseif(DEFINED SDL2_LIBRARIES)
    # Very old sdl2-config.cmake files only provide variables.
    add_library(soak_sdl2 INTERFACE)
    target_include_directories(soak_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(soak_sdl2 INTERFACE ${SDL2_LIBRARIES})
    set(SOAK_SDL_TARGET soak_sdl2)
  else()
    message(FATAL_ERROR "Found SDL2 but no usable CMake target")
  endif()

  set(GAME_SOURCES
    src/app/main.cpp
    src/app/game.cpp
    src/app/audio.cpp
    src/app/save.cpp
  )

  if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    # Embed the procedurally drawn icon in the .exe.
    add_executable(soak_icongen tools/icongen.cpp)
    target_link_libraries(soak_icongen PRIVATE soak_art)
    set(ICON_FILE ${CMAKE_CURRENT_BINARY_DIR}/soakthebun.ico)
    add_custom_command(
      OUTPUT ${ICON_FILE}
      COMMAND soak_icongen ${ICON_FILE}
      DEPENDS soak_icongen
      COMMENT "Drawing application icon")
    set(RC_FILE ${CMAKE_CURRENT_BINARY_DIR}/soakthebun.rc)
    file(WRITE ${RC_FILE} "IDI_ICON1 ICON \"${ICON_FILE}\"\n")
    set_source_files_properties(${RC_FILE} PROPERTIES OBJECT_DEPENDS ${ICON_FILE})
    list(APPEND GAME_SOURCES ${RC_FILE} ${ICON_FILE})
  endif()

  add_executable(soakthebun WIN32 ${GAME_SOURCES})
  target_link_libraries(soakthebun PRIVATE soak_core soak_art)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(soakthebun PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(soakthebun PRIVATE ${SOAK_SDL_TARGET})

  # Put SDL2.dll next to the game on Windows.
  if(WIN32 AND TARGET ${SOAK_SDL_TARGET})
    get_target_property(_sdl_type ${SOAK_SDL_TARGET} TYPE)
    if(_sdl_type STREQUAL "SHARED_LIBRARY")
      add_custom_command(TARGET soakthebun POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${SOAK_SDL_TARGET}> $<TARGET_FILE_DIR:soakthebun>)
    endif()
  endif()

  include(GNUInstallDirs)
  install(TARGETS soakthebun RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(UNIX AND NOT APPLE)
    configure_file(data/soakthebun.desktop.in soakthebun.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/soakthebun.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/soakthebun.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
  endif()
endif()
