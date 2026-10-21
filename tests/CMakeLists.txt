# Automated tests (Qt Test, run by CTest). Built with BUILD_TESTING=ON (default):
#   ctest --preset native            everything
#   ctest --preset native -L unit    only the suites that need no database
#   ctest --preset native -L db      only the suites that need PostgreSQL
# The database suites get an ephemeral PostgreSQL (Docker) or GUITA_TEST_DB.
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
find_package(libpqxx REQUIRED CONFIG)
find_package(httplib REQUIRED CONFIG)

# PostgreSQL fixture: CTest runs "start" once before the first suite that needs
# it and "stop" after the last one, also when suites fail
set(GUITA_TEST_DB_FILE ${CMAKE_CURRENT_BINARY_DIR}/postgres.conn)
add_test(NAME postgres_start COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/postgres-fixture.sh start ${GUITA_TEST_DB_FILE})
add_test(NAME postgres_stop  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/postgres-fixture.sh stop  ${GUITA_TEST_DB_FILE})
set_tests_properties(postgres_start PROPERTIES FIXTURES_SETUP postgres LABELS fixture)
set_tests_properties(postgres_stop  PROPERTIES FIXTURES_CLEANUP postgres LABELS fixture)

# guita_add_test(<name> LABELS <labels...> [LIBS <libs...>] [SOURCES <files...>] [DATABASE])
# Builds test_<name>.cpp (plus SOURCES) against the layer under test and runs it
# headless. Labels: unit, db, api, client, qml. DATABASE makes the suite require
# the PostgreSQL fixture and gives it TestDatabase (<support/TestDatabase.h>).
function(guita_add_test name)
    cmake_parse_arguments(TEST "DATABASE" "" "LABELS;LIBS;SOURCES" ${ARGN})
    add_executable(test_${name} test_${name}.cpp ${TEST_SOURCES})
    # Test helpers are included as <support/...>
    target_include_directories(test_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_${name} PRIVATE Qt6::Test ${TEST_LIBS})
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "${TEST_LABELS}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;GUITA_TEST_DB_FILE=${GUITA_TEST_DB_FILE}")
    if(TEST_DATABASE)
        target_link_libraries(test_${name} PRIVATE guita_test_support)
        set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED postgres)
    endif()
endfunction()

# Shared helpers of the database suites
add_library(guita_test_support STATIC support/TestDatabase.cpp)
target_include_directories(guita_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(guita_test_support PUBLIC libpqxx::pqxx)

# Test harness
guita_add_test(testdatabase LABELS db DATABASE)

# Contract: reference bytes of its messages (also checked in WebAssembly by bench_encoding)
guita_add_test(contract LABELS unit LIBS contract)

# Version in CMakeLists.txt against the branch name (scripts/check-version.sh, run by the CI)
set(GUITA_CHECK_VERSION ${PROJECT_SOURCE_DIR}/scripts/check-version.sh)
math(EXPR GUITA_OTHER_MINOR "${PROJECT_VERSION_MINOR} + 1")
add_test(NAME version_check_develop COMMAND ${GUITA_CHECK_VERSION} develop/v${PROJECT_VERSION})
add_test(NAME version_check_release COMMAND ${GUITA_CHECK_VERSION} release/v${PROJECT_VERSION})
add_test(NAME version_check_mismatch
    COMMAND ${GUITA_CHECK_VERSION} develop/v${PROJECT_VERSION_MAJOR}.${GUITA_OTHER_MINOR}.${PROJECT_VERSION_PATCH})
add_test(NAME version_check_change_branch COMMAND ${GUITA_CHECK_VERSION} change/is4-github-actions-ci)
set_tests_properties(version_check_develop version_check_release version_check_mismatch
    version_check_change_branch PROPERTIES LABELS unit)
# A mismatch must fail with its message, not for any other reason
set_tests_properties(version_check_mismatch PROPERTIES
    PASS_REGULAR_EXPRESSION "error: CMakeLists.txt has version ${PROJECT_VERSION}, but branch")

# Backend without database
guita_add_test(invoiceservice LABELS unit LIBS core)
guita_add_test(authorizer LABELS unit LIBS api)
guita_add_test(rpcrouter LABELS unit LIBS api contract)

# Backend with database
guita_add_test(migrations LABELS db LIBS core DATABASE)
guita_add_test(invoicestore LABELS db LIBS core DATABASE)
guita_add_test(database LABELS db LIBS api DATABASE)
guita_add_test(idempotency LABELS db LIBS core DATABASE)

# RPC API
guita_add_test(rpcapi LABELS api LIBS api contract httplib::httplib DATABASE)

# Client: its classes are compiled into the suite, since they live in the
# client executable that backs the QML module
set(GUITA_CLIENT_DIR ${PROJECT_SOURCE_DIR}/src/client)
guita_add_test(invoicerepository LABELS client
    LIBS api contract Qt6::Network Qt6::Qml
    SOURCES ${GUITA_CLIENT_DIR}/ApiConnection.h ${GUITA_CLIENT_DIR}/ApiConnection.cpp
            ${GUITA_CLIENT_DIR}/ApiTransport.h
            ${GUITA_CLIENT_DIR}/HttpTransport.h ${GUITA_CLIENT_DIR}/HttpTransport.cpp
            ${GUITA_CLIENT_DIR}/InProcTransport.h ${GUITA_CLIENT_DIR}/InProcTransport.cpp
            ${GUITA_CLIENT_DIR}/InvoiceRepository.h ${GUITA_CLIENT_DIR}/InvoiceRepository.cpp
            ${GUITA_CLIENT_DIR}/InvoiceRow.h
    DATABASE)

# QML logic (Qt Quick Test): the tst_*.qml cases in tests/qml
guita_add_test(amount LABELS qml LIBS Qt6::QuickTest)
target_compile_definitions(test_amount PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
