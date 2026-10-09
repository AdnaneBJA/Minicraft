cmake_minimum_required(VERSION 3.25)
project(Minicraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# What to build. The server's Docker image builds without the game (no SDL); the web build has no tests.
option(MINICRAFT_BUILD_CLIENT "Build the game (downloads and builds SDL3 and Dear ImGui)" ON)
if(EMSCRIPTEN)
    set(MINICRAFT_BUILD_TESTS OFF)
else()
    option(MINICRAFT_BUILD_TESTS "Build the unit and server tests (GoogleTest)" ON)
endif()

# MinGW: put the C++ runtime inside the executables, so they start from Explorer or any terminal without MinGW's
# DLLs on the PATH.
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

include(FetchContent)

# SDL3 and Dear ImGui (the debug UI), downloaded and built as part of the project on first configure.
if(MINICRAFT_BUILD_CLIENT)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)

    # Dear ImGui has no CMake build of its own, so compile the sources we need here.
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
endif()

# The simulation, shared by the client (and later the server, the bots and the Python environment). No SDL, no
# rendering, no I/O: it only links the standard library, so nothing platform-specific can creep in.
add_library(game_core STATIC
    game-core/random.cpp
    game-core/items.cpp
    game-core/recipe.cpp
    game-core/tile_map.cpp
    game-core/world_gen.cpp
    game-core/day_night.cpp
    game-core/dropped_items.cpp
    game-core/furniture.cpp
    game-core/mob.cpp
    game-core/mobs.cpp
    game-core/projectiles.cpp
    game-core/player.cpp
    game-core/player_actions.cpp
    game-core/world.cpp
    game-core/simulation.cpp)
target_include_directories(game_core PUBLIC game-core)

# IXWebSocket: the WebSocket client and server for everything but the browser (which has its own WebSocket API).
# No TLS (a reverse proxy adds it in front of the server) and no zlib (so no compression).
if(NOT EMSCRIPTEN)
    set(USE_TLS OFF CACHE BOOL "" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG v12.0.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(ixwebsocket)
endif()

# The messages the client and the server exchange (shared by both), and the client's WebSocket connection.
if(EMSCRIPTEN)
    add_library(net_common STATIC net-common/protocol.cpp net-common/client_socket_web.cpp)
    target_link_libraries(net_common PUBLIC game_core)
else()
    add_library(net_common STATIC net-common/protocol.cpp net-common/client_socket_native.cpp)
    target_link_libraries(net_common PUBLIC game_core ixwebsocket)
endif()
target_include_directories(net_common PUBLIC net-common)

# The game's connection to the server. No SDL, so the server tests can use it too.
add_library(net_client STATIC client/network_client.cpp)
target_include_directories(net_client PUBLIC client)
target_link_libraries(net_client PUBLIC net_common)

# The multiplayer server: lobbies, the 60 Hz tick relay and chat. It runs no game itself.
if(NOT EMSCRIPTEN)
    add_library(server_lib STATIC server/server.cpp server/lobby.cpp server/stats_observer.cpp
        server/stats_reporter.cpp server/stats_json.cpp server/process_usage.cpp)
    target_include_directories(server_lib PUBLIC server)
    target_link_libraries(server_lib PUBLIC net_common ixwebsocket)
    add_executable(minicraft-server server/main.cpp)
    target_link_libraries(minicraft-server PRIVATE server_lib)
endif()

if(MINICRAFT_BUILD_CLIENT)
    # The game.
    add_executable(Minicraft
        client/main.cpp
        client/tile_renderer.cpp
        client/sprite_renderer.cpp
        client/lighting.cpp
        client/item_icons.cpp
        client/camera.cpp
        client/debug_overlay.cpp
        client/effects.cpp
        client/texture.cpp
        client/font.cpp
        client/hud.cpp
        client/inventory_menu.cpp
        client/crafting_menu.cpp
        client/container_menu.cpp
        client/map_screen.cpp
        client/game_menu.cpp
        client/audio.cpp
        client/chat_box.cpp)
    target_link_libraries(Minicraft PRIVATE game_core net_client SDL3::SDL3 imgui)

    if(EMSCRIPTEN)
        # The web page: Minicraft.html (from web/shell.html), .js, .wasm, and .data (the assets, preloaded into the
        # page's virtual file system at /assets, where SDL_GetBasePath() "/" finds them).
        set(MINICRAFT_SERVER_URL "" CACHE STRING "The server the web version connects to, e.g. wss://example.org")
        set_target_properties(Minicraft PROPERTIES SUFFIX ".html")
        target_compile_definitions(Minicraft PRIVATE MINICRAFT_SERVER_URL="${MINICRAFT_SERVER_URL}")
        target_link_options(Minicraft PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=64MB
            -sENVIRONMENT=web
            -lwebsocket.js
            --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
            --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
        set_property(TARGET Minicraft APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
    else()
        # Copy assets next to the executable; the game finds them via SDL_GetBasePath().
        add_custom_command(TARGET Minicraft POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Minicraft>/assets)
    endif()
endif()

if(MINICRAFT_BUILD_TESTS)
    # Unit tests for the core rules (GoogleTest). Run the `game_core_tests` target, or `ctest` from the build folder.
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_executable(game_core_tests
        game-core/tests/simulation_test.cpp
        game-core/tests/world_test.cpp
        game-core/tests/rules_test.cpp
        game-core/tests/multiplayer_test.cpp
        game-core/tests/stats_test.cpp)
    target_link_libraries(game_core_tests PRIVATE game_core GTest::gtest_main)
    # The server and the client's connection, end to end over localhost WebSockets.
    add_executable(server_tests server/tests/server_test.cpp server/tests/stats_test.cpp
        server/tests/protocol_fixtures.cpp)
    target_compile_definitions(server_tests PRIVATE MINICRAFT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(server_tests PRIVATE server_lib net_client GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(game_core_tests)
    gtest_discover_tests(server_tests DISCOVERY_TIMEOUT 30)
endif()
