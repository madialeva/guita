#include <backend/core/Database.h>

Database::Database(std::string connectionString, std::size_t poolSize, Preparer preparer)
    : m_connectionString(std::move(connectionString)),
      m_poolSize(poolSize),
      m_preparer(std::move(preparer)) {}

void Database::warmUp() {
    release(acquire());
}

std::unique_ptr<pqxx::connection> Database::connect() {
    auto connection = std::make_unique<pqxx::connection>(m_connectionString);
    if (m_preparer) m_preparer(*connection);
    return connection;
}

std::unique_ptr<pqxx::connection> Database::acquire() {
    std::unique_lock lock(m_mutex);
    m_available.wait(lock, [this] { return !m_idle.empty() || m_open < m_poolSize; });

    if (!m_idle.empty()) {
        auto connection = std::move(m_idle.back());
        m_idle.pop_back();
        return connection;
    }

    // Reserve the slot, then connect without holding the lock
    ++m_open;
    lock.unlock();
    try {
        return connect();
    } catch (...) {
        lock.lock();
        --m_open;
        m_available.notify_one();
        throw;
    }
}

void Database::release(std::unique_ptr<pqxx::connection> connection) {
    {
        std::lock_guard lock(m_mutex);
        if (connection && connection->is_open())
            m_idle.push_back(std::move(connection));
        else
            --m_open;             // broken: a new one is opened on demand
    }
    m_available.notify_one();
}
