find_package(libpqxx REQUIRED CONFIG)
find_package(httplib REQUIRED CONFIG)
find_package(Threads REQUIRED)

# The static build links the static libpq of scripts/build-static-deps.sh, which
# needs the PostgreSQL libraries it was built with and OpenSSL. The PIC variants
# (_shlib) are those PostgreSQL builds its shared libpq from
if(GUITA_STATIC)
    if(NOT PostgreSQL_LIBRARY MATCHES "\\.a$")
        message(FATAL_ERROR
            "GUITA_STATIC needs a static libpq, but found ${PostgreSQL_LIBRARY}: build it with "
            "scripts/build-static-deps.sh and configure with the static preset, which uses it")
    endif()
    cmake_path(GET PostgreSQL_LIBRARY PARENT_PATH GUITA_LIBPQ_DIR)
    find_library(GUITA_PGCOMMON_LIBRARY pgcommon_shlib PATHS ${GUITA_LIBPQ_DIR} NO_DEFAULT_PATH REQUIRED)
    find_library(GUITA_PGPORT_LIBRARY pgport_shlib PATHS ${GUITA_LIBPQ_DIR} NO_DEFAULT_PATH REQUIRED)
    find_package(OpenSSL REQUIRED)
    set_property(TARGET PostgreSQL::PostgreSQL APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        ${GUITA_PGCOMMON_LIBRARY} ${GUITA_PGPORT_LIBRARY} ${OPENSSL_SSL_LIBRARY}
        ${OPENSSL_CRYPTO_LIBRARY} m)
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

add_executable(server server/main.cpp)
target_link_libraries(server PRIVATE api)
