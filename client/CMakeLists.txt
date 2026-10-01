find_package(SDL3 CONFIG REQUIRED)

add_executable(minicraft-client
    src/main.cpp
    src/application.cpp
    src/sdl_platform.cpp)

target_link_libraries(minicraft-client PRIVATE minicraft::game-core SDL3::SDL3)
minicraft_set_warnings(minicraft-client)
