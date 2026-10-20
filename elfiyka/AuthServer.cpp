#include "AuthServer.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUuid>

AuthServer* AuthServer::s_instance = nullptr;
static QMap<QString, QString> s_users, s_uuids;
static QMutex s_mutex;

static QString norm(const QString& s) { return QString(s).remove('-').toLower(); }

AuthServer::AuthServer(QObject* parent) : QObject(parent), m_tcpServer(new QTcpServer(this)) {
    {
        QMutexLocker lock(&s_mutex);
        s_instance = this;
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &AuthServer::newConnection);
    m_tcpServer->listen(QHostAddress::LocalHost);
}

AuthServer::~AuthServer() {
    QMutexLocker lock(&s_mutex);
    if (s_instance == this) s_instance = nullptr;
}

AuthServer* AuthServer::instance() {
    QMutexLocker lock(&s_mutex);
    return s_instance ? s_instance : (s_instance = new AuthServer(qApp));
}

quint16 AuthServer::port() {
    return m_tcpServer ? m_tcpServer->serverPort() : 0;
}

void AuthServer::registerUser(const QString& uuid, const QString& name) {
    if (uuid.isEmpty() || name.isEmpty()) return;
    QMutexLocker lock(&s_mutex);
    s_users[uuid] = s_users[uuid.toLower()] = s_users[norm(uuid)] = name;
    s_uuids[name.toLower()] = norm(uuid);
}

QString AuthServer::getUserName(const QString& uuid) {
    QMutexLocker lock(&s_mutex);
    QString n = s_users.value(uuid);
    return !n.isEmpty() ? n : s_users.value(norm(uuid));
}

QString AuthServer::getUserUuid(const QString& name) {
    QMutexLocker lock(&s_mutex);
    return s_uuids.value(name.toLower());
}

QString AuthServer::offlinePlayerUuid(const QString& username) {
    auto d = QCryptographicHash::hash(("OfflinePlayer:" + username).toUtf8(), QCryptographicHash::Md5);
    d[6] = static_cast<char>((d[6] & 0x0f) | 0x30);
    d[8] = static_cast<char>((d[8] & 0x3f) | 0x80);
    return QUuid::fromRfc4122(d).toString(QUuid::Id128);
}

void AuthServer::newConnection() {
    QTcpSocket* s = m_tcpServer->nextPendingConnection();
    if (!s) return;
    connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    auto buf = std::make_shared<QByteArray>();
    connect(s, &QTcpSocket::readyRead, s, [this, s, buf]() {
        buf->append(s->readAll());
        int hEnd = buf->indexOf("\r\n\r\n");
        if (hEnd == -1) return;
        int clIdx = buf->toLower().indexOf("content-length:");
        int bLen = 0;
        if (clIdx != -1 && clIdx < hEnd) {
            bLen = qMax(0, buf->mid(clIdx + 15, buf->indexOf("\r\n", clIdx) - (clIdx + 15)).trimmed().toInt());
        }
        if (buf->size() < hEnd + 4 + bLen) return;
        disconnect(s, &QTcpSocket::readyRead, nullptr, nullptr);

        QString fullUrl = QString::fromUtf8(buf->left(buf->indexOf("\r\n"))).section(' ', 1, 1);
        QString path = fullUrl.section('?', 0, 0);
        if (path.startsWith("http://") || path.startsWith("https://")) {
            path = QUrl(path).path();
        }
        while (path.endsWith('/') && path.length() > 1) path.chop(1);
        QByteArray body = buf->mid(hEnd + 4, bLen);

        int status = 200;
        QByteArray resBody, extraHdr;
        if (path == "" || path == "/" || path == "/auth" || path == "/authserver") {
            resBody = "{\"Status\":\"OK\",\"Application-Owner\":\"Mojang\",\"meta\":{\"serverName\":\"Prism Launcher\"},\"skinDomains\":[]}";
            extraHdr = "X-Authlib-Injector-API-Location: /auth/\r\n";
        } else if (path.endsWith("/authenticate") || path.endsWith("/refresh")) {
            auto obj = QJsonDocument::fromJson(body).object();
            QString token = obj.value("clientToken").toString();
            QString user = obj.value("username").toString();
            if (user.isEmpty() && obj["selectedProfile"].isObject())
                user = obj["selectedProfile"].toObject().value("name").toString();
            if (user.isEmpty()) {
                QString tok = obj.value("accessToken").toString();
                user = getUserName(tok);
                if (user.isEmpty()) user = tok;
            }
            if (user.isEmpty()) user = "Player";

            QString uuid = getUserUuid(user);
            if (uuid.isEmpty()) {
                if (obj["selectedProfile"].isObject() && obj["selectedProfile"].toObject().contains("id"))
                    uuid = obj["selectedProfile"].toObject().value("id").toString();
                else if (!token.isEmpty())
                    uuid = token;
                else
                    uuid = offlinePlayerUuid(user);
            }
            if (token.isEmpty()) token = uuid;

            registerUser(uuid, user);
            if (token != uuid) {
                QMutexLocker lock(&s_mutex);
                s_users[token] = s_users[token.toLower()] = s_users[norm(token)] = user;
            }

            QString prof = QString("{\"id\":\"%1\",\"name\":\"%2\"}").arg(uuid, user);
            resBody = QString("{\"accessToken\":\"%1\",\"clientToken\":\"%2\",\"availableProfiles\":[%3],\"selectedProfile\":%3}")
                          .arg(user, token, prof).toUtf8();
        } else if (path.endsWith("/validate") || path.endsWith("/invalidate") || path.endsWith("/signout") || path.endsWith("/join")) {
            status = 204;
        } else if (path.contains("/profile")) {
            QString rawUuid = path.section('/', -1);
            if (rawUuid == "profile" || rawUuid.isEmpty()) {
                status = 400;
                resBody = "Bad Request";
            } else {
                QString name = getUserName(rawUuid);
                resBody = QString("{\"id\":\"%1\",\"name\":\"%2\",\"properties\":[]}")
                              .arg(QString(rawUuid).remove('-'), name.isEmpty() ? "Player" : name).toUtf8();
            }
        } else if (path.endsWith("/hasJoined")) {
            QUrl url(fullUrl.startsWith('/') ? ("http://localhost" + fullUrl) : fullUrl);
            QString user = QUrlQuery(url).queryItemValue("username");
            if (user.isEmpty()) {
                status = 204;
            } else {
                QString uuid = getUserUuid(user);
                if (uuid.isEmpty()) uuid = offlinePlayerUuid(user);
                registerUser(uuid, user);
                resBody = QString("{\"id\":\"%1\",\"name\":\"%2\",\"properties\":[]}").arg(uuid, user).toUtf8();
            }
        } else {
            status = 404;
            resBody = "Not found";
        }

        QString statusText = status == 200 ? "OK" : status == 204 ? "No Content" : status == 400 ? "Bad Request" : "Not Found";
        s->write(QString("HTTP/1.1 %1 %2\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %3\r\nConnection: close\r\n%4\r\n")
                     .arg(status).arg(statusText).arg(resBody.size()).arg(QString::fromUtf8(extraHdr)).toUtf8() + resBody);
        s->disconnectFromHost();
    });
}
