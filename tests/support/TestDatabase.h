#pragma once
#include <string>
#include <string_view>

// A database of its own for one test suite, created empty on the test
// PostgreSQL server and dropped on destruction, so that suites can run in
// parallel and leave nothing behind.
//
// The server comes from GUITA_TEST_DB or from the file written by the CTest
// fixture (GUITA_TEST_DB_FILE); never from the application's defaults.
class TestDatabase {
public:
    // Creates guita_test_<suite>_<random> from template0. Throws when no test
    // server is configured or reachable, with a message saying how to provide one.
    explicit TestDatabase(std::string_view suite);
    ~TestDatabase();

    TestDatabase(const TestDatabase &) = delete;
    TestDatabase &operator=(const TestDatabase &) = delete;

    const std::string &name() const { return m_name; }

    // libpq connection string to this database
    const std::string &connectionString() const { return m_connectionString; }

    // libpq connection string to the test server (its maintenance database)
    static std::string serverConnectionString();

private:
    std::string m_server;
    std::string m_name;
    std::string m_connectionString;
};
