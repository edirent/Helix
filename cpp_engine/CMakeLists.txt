cmake_minimum_required(VERSION 3.15)
project(helix_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(helix_engine
    src/event_bus.cpp
    src/tick_replay.cpp
    src/feature_engine.cpp
    src/decision_engine.cpp
    src/risk_engine.cpp
    src/matching_engine.cpp
    src/recorder.cpp
    src/transport_servers.cpp
)
target_include_directories(helix_engine PUBLIC include)

add_executable(helix_engine_main src/main.cpp)
target_link_libraries(helix_engine_main PRIVATE helix_engine)

enable_testing()

add_executable(test_replay tests/test_replay.cpp)
target_link_libraries(test_replay PRIVATE helix_engine)
add_test(NAME test_replay COMMAND test_replay)

add_executable(test_match tests/test_match.cpp)
target_link_libraries(test_match PRIVATE helix_engine)
add_test(NAME test_match COMMAND test_match)

add_executable(test_features tests/test_features.cpp)
target_link_libraries(test_features PRIVATE helix_engine)
add_test(NAME test_features COMMAND test_features)
