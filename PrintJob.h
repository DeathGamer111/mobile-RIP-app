// PrintJob.h
#include <QString>
#include <QPoint>
#include <QDateTime>
#include <QSize>

    struct PrintJob {
        QString id;
        QString name;
        QString imagePath;
        QSize paperSize;
        QSize resolution;
        QPoint offset;
        QString whiteStrategy;
        QString varnishType;
        QString colorProfile;
        QDateTime createdAt;
    };
