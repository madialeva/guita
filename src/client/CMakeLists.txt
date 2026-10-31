# The Guita QML module is defined next to its QML files
qt_add_executable(client main.cpp ApiConfig.h)
qt_add_qml_module(client
    URI Guita
    VERSION 1.0
    SOURCES
        ApiConnection.h ApiConnection.cpp
        ApiTransport.h WebSocketTransport.h WebSocketTransport.cpp
        InvoiceRepository.h InvoiceRepository.cpp InvoiceRow.h
    QML_FILES Main.qml Amount.js)
# Project headers resolve from src/ (<client/...>). The generated QML type
# registration includes each header by its bare file name, hence this directory
# as an extra private include directory
target_include_directories(client PRIVATE ${GUITA_INCLUDE_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client PRIVATE Qt6::Quick Qt6::WebSockets contract)

# Languages from qt_standard_project_setup; catalogs embedded under :/i18n
qt_add_translations(TARGETS client
    TS_FILE_BASE guita
    TS_FILE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/translations)

if(EMSCRIPTEN)
    # The web client's handling of dead keys (Qt 6.12 for WebAssembly loses them)
    target_sources(client PRIVATE WebKeyboard.h WebKeyboard.cpp)
endif()

if(NOT EMSCRIPTEN)
    # Standalone mode embeds the backend and calls it in process. The client
    # only includes backend/api/Backend.h, which keeps the backend libraries private
    target_sources(client PRIVATE InProcTransport.h InProcTransport.cpp)
    target_link_libraries(client PRIVATE api)
    target_compile_definitions(client PRIVATE APP_STANDALONE_SUPPORT)
endif()

if(GUITA_STATIC)
    # A static Qt links its plugins into the executable, so they are fixed here:
    # X11 with its OpenGL integrations, the input methods that compose dead keys,
    # the desktop portal's theme, and TLS through the system's OpenSSL (loaded at run
    # time). The client loads no images, takes no TUIO touch input, uses no network
    # information and is not debugged through QML tooling. qmlimportscanner adds the
    # QML modules' plugins
    qt_import_plugins(client
        INCLUDE_BY_TYPE platforms Qt6::QXcbIntegrationPlugin
        INCLUDE_BY_TYPE xcbglintegrations Qt6::QXcbGlxIntegrationPlugin Qt6::QXcbEglIntegrationPlugin
        INCLUDE_BY_TYPE platforminputcontexts
            Qt6::QComposePlatformInputContextPlugin Qt6::QIbusPlatformInputContextPlugin
        INCLUDE_BY_TYPE platformthemes Qt6::QXdgDesktopPortalThemePlugin
        INCLUDE_BY_TYPE tls Qt6::QTlsBackendOpenSSLPlugin
        EXCLUDE_BY_TYPE imageformats generic networkinformation qmltooling)
endif()
