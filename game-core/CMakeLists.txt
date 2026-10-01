# game-core: deterministic simulation. No I/O, rendering, or networking dependencies.
add_library(game-core STATIC
    src/build_info.cpp)
add_library(minicraft::game-core ALIAS game-core)

target_include_directories(game-core PUBLIC include)
target_compile_features(game-core PUBLIC cxx_std_20)
minicraft_set_warnings(game-core)
