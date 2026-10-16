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

# REST routes and Backend (cpp-httplib), shared by server and standalone client
add_library(api STATIC api/Http.cpp api/InvoiceRoutes.cpp api/Backend.cpp)
target_link_libraries(api PUBLIC core PRIVATE httplib::httplib glaze::glaze)

add_executable(server server/main.cpp)
target_link_libraries(server PRIVATE api)
