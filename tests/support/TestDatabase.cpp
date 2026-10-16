#include <support/TestDatabase.h>
#include <pqxx/pqxx>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
std::string randomSuffix() {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::random_device device;
    std::string suffix;
    for (int i = 0; i < 8; ++i)
        suffix += kDigits[device() % 16];
    return suffix;
}

std::string identifier(std::string_view suite) {
    std::string name = "guita_test_";
    for (const char c : suite)
        name += std::isalnum(static_cast<unsigned char>(c)) ? std::tolower(static_cast<unsigned char>(c)) : '_';
    return name + '_' + randomSuffix();
}
}

std::string TestDatabase::serverConnectionString() {
    if (const char *conn = std::getenv("GUITA_TEST_DB"); conn && *conn)
        return conn;
    if (const char *file = std::getenv("GUITA_TEST_DB_FILE"); file && *file) {
        std::ifstream in(file);
        std::string conn;
        if (in && std::getline(in, conn) && !conn.empty())
            return conn;
    }
    throw std::runtime_error(
        "No test PostgreSQL server: run the suite through ctest (which starts an "
        "ephemeral container with Docker) or set GUITA_TEST_DB to a key=value libpq "
        "connection string of a server where databases may be created and dropped");
}

TestDatabase::TestDatabase(std::string_view suite)
    : m_server(serverConnectionString()), m_name(identifier(suite)) {
    pqxx::connection server(m_server);
    pqxx::nontransaction tx(server);
    tx.exec("CREATE DATABASE " + tx.quote_name(m_name) + " TEMPLATE template0 ENCODING 'UTF8'");
    // In a libpq connection string the last value of a keyword wins
    m_connectionString = m_server + " dbname=" + m_name;
}

TestDatabase::~TestDatabase() {
    try {
        pqxx::connection server(m_server);
        pqxx::nontransaction tx(server);
        tx.exec("DROP DATABASE IF EXISTS " + tx.quote_name(m_name) + " WITH (FORCE)");
    } catch (const std::exception &e) {
        std::cerr << "warning: could not drop test database " << m_name << ": " << e.what() << '\n';
    }
}
