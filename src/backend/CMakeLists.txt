find_package(libpqxx REQUIRED CONFIG)
find_package(httplib REQUIRED CONFIG)
find_package(Threads REQUIRED)

# The static builds link a static libpq of toolchain-cpp (libpq-static, or libpq-mingw
# for Windows), which needs the PostgreSQL libraries it was built with and OpenSSL.
# The PIC variants (_shlib) are those PostgreSQL builds its shared libpq from. On
# Windows OpenSSL is static too (openssl-mingw), and both need Windows' sockets, SSPI
# and certificate libraries; on Linux, OpenSSL is the system's
if(GUITA_STATIC)
    if(NOT PostgreSQL_LIBRARY MATCHES "\\.a$")
        message(FATAL_ERROR
            "GUITA_STATIC needs a static libpq, but found ${PostgreSQL_LIBRARY}: install it with "
            "the libpq-static component of toolchain-cpp (libpq-mingw for Windows) and configure "
            "with the static or windows preset, which uses it")
    endif()
    cmake_path(GET PostgreSQL_LIBRARY PARENT_PATH GUITA_LIBPQ_DIR)
    find_library(GUITA_PGCOMMON_LIBRARY pgcommon_shlib PATHS ${GUITA_LIBPQ_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(GUITA_PGPORT_LIBRARY pgport_shlib PATHS ${GUITA_LIBPQ_DIR} NO_DEFAULT_PATH REQUIRED)
    if(WIN32)
        set(OPENSSL_USE_STATIC_LIBS TRUE)
        set(GUITA_LIBPQ_SYSTEM_LIBRARIES ws2_32 secur32 crypt32)
    else()
        set(GUITA_LIBPQ_SYSTEM_LIBRARIES m)
    endif()
    find_package(OpenSSL REQUIRED)
    set_property(TARGET PostgreSQL::PostgreSQL APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${GUITA_PGCOMMON_LIBRARY} ${GUITA_PGPORT_LIBRARY} ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY} ${GUITA_LIBPQ_SYSTEM_LIBRARIES})
endif()

# Domain, persistence (libpqxx) and business services
add_library(core STATIC
    core/Database.cpp core/Migrations.cpp core/InvoiceService.cpp
    core/InvoiceStore.cpp core/Statements.cpp)
target_include_directories(core PUBLIC ${GUITA_INCLUDE_ROOT})
target_link_libraries(core PUBLIC libpqxx::pqxx Threads::Threads)

# RPC router, its network transports (HTTP and the WebSocket channel, over
# cpp-httplib) and Backend, shared by the server and the standalone client
add_library(api STATIC
    api/RpcRouter.cpp api/HttpRpcTransport.cpp api/WebSocketRpcTransport.cpp
    api/InvoiceProcedures.cpp api/Backend.cpp)
target_link_libraries(api PUBLIC core contract PRIVATE httplib::httplib)

# The server is POSIX only (threads and signals in server/main.cpp): it is not built
# for Windows, where only the client, with the embedded backend, is
if(NOT WIN32)
    add_executable(server server/main.cpp)
    target_link_libraries(server PRIVATE api)
endif()
