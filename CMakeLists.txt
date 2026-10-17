cmake_minimum_required(VERSION 3.21)
project(Guita VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The backend libraries are static and get linked into the Qt client (PIE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Project headers are included from src/: <backend/...>, <client/...>
set(GUITA_INCLUDE_ROOT ${PROJECT_SOURCE_DIR}/src)

# Qt 6.12 LTS everywhere: desktop and WASM kits in ~/software/Qt (see docs/architecture.md)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick Network LinguistTools)
qt_standard_project_setup(REQUIRES 6.12
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES es)

# BUILD_TESTING (ON by default) controls the test suite in tests/
include(CTest)

# The client-backend contract (Protobuf), generated for both sides
add_subdirectory(src/proto)

# The backend is native only: never built for WASM
if(NOT EMSCRIPTEN)
    add_subdirectory(src/backend)
endif()
add_subdirectory(src/client)

# Tests run natively: they need the backend, PostgreSQL and a native Qt
if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_subdirectory(tests)
endif()
