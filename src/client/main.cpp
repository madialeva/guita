#include <client/ApiConfig.h>
#include <client/ApiConnection.h>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

#ifdef APP_STANDALONE_SUPPORT
#include <backend/api/Backend.h>
#include <QRandomGenerator>
#include <exception>
#include <memory>

static QByteArray randomToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), 8);
    return bytes.toHex();
}
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
        "Runs the backend inside the process (127.0.0.1, random port and token)."));
    QCommandLineOption dbOption("db", QCoreApplication::translate("main",
        "PostgreSQL connection string in standalone mode (defaults to $DB_CONN)."), "conn");
    parser.addOptions({standaloneOption, dbOption});
#endif
    parser.process(app);

    // Web / connected modes: shared development token, if any
    ApiConnection::Settings settings{defaultApiBaseUrl(), qgetenv("API_TOKEN"),
#ifdef Q_OS_WASM
                                     ApiConnection::Web};
#else
                                     ApiConnection::Connected};
#endif

#ifdef APP_STANDALONE_SUPPORT
    // Destroyed when main returns, after the QML engine is gone
    std::unique_ptr<Backend> backend;

    if (parser.isSet(standaloneOption)) {
        const auto connection = parser.isSet(dbOption)
            ? parser.value(dbOption)
            : qEnvironmentVariable("DB_CONN", "host=127.0.0.1 dbname=guita user=guita password=guita");

        const auto token = randomToken();
        try {
            // Loopback only, free port chosen by the OS, no docroot: API only
            backend = std::make_unique<Backend>(BackendOptions{
                .connectionString = connection.toStdString(),
                .authorizer = tokenAuthorizer(token.toStdString()),
                .http = HttpOptions{},
            });
        } catch (const std::exception &e) {
            qCritical("Could not start the embedded backend: %s", e.what());
            return 1;
        }
        if (!backend->start()) return 1;

        settings = {QUrl(QStringLiteral("http://127.0.0.1:%1/api/").arg(backend->port())),
                    token, ApiConnection::Standalone};
    }
#endif

    ApiConnection::configure(std::move(settings));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Guita", "Main");
    return app.exec();
}
