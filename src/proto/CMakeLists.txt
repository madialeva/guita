# The client-backend contract, generated twice from the same .proto files:
#   contract     protoc's C++ for the backend (guita::v1, no Qt), native only
#   contract_qt  Qt Protobuf for the client (qtc::guita::v1), native and WASM
# Both are linked into the standalone client, hence the extra namespace of the
# Qt side. Generated headers are included as <guita/v1/...>.
set(GUITA_PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/guita/v1/invoice_service.proto)

if(NOT EMSCRIPTEN)
    find_package(protobuf REQUIRED CONFIG)

    add_library(contract STATIC)
    protobuf_generate(TARGET contract
        LANGUAGE cpp
        PROTOS ${GUITA_PROTO_FILES}
        IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/cpp)
    target_include_directories(contract PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/cpp)
    target_link_libraries(contract PUBLIC protobuf::libprotobuf)
endif()

find_package(Qt6 REQUIRED COMPONENTS Protobuf)
qt_add_protobuf(contract_qt
    PROTO_FILES ${GUITA_PROTO_FILES}
    PROTO_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
    EXTRA_NAMESPACE qtc
    GENERATE_PACKAGE_SUBFOLDERS
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qt)

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
