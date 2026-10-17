# Measurements, outside ctest; built with -DGUITA_BENCH=ON (results in
# docs/architecture.md):
#   bench_encoding  size and coding time of invoice lists: JSON, compressed
#                   JSON and Protobuf (native and WASM, after checking the
#                   reference bytes of tests/support/ProtoSamples.h)
#   bench_latency   round trip of a small call per transport (native)
qt_add_executable(bench_encoding encoding_bench.cpp)
target_include_directories(bench_encoding PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(bench_encoding PRIVATE contract Qt6::Core)

if(NOT EMSCRIPTEN)
    find_package(httplib REQUIRED CONFIG)
    add_executable(bench_latency latency_bench.cpp)
    target_include_directories(bench_latency PRIVATE ${GUITA_INCLUDE_ROOT})
    target_link_libraries(bench_latency PRIVATE api contract httplib::httplib Qt6::Core)
endif()
