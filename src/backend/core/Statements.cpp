#include <backend/core/Statements.h>
#include <backend/core/InvoiceStore.h>

void prepareStatements(pqxx::connection &connection) {
    InvoiceStore::prepare(connection);
}
