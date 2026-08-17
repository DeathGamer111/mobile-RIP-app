#pragma once

#include "NocaiDirectPrintClient.h"

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QVariantMap>

class QLocalSocket;
class QTcpSocket;

class PrinterServiceServer : public QObject
{
    Q_OBJECT

public:
    explicit PrinterServiceServer(QObject* parent = nullptr);

    bool listen(const QString& socketName, QString* errorMessage = nullptr);
    bool listenTcp(const QHostAddress& address, quint16 port,
                   QString* errorMessage = nullptr);
    QString socketName() const;
    quint16 tcpPort() const;

private slots:
    void acceptConnections();
    void acceptTcpConnections();
    void readClient();
    void readTcpClient();
    void removeClient();
    void removeTcpClient();

private:
    QVariantMap handleRequest(const QVariantMap& request);
    QVariantMap serviceState();
    QVariantMap response(bool ok, const QVariant& result = {},
                         const QString& errorMessage = {});
    void finishRequest(QLocalSocket* socket, const QVariantMap& result);
    void finishTcpRequest(QTcpSocket* socket, const QVariantMap& result);

    QLocalServer m_server;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QTcpServer m_tcpServer;
    QHash<QTcpSocket*, QByteArray> m_tcpBuffers;
    NocaiDirectPrintClient m_backend;
};
