#pragma once
#include <pqxx/pqxx>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Pool of PostgreSQL connections shared by the services. pqxx::connection is
// not thread-safe and the HTTP server handles requests concurrently: each
// operation borrows a connection for the duration of one transaction.
class Database {
public:
    // Called on every new connection to register its prepared statements
    using Preparer = std::function<void(pqxx::connection &)>;

    Database(std::string connectionString, std::size_t poolSize, Preparer preparer);

    // Opens one connection now, so that a wrong connection string or a
    // statement that does not match the schema fails at startup
    void warmUp();

    // Runs fn(pqxx::work &) in a transaction, committed when fn returns
    // normally and rolled back when it throws. Returns what fn returns.
    template <class Fn>
    decltype(auto) inTransaction(Fn &&fn) {
        Lease lease(*this);
        pqxx::work tx(lease.connection());
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, pqxx::work &>>) {
            std::forward<Fn>(fn)(tx);
            tx.commit();
        } else {
            auto result = std::forward<Fn>(fn)(tx);
            tx.commit();
            return result;
        }
    }

private:
    // Borrows a connection and gives it back on destruction
    class Lease {
    public:
        explicit Lease(Database &db) : m_db(db), m_connection(db.acquire()) {}
        ~Lease() { m_db.release(std::move(m_connection)); }
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        pqxx::connection &connection() { return *m_connection; }

    private:
        Database &m_db;
        std::unique_ptr<pqxx::connection> m_connection;
    };

    std::unique_ptr<pqxx::connection> acquire();
    void release(std::unique_ptr<pqxx::connection> connection);
    std::unique_ptr<pqxx::connection> connect();

    const std::string m_connectionString;
    const std::size_t m_poolSize;
    const Preparer m_preparer;

    std::mutex m_mutex;
    std::condition_variable m_available;
    std::vector<std::unique_ptr<pqxx::connection>> m_idle;
    std::size_t m_open = 0;   // idle + borrowed
};
