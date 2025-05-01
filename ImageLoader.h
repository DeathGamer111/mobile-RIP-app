//ImageLoader.h
#include <QObject>
#include <QString>
#include <QVariantMap>


/****************************************************************************************
    ImageLoader handles validation and metadata extraction for image, SVG, and PDF files.
*****************************************************************************************/

class ImageLoader : public QObject {
    Q_OBJECT

public:
    explicit ImageLoader(QObject *parent = nullptr);

    Q_INVOKABLE bool validateFile(const QString &path);             // Validate if file is supported and loadable
    Q_INVOKABLE QVariantMap extractMetadata(const QString &path);   // Extract metadata from supported file
    Q_INVOKABLE bool isSupportedExtension(const QString &path);     // Check file extension support

private:
    QStringList supportedExtensions;                                // List of accepted file types

    // Internal helpers
    QString getFileExtension(const QString &path);                  // Extract extension from path
    QVariantMap inspectImage(const QString &path);                  // Metadata for bitmap images
    QVariantMap inspectSvgOrPdf(const QString &path);               // Metadata for vector/PDF formats
};
