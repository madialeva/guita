#include <client/ApiConfig.h>
#include <client/ApiConnection.h>
#include <client/HttpTransport.h>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <memory>

#ifdef APP_STANDALONE_SUPPORT
#include <backend/api/Backend.h>
#include <client/InProcTransport.h>
#include <exception>
#endif

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("guita");
    QCoreApplication::setApplicationName("guita");

    QTranslator translator;
    if (translator.load(QLocale(), "guita", "_", ":/i18n"))
        QCoreApplication::installTranslator(&translator);

    QCommandLineParser parser;
    parser.addHelpOption();
#ifdef APP_STANDALONE_SUPPORT
    QCommandLineOption standaloneOption("standalone", QCoreApplication::translate("main",
        "Runs the backend inside the process, without network."));
    QCommandLineOption dbOption("db", QCoreApplication::translate("main",
        "PostgreSQL connection string in standalone mode (defaults to $DB_CONN)."), "conn");
    parser.addOptions({standaloneOption, dbOption});
#endif
    parser.process(app);

#ifdef Q_OS_WASM
    const auto mode = ApiConnection::Web;
#else
    const auto mode = ApiConnection::Connected;
#endif
    // Web / connected modes: HTTP, with the shared development token, if any
    ApiConnection::Settings settings{mode, defaultApiBaseUrl(),
                                     std::make_shared<HttpTransport>(defaultApiBaseUrl(), qgetenv("API_TOKEN"))};

#ifdef APP_STANDALONE_SUPPORT
    // Destroyed when main returns, after the QML engine (and with it the
    // transport that calls it) is gone
    std::unique_ptr<Backend> backend;

    if (parser.isSet(standaloneOption)) {
        const auto connection = parser.isSet(dbOption)
            ? parser.value(dbOption)
            : qEnvironmentVariable("DB_CONN", "host=127.0.0.1 dbname=guita user=guita password=guita");
        try {
            // No HTTP: nothing listens on the network, so in-process calls need
            // no credentials
            backend = std::make_unique<Backend>(BackendOptions{
                .connectionString = connection.toStdString(),
                .authorizer = tokenAuthorizer(""),
            });
        } catch (const std::exception &e) {
            qCritical("Could not start the embedded backend: %s", e.what());
            return 1;
        }
        settings = {ApiConnection::Standalone, {}, std::make_shared<InProcTransport>(*backend)};
    }
#endif

    ApiConnection::configure(std::move(settings));

    int status = 0;
    {
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                         &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("Guita", "Main");
        status = app.exec();
    }
    // Drops the transport if the engine never created the connection
    ApiConnection::configure({});
    return status;
}
