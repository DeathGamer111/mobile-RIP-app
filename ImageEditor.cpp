#include "ImageEditor.h"
#include <QDebug>
#include <QUrl>
#include <QFile>

using namespace Magick;


/*****************************************************
    ImageEditor constructor, Initializes ImageMagick.
*****************************************************/
ImageEditor::ImageEditor(QObject *parent) : QObject(parent) {
    InitializeMagick(nullptr);
}


// Load an image from the provided file path
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


// Save the currently loaded image to a specified output path
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


// Delete a file from disk, used to delete the temporary image used in editing
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


// Resize the image to specified width and height
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


// Rotate image by a given number of degrees
bool ImageEditor::rotate(double degrees) {
    if (!m_imageLoaded) return false;

    try {
        m_image.rotate(degrees);
        return true;
    } catch (...) {
        return false;
    }
}


// Flip the image horizontally or vertically
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


// Crop the image to a specific rectangle (x, y, width, height)
bool ImageEditor::crop(int x, int y, int width, int height) {
    if (!m_imageLoaded) return false;

    try {
        m_image.crop(Magick::Geometry(width, height, x, y));
        return true;
    } catch (...) {
        return false;
    }
}


// Adjust image brightness and contrast
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


// Resize image to its original dimensions
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


// Resize image to half its current size
bool ImageEditor::resizeToHalf() {
    if (!m_imageLoaded) return false;
    try {
        m_image.resize(Geometry(m_image.columns() / 2, m_image.rows() / 2));
        return true;
    } catch (...) {
        return false;
    }
}


// Resize image to double its current size
bool ImageEditor::resizeToDouble() {
    if (!m_imageLoaded) return false;
    try {
        m_image.resize(Geometry(m_image.columns() * 2, m_image.rows() * 2));
        return true;
    } catch (...) {
        return false;
    }
}


// Adjust image hue level
bool ImageEditor::adjustHue(int hue) {
    if (!m_imageLoaded) return false;
    try {
        m_image.modulate(100.0, 100.0, 100.0 + hue);
        return true;
    } catch (...) {
        return false;
    }
}


// Adjust image saturation level
bool ImageEditor::adjustSaturation(int saturation) {
    if (!m_imageLoaded) return false;
    try {
        m_image.modulate(100.0, 100.0 + saturation, 100.0);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply gamma correction
bool ImageEditor::adjustGamma(double gamma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.gamma(gamma);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply sharpening effect using radius and sigma
bool ImageEditor::sharpenImage(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.sharpen(radius, sigma);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply Gaussian blur using radius and sigma
bool ImageEditor::applyBlur(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
        m_image.blur(radius, sigma);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply sepia tone effect to image
bool ImageEditor::applySepia(double threshold) {
    if (!m_imageLoaded) return false;
    try {
        m_image.sepiaTone(threshold);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply vignette effect
bool ImageEditor::applyVignette() {
    if (!m_imageLoaded) return false;
    try {
        m_image.vignette();
        return true;
    } catch (...) {
        return false;
    }
}


// Apply swirl distortion effect
bool ImageEditor::applySwirl(double degrees) {
    if (!m_imageLoaded) return false;
    try {
        m_image.swirl(degrees);
        return true;
    } catch (...) {
        return false;
    }
}


// Apply implode effect using a distortion factor
bool ImageEditor::applyImplode(double factor) {
    if (!m_imageLoaded) return false;
    try {
        m_image.implode(factor);
        return true;
    } catch (...) {
        return false;
    }
}


// Draw text on the image at specified coordinates
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


// Draw a rectangle at (x, y) with width and height
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
