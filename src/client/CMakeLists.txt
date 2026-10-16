# The Guita QML module is defined next to its QML files
qt_add_executable(client main.cpp ApiConfig.h)
qt_add_qml_module(client
    URI Guita
    VERSION 1.0
    SOURCES
        ApiConnection.h ApiConnection.cpp
        InvoiceRepository.h InvoiceRepository.cpp
    QML_FILES Main.qml)
# Project headers resolve from src/ (<client/...>). The generated QML type
# registration includes each header by its bare file name, hence this directory
# as an extra private include directory
target_include_directories(client PRIVATE ${GUITA_INCLUDE_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client PRIVATE Qt6::Quick Qt6::Network)

# Languages from qt_standard_project_setup; catalogs embedded under :/i18n
qt_add_translations(TARGETS client
    TS_FILE_BASE guita
    TS_FILE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/translations)

if(NOT EMSCRIPTEN)
    # Standalone mode embeds the backend. The client only includes
    # backend/api/Backend.h, which keeps the backend libraries private
    target_link_libraries(client PRIVATE api)
    target_compile_definitions(client PRIVATE APP_STANDALONE_SUPPORT)
endif()
