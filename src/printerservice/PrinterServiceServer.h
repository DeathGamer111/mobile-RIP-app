#pragma once

#include "NocaiDirectPrintClient.h"

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QLocalSocket;

class PrinterServiceServer : public QObject
{
    Q_OBJECT

public:
    explicit PrinterServiceServer(QObject* parent = nullptr);

    bool listen(const QString& socketName, QString* errorMessage = nullptr);
    QString socketName() const;

private slots:
    void acceptConnections();
    void readClient();
    void removeClient();

private:
    QVariantMap handleRequest(const QVariantMap& request);
    QVariantMap serviceState();
    QVariantMap response(bool ok, const QVariant& result = {},
                         const QString& errorMessage = {});
    void finishRequest(QLocalSocket* socket, const QVariantMap& result);

    QLocalServer m_server;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    NocaiDirectPrintClient m_backend;
};
