#include "ImageEditor.h"
#include <QDebug>
#include <QUrl>
#include <QFile>

using namespace Magick;


ImageEditor::ImageEditor(QObject *parent) : QObject(parent) {
    InitializeMagick(nullptr);
}


bool ImageEditor::loadImage(const QString &path) {
    try {
        QString localPath = QUrl(path).toLocalFile();
        m_image.read(localPath.toStdString());
        m_imageLoaded = true;
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Failed to load image:" << e.what();
        m_imageLoaded = false;
        return false;
    }
}


bool ImageEditor::saveImage(const QString &outputPath) {
    if (!m_imageLoaded) {
        qWarning() << "No image loaded.";
        return false;
    }
    try {
        QString localPath = QUrl(outputPath).toLocalFile();
        m_image.write(localPath.toStdString());
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Failed to save image:" << e.what();
        return false;
    }
}


bool ImageEditor::deleteFile(const QString &path) {
    QString localPath = QUrl(path).toLocalFile();

    if (QFile::exists(localPath)) {
        if (QFile::remove(localPath)) {
            qDebug() << "Deleted file:" << localPath;
            return true;
        } else {
            qWarning() << "Failed to delete file:" << localPath;
            return false;
        }
    } else {
        qWarning() << "File does not exist:" << localPath;
        return false;
    }
}


bool ImageEditor::convertColorSpace(const QString &targetSpace) {
    if (!m_imageLoaded) return false;

    try {
        if (targetSpace == "Gray") {
            m_image.type(GrayscaleType);
        } else if (targetSpace == "CMYK") {
            m_image.colorSpace(MagickCore::CMYKColorspace);
        } else if (targetSpace == "RGB") {
            m_image.colorSpace(MagickCore::RGBColorspace);
        } else {
            qWarning() << "Unsupported color space:" << targetSpace;
            return false;
        }
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Color space conversion failed:" << e.what();
        return false;
    }
}


bool ImageEditor::resizeImage(int width, int height) {
    if (!m_imageLoaded) return false;

    try {
        m_image.resize(Geometry(width, height));
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Resize failed:" << e.what();
        return false;
    }
}


bool ImageEditor::rotate(double degrees) {
    if (!m_imageLoaded) return false;

    try {
        m_image.rotate(degrees);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::flip(const QString &direction) {
    if (!m_imageLoaded) return false;

    try {
        if (direction == "horizontal")
            m_image.flop();
        else if (direction == "vertical")
            m_image.flip();
        else
            return false;
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::crop(int x, int y, int width, int height) {
    if (!m_imageLoaded) return false;

    try {
        m_image.crop(Magick::Geometry(width, height, x, y));
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::adjustBrightnessContrast(int brightness, int contrast) {
    if (!m_imageLoaded) return false;

    try {
        double b = 100.0 + brightness;
        double s = 100.0;               // keep saturation constant
        double h = 100.0 + contrast;    // simulate contrast via hue adjustment (optional)
        m_image.modulate(b, s, h);
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Brightness/Contrast adjustment failed:" << e.what();
        return false;
    }
}


bool ImageEditor::resizeToOriginal() {
    if (!m_imageLoaded) return false;
    try {
        Geometry originalSize(m_image.baseColumns(), m_image.baseRows());
        m_image.resize(originalSize);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::resizeToHalf() {
    if (!m_imageLoaded) return false;
    try {
        m_image.resize(Geometry(m_image.columns() / 2, m_image.rows() / 2));
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::resizeToDouble() {
    if (!m_imageLoaded) return false;
    try {
        m_image.resize(Geometry(m_image.columns() * 2, m_image.rows() * 2));
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::adjustHue(int hue) {
    if (!m_imageLoaded) return false;
    try {
        m_image.modulate(100.0, 100.0, 100.0 + hue);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::adjustSaturation(int saturation) {
    if (!m_imageLoaded) return false;
    try {
        m_image.modulate(100.0, 100.0 + saturation, 100.0);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::adjustGamma(double gamma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.gamma(gamma);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::sharpenImage(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.sharpen(radius, sigma);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::applyBlur(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.blur(radius, sigma);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::applySepia(double threshold) {
    if (!m_imageLoaded) return false;
    try {
        m_image.sepiaTone(threshold);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::applyVignette() {
    if (!m_imageLoaded) return false;
    try {
        m_image.vignette();
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::applySwirl(double degrees) {
    if (!m_imageLoaded) return false;
    try {
        m_image.swirl(degrees);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::applyImplode(double factor) {
    if (!m_imageLoaded) return false;
    try {
        m_image.implode(factor);
        return true;
    } catch (...) {
        return false;
    }
}


bool ImageEditor::drawText(const QString &text, int x, int y) {
    if (!m_imageLoaded) return false;

    try {
        DrawableList drawList;
        drawList.push_back(DrawableFont("Arial"));
        drawList.push_back(DrawablePointSize(24));
        drawList.push_back(DrawableText(x, y, text.toStdString()));
        drawList.push_back(DrawableFillColor("white"));
        m_image.draw(drawList);
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Draw text failed:" << e.what();
        return false;
    }
}


bool ImageEditor::drawRectangle(int x, int y, int w, int h) {
    if (!m_imageLoaded) return false;

    try {
        Magick::DrawableList drawList;
        drawList.push_back(Magick::DrawableStrokeColor("red"));
        drawList.push_back(Magick::DrawableFillColor("none"));
        drawList.push_back(Magick::DrawableRectangle(x, y, x + w, y + h));
        m_image.draw(drawList);
        return true;
    } catch (...) {
        return false;
    }
}
