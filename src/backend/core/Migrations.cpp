#include <backend/core/Migrations.h>
#include <string_view>
#include <utility>
#include <vector>

namespace {
// One statement per migration. Never edit a released migration: add a new one.
const std::vector<std::pair<int, std::string_view>> kMigrations = {
    {1, R"sql(
        CREATE TABLE invoices (
            id              bigserial     PRIMARY KEY,
            version         integer       NOT NULL,
            customer        text          NOT NULL,
            customer_tax_id text,
            total           numeric(12,2) NOT NULL CHECK (total >= 0)
        ))sql"},
    // {2, "ALTER TABLE ..."},
};

// Arbitrary key that serializes migrations between instances starting at once
constexpr long long kMigrationLock = 0x6775697461;   // "guita"
}

void applyMigrations(pqxx::connection &connection) {
    pqxx::work tx(connection);
    tx.exec("SELECT pg_advisory_xact_lock($1)", pqxx::params{kMigrationLock});
    tx.exec("CREATE TABLE IF NOT EXISTS schema_version (version integer PRIMARY KEY)");
    const int current = tx.query_value<int>("SELECT COALESCE(MAX(version), 0) FROM schema_version");

    for (const auto &[version, sql] : kMigrations) {
        if (version <= current) continue;
        tx.exec(sql);
        tx.exec("INSERT INTO schema_version (version) VALUES ($1)", pqxx::params{version});
    }
    tx.commit();
}
