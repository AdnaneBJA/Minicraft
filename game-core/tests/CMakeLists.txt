find_package(GTest CONFIG REQUIRED)

add_executable(game-core-tests
    build_info_test.cpp)
target_link_libraries(game-core-tests PRIVATE minicraft::game-core GTest::gtest_main)
minicraft_set_warnings(game-core-tests)

include(GoogleTest)
gtest_discover_tests(game-core-tests)
