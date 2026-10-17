find_package(libpqxx REQUIRED CONFIG)
find_package(httplib REQUIRED CONFIG)
find_package(glaze REQUIRED CONFIG)
find_package(Threads REQUIRED)

# Domain, persistence (libpqxx) and business services
add_library(core STATIC
    core/Database.cpp core/Migrations.cpp core/InvoiceService.cpp
    core/InvoiceStore.cpp core/Statements.cpp)
target_include_directories(core PUBLIC ${GUITA_INCLUDE_ROOT})
target_link_libraries(core PUBLIC libpqxx::pqxx Threads::Threads)

# RPC router, its HTTP transport (cpp-httplib) and Backend, shared by the server
# and the standalone client; still serves the REST routes until the client
# moves to RPC
add_library(api STATIC
    api/RpcRouter.cpp api/HttpRpcTransport.cpp api/InvoiceProcedures.cpp
    api/Http.cpp api/InvoiceRoutes.cpp api/Backend.cpp)
target_link_libraries(api PUBLIC core contract PRIVATE httplib::httplib glaze::glaze)

add_executable(server server/main.cpp)
target_link_libraries(server PRIVATE api)
