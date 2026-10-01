cmake_minimum_required(VERSION 3.25)
project(Minicraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 is downloaded and built as part of the project on first configure.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui (debug UI). It has no CMake build of its own, so compile the sources we need here.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_executable(Minicraft
    client/main.cpp
    client/player.cpp
    client/tile_map.cpp
    client/camera.cpp
    client/debug_overlay.cpp
    client/effects.cpp
    client/texture.cpp
    client/font.cpp
    client/items.cpp
    client/dropped_items.cpp
    client/hud.cpp
    client/inventory_menu.cpp
    client/recipe.cpp
    client/crafting_menu.cpp
    client/furniture.cpp
    client/world_gen.cpp
    client/mob.cpp
    client/mobs.cpp
    client/day_night.cpp
    client/world_save.cpp
    client/game_menu.cpp)
target_link_libraries(Minicraft PRIVATE SDL3::SDL3 imgui)

# Copy assets next to the executable; the game finds them via SDL_GetBasePath().
add_custom_command(TARGET Minicraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Minicraft>/assets)
