#pragma once
#include <pqxx/pqxx>

// Applies the pending migrations in a single transaction (transactional DDL).
void applyMigrations(pqxx::connection &connection);
