#pragma once

#include <QObject>
#include <QString>

class QTcpServer;

class AuthServer : public QObject {
    Q_OBJECT
   public:
    explicit AuthServer(QObject* parent = nullptr);
    ~AuthServer() override;

    quint16 port();
    static AuthServer* instance();
    static void registerUser(const QString& uuid, const QString& name);
    static QString getUserName(const QString& uuid);
    static QString getUserUuid(const QString& name);
    static QString offlinePlayerUuid(const QString& username);

   private slots:
    void newConnection();

   private:
    QTcpServer* m_tcpServer = nullptr;
    static AuthServer* s_instance;
};
