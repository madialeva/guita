# The client-backend contract, generated once by protoc for both sides:
#   contract  protoc's C++ (guita::v1, no Qt), native and WASM, used by the
#             backend and the client
# Generated headers are included as <guita/v1/...>.
set(GUITA_PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/guita/v1/invoice_service.proto)

find_package(protobuf REQUIRED CONFIG)
# The WebAssembly build of protobuf has no protoc: the host's generates the code
if(EMSCRIPTEN)
    find_program(GUITA_PROTOC_EXECUTABLE protoc
        HINTS ${GUITA_HOST_PROTOBUF_DIR}/bin
        NO_CMAKE_FIND_ROOT_PATH
        REQUIRED)
    set(_protoc PROTOC_EXE ${GUITA_PROTOC_EXECUTABLE})
endif()

add_library(contract STATIC)
protobuf_generate(TARGET contract
    LANGUAGE cpp
    PROTOS ${GUITA_PROTO_FILES}
    IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/cpp
    ${_protoc})
target_include_directories(contract PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/cpp)
target_link_libraries(contract PUBLIC protobuf::libprotobuf)

# Style of the contract (buf lint and buf format); compatibility with the
# development branch is checked by scripts/check-contract.sh
if(BUILD_TESTING AND NOT EMSCRIPTEN)
    find_program(GUITA_BUF_EXECUTABLE buf)
    if(GUITA_BUF_EXECUTABLE)
        add_test(NAME contract_lint COMMAND ${GUITA_BUF_EXECUTABLE} lint
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME contract_format COMMAND ${GUITA_BUF_EXECUTABLE} format --diff --exit-code
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    else()
        add_test(NAME contract_lint COMMAND ${CMAKE_COMMAND} -E echo
            "error: buf not found; install it with scripts/build-deps.sh")
        set_tests_properties(contract_lint PROPERTIES WILL_FAIL TRUE)
    endif()
    set_tests_properties(contract_lint PROPERTIES LABELS unit)
    if(TEST contract_format)
        set_tests_properties(contract_format PROPERTIES LABELS unit)
    endif()
endif()
