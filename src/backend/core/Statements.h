#pragma once
#include <pqxx/pqxx>

// Registers the prepared statements of every store on a new connection.
void prepareStatements(pqxx::connection &connection);
