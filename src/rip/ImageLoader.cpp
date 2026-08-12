#include "ImageLoader.h"
#include "MagickCompatibility.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <Magick++.h>

#include <QTemporaryFile>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QUrl>
#include <QByteArray>
#include <QDebug>

using namespace Magick;


// Initializes supported extensions.
ImageLoader::ImageLoader(QObject *parent) : QObject(parent) {
    supportedExtensions = { ".jpeg", ".jpg", ".png", ".bmp", ".tiff", ".tif", ".svg", ".pdf" };
}


// Preview image for PDF files
QString ImageLoader::renderPdfToPreviewImage(const QString& pdfPath) {
    try {
        Magick::Image image;

        // Read first page of PDF
        image.read(pdfPath.toStdString() + "[0]");
        image.density("150");
        image.quality(90);
        image.backgroundColor("white");
        image.alphaChannel(Magick::RemoveAlphaChannel);  // Remove transparency

        // Downscale for preview
        const int maxWidth = 1000;
        if (image.columns() > maxWidth) {
            double scale = static_cast<double>(maxWidth) / image.columns();
            image.resize(Magick::Geometry(maxWidth, static_cast<int>(image.rows() * scale)));
        }

        QTemporaryFile tempFile(QDir::tempPath() + "/pdf_preview_XXXXXX.png");
        tempFile.setAutoRemove(false);
        if (!tempFile.open()) return "";

        QString tempPath = tempFile.fileName();
        image.write(tempPath.toStdString());
        return tempPath;
    } catch (const Magick::Exception &err) {
        qWarning() << "Failed to render PDF preview:" << err.what();
        return "";
    }
}


// Delete Temporary files used to display a Preview Image for PDFs
void ImageLoader::deleteTemporaryFile(const QString& path) {
    QFile temp(path);
    if (temp.exists()) {
        temp.remove();
        qDebug() << "Temporary preview deleted:" << path;
    }
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
    QString ext = getFileExtension(path).toLower();
    return (ext == "svg") ? inspectSvgOrPdf(path) : inspectImage(path);
}


// Validate that a file exists and is readable by its format type
bool ImageLoader::validateFile(const QString &path) {
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QString ext = getFileExtension(localPath).toLower();
    qDebug() << "Validating file:" << localPath << "with extension:" << ext;

    if (ext == "jpeg" || ext == "jpg" || ext == "png" || ext == "bmp") {
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
    
else if (ext == "tif" || ext == "tiff" || ext == "pdf") {
        // Use Magick++ for TIFF and PDF
        if (!QFile::exists(localPath)) {
            qWarning() << "File does not exist:" << localPath;
            return false;
        }

        try {
            Image image;
            image.read(localPath.toStdString() + (ext == "pdf" ? "[0]" : ""));
            qDebug() << (ext == "pdf" ? "PDF" : "TIFF")
                     << "image loaded with dimensions:"
                     << image.columns() << "x" << image.rows()
                     << "and color space:" << static_cast<int>(image.colorSpace());
            return true;
        } catch (const Magick::Exception &error) {
            qWarning() << "Magick++ failed to load" << ext << "image:" << error.what();
            return false;
        }
    }
    
    else if (ext == "svg") {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open SVG file:" << localPath;
            return false;
        }
        QTextStream in(&file);
        QString header = in.readLine();
        bool isValid = header.contains("<svg");
        qDebug() << "SVG header check passed:" << isValid;
        return isValid;
    }

    qWarning() << "Unsupported extension:" << ext;
    return false;
}


// Extract metadata (dimensions, format hints) from image files
QVariantMap ImageLoader::inspectImage(const QString &path) {
    QVariantMap meta;
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QString ext = getFileExtension(localPath).toLower();
    QFileInfo info(localPath);
    meta["name"] = info.fileName();
    meta["size"] = info.size();
    meta["extension"] = "." + ext;

    // Use Magick++ for PDF and TIFF
    if (ext == "tiff" || ext == "tif" || ext == "pdf") {
        try {
            Image image;
            image.read(localPath.toStdString() + (ext == "pdf" ? "[0]" : ""));
            meta["width"] = static_cast<int>(image.columns());
            meta["height"] = static_cast<int>(image.rows());
            
            // Estimate channels based on color space
	    int channelCount = (MagickCompatibility::hasAlphaChannel(image) ? 4 : 3);
            if (image.colorSpace() == Magick::CMYKColorspace) {
		channelCount = 4;
	    }
 	    
 	    meta["channels"] = channelCount;

	   // Convert color space enum to string
	    QString colorSpaceName;
	    switch (image.colorSpace()) {
	    	case Magick::RGBColorspace:        colorSpaceName = "RGB"; break;
	    	case Magick::CMYKColorspace:       colorSpaceName = "CMYK"; break;
	    	case Magick::GRAYColorspace:       colorSpaceName = "Grayscale"; break;
	    	case Magick::LabColorspace:        colorSpaceName = "Lab"; break;
	    	case Magick::YCbCrColorspace:      colorSpaceName = "YCbCr"; break;
	    	case Magick::Rec601YCbCrColorspace:colorSpaceName = "Rec601 YCbCr"; break;
	    	default:                           colorSpaceName = "Unknown"; break;
	    }
	    
	    meta["colorProfile"] = colorSpaceName;

        } catch (const Magick::Exception &error) {
            qWarning() << "Failed to extract metadata from" << ext << ":" << error.what();
        }
        return meta;
    }

    // Use STB for standard formats
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return meta;
    QByteArray imageData = file.readAll();

    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(imageData.constData()),
        imageData.size(), &w, &h, &c, 0);

    if (!data) return meta;
    stbi_image_free(data);

    meta["width"] = w;
    meta["height"] = h;
    meta["channels"] = c;

    // Color profile inference via heuristic
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
