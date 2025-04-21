//ImageLoader.h
#include <QObject>
#include <QString>
#include <QVariantMap>

class ImageLoader : public QObject {
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);

    Q_INVOKABLE bool validateFile(const QString &path);
    Q_INVOKABLE QVariantMap extractMetadata(const QString &path);
    Q_INVOKABLE bool isSupportedExtension(const QString &path);

private:
    QStringList supportedExtensions;

    QString getFileExtension(const QString &path);
    QVariantMap inspectImage(const QString &path);
    QVariantMap inspectSvgOrPdf(const QString &path);
};
