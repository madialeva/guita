# Automated tests (Qt Test, run by CTest). Built with BUILD_TESTING=ON (default):
#   ctest --preset native            everything
#   ctest --preset native -L unit    only the suites that need no database
#   ctest --preset native -L db      only the suites that need PostgreSQL
# The database suites get an ephemeral PostgreSQL (Docker) or GUITA_TEST_DB.
find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(libpqxx REQUIRED CONFIG)

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
