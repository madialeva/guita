cmake_minimum_required(VERSION 3.25)
project(Guita VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The self-contained desktop client of the static preset (scripts/build-static.sh,
# docs/architecture.md §9.2): Qt, libpq and the C++ runtime linked into the executable
option(GUITA_STATIC "Link Qt, libpq and the C++ runtime statically into the native executables" OFF)

# The native compiler: GCC 16 or later, with C++26 reflection (scripts/build-gcc.sh,
# docs/architecture.md §8), checked before anything is searched or built. The web
# client is built by the Clang of the emsdk that Qt requires
if(NOT EMSCRIPTEN)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        message(FATAL_ERROR
            "Found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER}), "
            "but the native build needs GCC 16 or later with C++26 reflection: build it with "
            "scripts/build-gcc.sh and configure with the native preset, which uses it")
    endif()
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS -freflection)
    check_cxx_source_compiles("static_assert(^^int != ^^long); int main() {}" GUITA_HAS_REFLECTION)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT GUITA_HAS_REFLECTION)
        message(FATAL_ERROR
            "${CMAKE_CXX_COMPILER} (GCC ${CMAKE_CXX_COMPILER_VERSION}) does not accept "
            "-std=c++26 -freflection, which the native build needs: use the GCC of scripts/build-gcc.sh")
    endif()

    if(GUITA_STATIC)
        # With a static Qt nothing else loads a libstdc++, so the executables carry
        # their own and need no run path
        add_link_options(-static-libstdc++ -static-libgcc)
    else()
        # Its libstdc++ is newer than the system's: the executables find it through
        # their run path, and the Qt libraries they load share it
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
            OUTPUT_VARIABLE GUITA_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        file(REAL_PATH "${GUITA_LIBSTDCXX}" GUITA_LIBSTDCXX)
        cmake_path(GET GUITA_LIBSTDCXX PARENT_PATH GUITA_LIBSTDCXX_DIR)
        list(APPEND CMAKE_BUILD_RPATH ${GUITA_LIBSTDCXX_DIR})
    endif()
endif()
# The backend libraries are static and get linked into the Qt client (PIE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Project headers are included from src/: <backend/...>, <client/...>
set(GUITA_INCLUDE_ROOT ${PROJECT_SOURCE_DIR}/src)

# Qt 6.12 LTS everywhere: desktop and WASM kits in ~/software/Qt (see docs/architecture.md)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick WebSockets LinguistTools)
if(GUITA_STATIC)
    get_target_property(GUITA_QT_CORE_TYPE Qt6::Core TYPE)
    if(NOT GUITA_QT_CORE_TYPE STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "GUITA_STATIC needs a static Qt, but the Qt found in ${Qt6_DIR} is shared: build "
            "the static one in ~/software/Qt/6.12.0-static with scripts/build-static-deps.sh "
            "and configure with the static preset, which uses it")
    endif()
endif()
qt_standard_project_setup(REQUIRES 6.12
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES es)

# BUILD_TESTING (ON by default) controls the test suite in tests/
include(CTest)

# The client-backend contract (Protobuf), generated once for both sides
add_subdirectory(src/proto)

# The backend is native only: never built for WASM
if(NOT EMSCRIPTEN)
    add_subdirectory(src/backend)
endif()
add_subdirectory(src/client)

# Measurements (tests/bench), native and WASM; off by default
option(GUITA_BENCH "Build the measurement programs in tests/bench" OFF)
if(GUITA_BENCH)
    add_subdirectory(tests/bench)
endif()

# Tests run natively: they need the backend, PostgreSQL and a native Qt
if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_subdirectory(tests)
endif()
