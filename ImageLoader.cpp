#include "ImageLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QUrl>
#include <QByteArray>
#include <QDebug>


/*******************************************************************
    ImageLoader constructor, Initializes supported file extensions.
*******************************************************************/
ImageLoader::ImageLoader(QObject *parent) : QObject(parent) {
    supportedExtensions = { ".jpeg", ".jpg", ".png", ".bmp", ".tiff", ".tif", ".svg", ".pdf" };
}


// Extract and normalize the file extension from the given path
QString ImageLoader::getFileExtension(const QString &path) {
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    return QFileInfo(localPath).suffix().toLower();
}


// Check whether the file extension is supported
bool ImageLoader::isSupportedExtension(const QString &path) {
    return supportedExtensions.contains("." + getFileExtension(path));
}


// Extract metadata for supported image, SVG, or PDF files
QVariantMap ImageLoader::extractMetadata(const QString &path) {
    QString ext = getFileExtension(path);
    return (ext == "svg" || ext == "pdf") ? inspectSvgOrPdf(path) : inspectImage(path);
}


// Validate that a file exists and is readable by its format type
bool ImageLoader::validateFile(const QString &path) {
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QString ext = getFileExtension(localPath);
    qDebug() << "Validating file:" << localPath << "with extension:" << ext;

    if (ext == "jpeg" || ext == "jpg" || ext == "png" || ext == "bmp" || ext == "tiff" || ext == "tif") {
        QFile file(localPath);
        if (!file.exists()) {
            qWarning() << "File does not exist:" << localPath;
            return false;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Failed to open file:" << localPath;
            return false;
        }

        QByteArray imageData = file.readAll();
        qDebug() << "Read" << imageData.size() << "bytes from file.";

        int w = 0, h = 0, c = 0;
        unsigned char* data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(imageData.constData()),
            imageData.size(), &w, &h, &c, 0);

        if (data) {
            qDebug() << "Image loaded successfully with dimensions:" << w << "x" << h << "and channels:" << c;
            stbi_image_free(data);
            return true;
        }
        else {
            qWarning() << "stbi_load_from_memory failed:" << stbi_failure_reason();
            return false;
        }
    }
    else if (ext == "svg" || ext == "pdf") {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open text-based file:" << localPath;
            return false;
        }
        QTextStream in(&file);
        QString header = in.readLine();
        qDebug() << "Header line:" << header;

        bool isValid = (ext == "svg" && header.contains("<svg")) || (ext == "pdf" && header.contains("%PDF"));
        qDebug() << "Header check passed:" << isValid;
        return isValid;
    }

    qWarning() << "Unsupported extension:" << ext;
    return false;
}


// Extract metadata (dimensions, format hints) from bitmap image files
QVariantMap ImageLoader::inspectImage(const QString &path) {
    QVariantMap meta;
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QFileInfo info(localPath);
    QFile file(localPath);

    if (!file.open(QIODevice::ReadOnly)) return meta;
    QByteArray imageData = file.readAll();

    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(imageData.constData()),
        imageData.size(), &w, &h, &c, 0);

    if (!data) return meta;
    stbi_image_free(data);

    // Basic metadata
    meta["name"] = info.fileName();
    meta["size"] = info.size();
    meta["width"] = w;
    meta["height"] = h;
    meta["channels"] = c;
    meta["extension"] = "." + info.suffix().toLower();

    // Attempt to infer color profile from content
    QString content(imageData);
    QString profile = "Unknown";

    const QStringList hints = {
        "sRGB", "Adobe RGB", "CMYK", "Cyan", "Magenta", "Yellow", "Black",
        "Ic", "Im", "IndexColor", "Indexed", "Palette", "8-color", "16-color",
        "YCbCr", "LAB", "XYZ", "Gray", "Grayscale", "Mono"
    };

    for (const QString& hint : hints) {
        if (content.contains(hint, Qt::CaseInsensitive)) {
            profile = hint;
            break;
        }
    }

    meta["colorProfile"] = profile;

    return meta;
}


// Extract metadata from vector/PDF files (name, size, basic format)
QVariantMap ImageLoader::inspectSvgOrPdf(const QString &path) {
    QVariantMap meta;
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QFileInfo info(localPath);
    QFile file(localPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return meta;

    QTextStream in(&file);
    QString header = in.readLine();

    meta["name"] = info.fileName();
    meta["size"] = info.size();
    meta["extension"] = "." + info.suffix().toLower();

    if (header.contains("<svg"))
        meta["format"] = "SVG";
    else if (header.contains("%PDF"))
        meta["format"] = "PDF";

    return meta;
}
