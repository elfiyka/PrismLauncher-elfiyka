#include "AuthServer.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

class AuthServerTest : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase();
    void testRootEndpoint();
    void testRootEndpointWithQuery();
    void testAuthenticateEndpoint();
    void testRefreshEndpoint();
    void testProfileEndpoint();
    void testProfileEndpointTrailingSlash();
    void testProfileEndpointWithQuery();
    void testEmptyProfileLookup();
    void testJoinAndHasJoinedEndpoints();
    void testHasJoinedSuccess();
    void testLanPlayUnregisteredClientJoinAndProfileLookup();
    void testValidateEndpoint();
    void testUnknownEndpoint();
    void testChunkedPostRequest();
    void testLowercaseContentLengthHeader();
    void testUserRegistration();
    void testAuthserverPrefixAuthenticate();
    void testAuthserverRootAndMetadata();
    void testProfileUuidPreservationWithCustomClientToken();
    void testHasJoinedQueryVariations();
    void testSplitHeadersTcpStream();
    void testPostWithoutContentLength();
    void testStandardMinecraftOfflineUuidCalculation();
    void testRapidClientDisconnectLifecycle();
    void testAuthenticateFallbackStandardUuid();
    void cleanupTestCase() {}

   private:
    std::unique_ptr<AuthServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_nam;
    quint16 m_port = 0;

    QNetworkReply* sendRequest(const QString& method, const QString& path, const QByteArray& data = QByteArray()) {
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1%2").arg(m_port).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* r = (method == "POST") ? m_nam->post(req, data) : m_nam->get(req);
        QEventLoop loop;
        connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        return r;
    }

    QJsonObject getJson(const QString& path, int exp = 200, QNetworkReply** outR = nullptr) {
        auto r = sendRequest("GET", path);
        int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto doc = QJsonDocument::fromJson(r->readAll());
        if (outR) *outR = r; else r->deleteLater();
        return (status == exp) ? doc.object() : QJsonObject();
    }

    QJsonObject postJson(const QString& path, const QJsonObject& body, int exp = 200) {
        auto r = sendRequest("POST", path, QJsonDocument(body).toJson());
        int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto doc = QJsonDocument::fromJson(r->readAll());
        r->deleteLater();
        return (status == exp) ? doc.object() : QJsonObject();
    }

    QTcpSocket* connectTcp() {
        auto* s = new QTcpSocket(this);
        s->connectToHost("127.0.0.1", m_port);
        QEventLoop loop;
        connect(s, &QTcpSocket::connected, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        return s;
    }
};

void AuthServerTest::initTestCase() {
    m_server = std::make_unique<AuthServer>();
    m_port = m_server->port();
    QVERIFY(m_port > 0);
    m_nam = std::make_unique<QNetworkAccessManager>();
}

void AuthServerTest::testRootEndpoint() {
    QNetworkReply* r = nullptr;
    auto obj = getJson("/auth/", 200, &r);
    QCOMPARE(obj["Status"].toString(), "OK");
    QCOMPARE(obj["Application-Owner"].toString(), "Mojang");
    QCOMPARE(r->rawHeader("X-Authlib-Injector-API-Location"), QByteArray("/auth/"));
    r->deleteLater();
}

void AuthServerTest::testRootEndpointWithQuery() {
    QCOMPARE(getJson("/auth/?param=value", 200)["Status"].toString(), "OK");
}

void AuthServerTest::testAuthenticateEndpoint() {
    auto o = postJson("/auth/authenticate", { { "username", "Steve" }, { "clientToken", "test-uuid-steve-1234567890123456" } });
    QCOMPARE(o["accessToken"].toString(), "Steve");
    QCOMPARE(o["clientToken"].toString(), "test-uuid-steve-1234567890123456");
    QCOMPARE(o["selectedProfile"].toObject()["name"].toString(), "Steve");
    QCOMPARE(o["selectedProfile"].toObject()["id"].toString(), "test-uuid-steve-1234567890123456");
}

void AuthServerTest::testRefreshEndpoint() {
    auto o = postJson("/auth/refresh", { { "accessToken", "Steve" }, { "clientToken", "test-uuid-steve-1234567890123456" } });
    QCOMPARE(o["accessToken"].toString(), "Steve");
    QCOMPARE(o["clientToken"].toString(), "test-uuid-steve-1234567890123456");
}

void AuthServerTest::testProfileEndpoint() {
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/test-uuid-steve-1234567890123456")["name"].toString(), "Steve");
}

void AuthServerTest::testProfileEndpointTrailingSlash() {
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/test-uuid-steve-1234567890123456/")["name"].toString(), "Steve");
}

void AuthServerTest::testProfileEndpointWithQuery() {
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/test-uuid-steve-1234567890123456?unsigned=false")["name"].toString(), "Steve");
}

void AuthServerTest::testEmptyProfileLookup() {
    auto r = sendRequest("GET", "/sessionserver/session/minecraft/profile/");
    QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 400);
    r->deleteLater();
}

void AuthServerTest::testJoinAndHasJoinedEndpoints() {
    auto r1 = sendRequest("POST", "/sessionserver/session/minecraft/join");
    QCOMPARE(r1->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    r1->deleteLater();
    auto r2 = sendRequest("GET", "/sessionserver/session/minecraft/hasJoined");
    QCOMPARE(r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    r2->deleteLater();
}

void AuthServerTest::testHasJoinedSuccess() {
    auto o = getJson("/sessionserver/session/minecraft/hasJoined?username=LanPlayer&serverId=dummy-server-hash");
    QCOMPARE(o["name"].toString(), "LanPlayer");
    QVERIFY(!o["id"].toString().isEmpty());
}

void AuthServerTest::testLanPlayUnregisteredClientJoinAndProfileLookup() {
    auto o1 = getJson("/sessionserver/session/minecraft/hasJoined?username=LanClientBob&serverId=lan-secret-hash-99");
    QString uuid = o1["id"].toString();
    QCOMPARE(o1["name"].toString(), "LanClientBob");
    QCOMPARE(getJson(QString("/sessionserver/session/minecraft/profile/%1").arg(uuid))["name"].toString(), "LanClientBob");
}

void AuthServerTest::testValidateEndpoint() {
    auto r = sendRequest("POST", "/auth/validate");
    QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    r->deleteLater();
}

void AuthServerTest::testUnknownEndpoint() {
    auto r = sendRequest("GET", "/some/nonexistent/endpoint");
    QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    r->deleteLater();
}

void AuthServerTest::testChunkedPostRequest() {
    std::unique_ptr<QTcpSocket> s(connectTcp());
    QCOMPARE(s->state(), QAbstractSocket::ConnectedState);
    QByteArray body = "{\"username\":\"ChunkUser\",\"clientToken\":\"chunk-token\"}";
    s->write(QString("POST /auth/authenticate HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: %1\r\n\r\n")
                 .arg(body.size()).toUtf8());
    s->flush();
    QTest::qWait(30);
    s->write(body.left(10));
    s->flush();
    QTest::qWait(30);
    s->write(body.mid(10));
    s->flush();
    QEventLoop loop;
    connect(s.get(), &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(s->readAll().startsWith("HTTP/1.1 200 OK"));
}

void AuthServerTest::testLowercaseContentLengthHeader() {
    std::unique_ptr<QTcpSocket> s(connectTcp());
    QCOMPARE(s->state(), QAbstractSocket::ConnectedState);
    QByteArray body = "{\"username\":\"LowerUser\",\"clientToken\":\"lower-token\"}";
    s->write(QString("POST /auth/authenticate HTTP/1.1\r\nhost: 127.0.0.1\r\ncontent-type: application/json\r\ncontent-length: %1\r\n\r\n")
                 .arg(body.size()).toUtf8() + body);
    s->flush();
    QEventLoop loop;
    connect(s.get(), &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(s->readAll().startsWith("HTTP/1.1 200 OK"));
}

void AuthServerTest::testUserRegistration() {
    AuthServer::registerUser("99999999-8888-7777-6666-555555555555", "RegisteredUser");
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/99999999-8888-7777-6666-555555555555")["name"].toString(), "RegisteredUser");
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/99999999888877776666555555555555")["name"].toString(), "RegisteredUser");
}

void AuthServerTest::testAuthserverPrefixAuthenticate() {
    QCOMPARE(postJson("/authserver/authenticate", { { "username", "PrefixUser" } })["accessToken"].toString(), "PrefixUser");
    auto r1 = sendRequest("POST", "/authserver/refresh", "{\"accessToken\":\"PrefixUser\"}");
    QCOMPARE(r1->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    r1->deleteLater();
    auto r2 = sendRequest("POST", "/authserver/validate");
    QCOMPARE(r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 204);
    r2->deleteLater();
}

void AuthServerTest::testAuthserverRootAndMetadata() {
    QCOMPARE(getJson("/authserver/")["Status"].toString(), "OK");
}

void AuthServerTest::testProfileUuidPreservationWithCustomClientToken() {
    QString expClean = "11112222333344445555666677778888";
    AuthServer::registerUser("11112222-3333-4444-5555-666677778888", "CustomSteve");
    auto o = postJson("/auth/authenticate", { { "username", "CustomSteve" }, { "clientToken", "launcher-client-token-999" } });
    QCOMPARE(o["clientToken"].toString(), "launcher-client-token-999");
    QCOMPARE(o["selectedProfile"].toObject()["id"].toString(), expClean);
    QCOMPARE(getJson("/sessionserver/session/minecraft/hasJoined?username=CustomSteve&serverId=dummy")["id"].toString(), expClean);
}

void AuthServerTest::testHasJoinedQueryVariations() {
    auto o = getJson("/sessionserver/session/minecraft/hasJoined?serverId=serverhash123&username=Percent%20Player&ip=10.0.0.1");
    QCOMPARE(o["name"].toString(), "Percent Player");
}

void AuthServerTest::testSplitHeadersTcpStream() {
    std::unique_ptr<QTcpSocket> s(connectTcp());
    QCOMPARE(s->state(), QAbstractSocket::ConnectedState);
    s->write("POST /auth/authenticate HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    s->flush();
    QTest::qWait(50);
    QByteArray body = "{\"username\":\"SplitUser\",\"clientToken\":\"split-token-1\"}";
    s->write(QString("Content-Type: application/json\r\nContent-Length: %1\r\n\r\n").arg(body.size()).toUtf8() + body);
    s->flush();
    QEventLoop loop;
    connect(s.get(), &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray res = s->readAll();
    QVERIFY(res.startsWith("HTTP/1.1 200 OK"));
    QVERIFY(res.contains("\"accessToken\":\"SplitUser\""));
}

void AuthServerTest::testPostWithoutContentLength() {
    std::unique_ptr<QTcpSocket> s(connectTcp());
    QCOMPARE(s->state(), QAbstractSocket::ConnectedState);
    s->write("POST /auth/validate HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    s->flush();
    QEventLoop loop;
    connect(s.get(), &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(s->readAll().startsWith("HTTP/1.1 204 No Content"));
}

void AuthServerTest::testStandardMinecraftOfflineUuidCalculation() {
    QCOMPARE(AuthServer::offlinePlayerUuid("Steve"), "5627dd98e6be3c21b8a8e92344183641");
    QCOMPARE(AuthServer::offlinePlayerUuid("Alex"), "36532b5ec4423dbba24cc7e55d0f979a");
    auto o = getJson("/sessionserver/session/minecraft/hasJoined?username=Alex&serverId=lan-test-server-1");
    QCOMPARE(o["id"].toString(), "36532b5ec4423dbba24cc7e55d0f979a");
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/36532b5ec4423dbba24cc7e55d0f979a")["name"].toString(), "Alex");
    QCOMPARE(getJson("/sessionserver/session/minecraft/profile/36532B5EC4423DBBA24CC7E55D0F979A")["name"].toString(), "Alex");
}

void AuthServerTest::testRapidClientDisconnectLifecycle() {
    for (int i = 0; i < 10; ++i) {
        auto* s = connectTcp();
        if (s->state() == QAbstractSocket::ConnectedState) {
            if (i % 2 == 0) {
                s->write("POST /auth/authen");
                s->flush();
            }
            s->abort();
        }
        delete s;
    }
    QCOMPARE(getJson("/auth/")["Status"].toString(), "OK");
}

void AuthServerTest::testAuthenticateFallbackStandardUuid() {
    QString exp = AuthServer::offlinePlayerUuid("VanillaSteve");
    auto o = postJson("/auth/authenticate", { { "username", "VanillaSteve" } });
    QCOMPARE(o["accessToken"].toString(), "VanillaSteve");
    QCOMPARE(o["clientToken"].toString(), exp);
    QCOMPARE(o["selectedProfile"].toObject()["name"].toString(), "VanillaSteve");
    QCOMPARE(o["selectedProfile"].toObject()["id"].toString(), exp);
}

QTEST_MAIN(AuthServerTest)
#include "AuthServer_test.moc"
