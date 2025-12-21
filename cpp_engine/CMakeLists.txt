cmake_minimum_required(VERSION 3.15)
project(helix_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(helix_engine
    src/event_bus.cpp
    src/tick_replay.cpp
    src/fee_model.cpp
    src/rules_engine.cpp
    src/order_manager.cpp
    src/feature_engine.cpp
    src/decision_engine.cpp
    src/risk_engine.cpp
    src/matching_engine.cpp
    src/maker_queue.cpp
    src/recorder.cpp
    src/transport_servers.cpp
)
target_include_directories(helix_engine PUBLIC include)

add_executable(helix_engine_main src/main.cpp)
target_link_libraries(helix_engine_main PRIVATE helix_engine)

add_executable(feature_dump src/feature_dump.cpp)
target_link_libraries(feature_dump PRIVATE helix_engine)

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

add_executable(test_matching_conservation tests/test_matching_conservation.cpp)
target_link_libraries(test_matching_conservation PRIVATE helix_engine)
add_test(NAME test_matching_conservation COMMAND test_matching_conservation)

add_executable(test_latency_causality tests/test_latency_causality.cpp)
target_link_libraries(test_latency_causality PRIVATE helix_engine)
add_test(NAME test_latency_causality COMMAND test_latency_causality)

add_executable(test_depth_behaviour tests/test_depth_behaviour.cpp)
target_link_libraries(test_depth_behaviour PRIVATE helix_engine)
add_test(NAME test_depth_behaviour COMMAND test_depth_behaviour)

add_executable(test_maker_queue tests/test_maker_queue.cpp)
target_link_libraries(test_maker_queue PRIVATE helix_engine)
add_test(NAME test_maker_queue COMMAND test_maker_queue)

add_executable(test_maker_trades tests/test_maker_trades.cpp)
target_link_libraries(test_maker_trades PRIVATE helix_engine)
add_test(NAME test_maker_trades COMMAND test_maker_trades)

add_executable(test_matching_fuzz tests/test_matching_fuzz.cpp)
target_link_libraries(test_matching_fuzz PRIVATE helix_engine)
add_test(NAME test_matching_fuzz COMMAND test_matching_fuzz)

add_executable(test_crossing_classification tests/test_crossing_classification.cpp)
target_link_libraries(test_crossing_classification PRIVATE helix_engine)
add_test(NAME test_crossing_classification COMMAND test_crossing_classification)

add_executable(test_crossing_equals_taker tests/test_crossing_equals_taker.cpp)
target_link_libraries(test_crossing_equals_taker PRIVATE helix_engine)
add_test(NAME test_crossing_equals_taker COMMAND test_crossing_equals_taker)

add_executable(test_pnl_bookkeeping tests/test_pnl_bookkeeping.cpp)
target_link_libraries(test_pnl_bookkeeping PRIVATE helix_engine)
add_test(NAME test_pnl_bookkeeping COMMAND test_pnl_bookkeeping)

add_executable(test_exec_cost_identity tests/test_exec_cost_identity.cpp)
target_link_libraries(test_exec_cost_identity PRIVATE helix_engine)
add_test(NAME test_exec_cost_identity COMMAND test_exec_cost_identity)

add_executable(test_deterministic_hash tests/test_deterministic_hash.cpp)
target_link_libraries(test_deterministic_hash PRIVATE helix_engine)
add_test(NAME test_deterministic_hash COMMAND test_deterministic_hash)

add_executable(test_rules_fee tests/test_rules_fee.cpp)
target_link_libraries(test_rules_fee PRIVATE helix_engine)
add_test(NAME test_rules_fee COMMAND test_rules_fee)

add_executable(test_rules_normalization tests/test_rules_normalization.cpp)
target_link_libraries(test_rules_normalization PRIVATE helix_engine)
add_test(NAME test_rules_normalization COMMAND test_rules_normalization)

add_executable(test_rules_rounding_direction tests/test_rules_rounding_direction.cpp)
target_link_libraries(test_rules_rounding_direction PRIVATE helix_engine)
add_test(NAME test_rules_rounding_direction COMMAND test_rules_rounding_direction)

add_executable(test_fee_exactness_smoke tests/test_fee_exactness_smoke.cpp)
target_link_libraries(test_fee_exactness_smoke PRIVATE helix_engine)
add_test(NAME test_fee_exactness_smoke COMMAND test_fee_exactness_smoke)

add_executable(test_fee_split tests/test_fee_split.cpp)
target_link_libraries(test_fee_split PRIVATE helix_engine)
add_test(NAME test_fee_split COMMAND test_fee_split)

add_executable(test_adv_selection tests/test_adv_selection.cpp)
target_link_libraries(test_adv_selection PRIVATE helix_engine)
add_test(NAME test_adv_selection COMMAND test_adv_selection)

add_executable(test_order_lifecycle tests/test_order_lifecycle.cpp)
target_link_libraries(test_order_lifecycle PRIVATE helix_engine)
add_test(NAME test_order_lifecycle COMMAND test_order_lifecycle)

set(BYBIT_REPLAY ${CMAKE_CURRENT_SOURCE_DIR}/../data/replay/bybit_l2.csv)
set(REPLAY_BOOKCHECK ${CMAKE_CURRENT_BINARY_DIR}/replay_bookcheck.csv)
set(GO_BOOKCHECK ${CMAKE_CURRENT_BINARY_DIR}/go_bookcheck.csv)

add_test(NAME bookcheck_cpp
         COMMAND helix_engine_main ${BYBIT_REPLAY} --no_actions --bookcheck ${REPLAY_BOOKCHECK} --bookcheck_every 100)

add_test(NAME bookcheck_go
         COMMAND go run ./cmd/bookcheck_from_csv --in ${BYBIT_REPLAY} --out ${GO_BOOKCHECK} --every 100)
set_tests_properties(bookcheck_go PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../gateway)

add_test(NAME test_bookcheck_diff
         COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/bookcheck_diff.py ${GO_BOOKCHECK} ${REPLAY_BOOKCHECK})
set_tests_properties(test_bookcheck_diff PROPERTIES DEPENDS "bookcheck_cpp;bookcheck_go")

add_test(NAME test_replay_rejects_negative_qty
         COMMAND helix_engine_main ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bad_negative_qty.csv --no_actions)
set_tests_properties(test_replay_rejects_negative_qty PROPERTIES WILL_FAIL TRUE)

add_test(NAME test_replay_rejects_seq_gap
         COMMAND helix_engine_main ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bad_seq_gap.csv --no_actions)
set_tests_properties(test_replay_rejects_seq_gap PROPERTIES WILL_FAIL TRUE)
