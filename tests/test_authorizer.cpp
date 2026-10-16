#include <backend/api/Authorizer.h>
#include <QtTest>

// tokenAuthorizer: the shared Bearer token of standalone mode and of the
// server during development.
class AuthorizerTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsOnlyTheExpectedBearerToken_data() {
        QTest::addColumn<QString>("header");
        QTest::addColumn<bool>("accepted");

        QTest::newRow("right token") << "Bearer s3cret" << true;
        QTest::newRow("wrong token") << "Bearer other" << false;
        QTest::newRow("no header") << "" << false;
        QTest::newRow("token without scheme") << "s3cret" << false;
        QTest::newRow("other scheme") << "Basic s3cret" << false;
        QTest::newRow("token prefix") << "Bearer s3cre" << false;
        QTest::newRow("token with suffix") << "Bearer s3cretX" << false;
    }

    void acceptsOnlyTheExpectedBearerToken() {
        QFETCH(QString, header);
        QFETCH(bool, accepted);
        QCOMPARE(tokenAuthorizer("s3cret")(header.toStdString()), accepted);
    }

    void emptyTokenAcceptsEverything() {
        const auto authorize = tokenAuthorizer("");
        QVERIFY(authorize(""));
        QVERIFY(authorize("Bearer anything"));
    }
};

QTEST_GUILESS_MAIN(AuthorizerTest)
#include "test_authorizer.moc"
