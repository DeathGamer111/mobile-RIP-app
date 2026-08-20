#include "ImageEditor.h"
#include "BoundedRasterPipeline.h"
#include "ImagePhysicalSize.h"
#include "MagickCompatibility.h"
#include <QDebug>
#include <QUrl>
#include <QFile>

#include <cmath>

using namespace Magick;


/*****************************************************
    ImageEditor constructor, Initializes ImageMagick.
*****************************************************/
ImageEditor::ImageEditor(QObject *parent) : QObject(parent) {
    BoundedRasterPipeline::configureImageMagickCache();
}


// Load an image from the provided file path
bool ImageEditor::loadImage(const QString &path) {
	try {
        QString localPath = QUrl(path).toLocalFile();
        m_image = Magick::Image();
        ImagePhysicalSize::setVectorReadDensity(m_image, localPath, 720, 720);
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


// Get Image Width
int ImageEditor::getImageWidth() const {
    return m_imageLoaded ? static_cast<int>(m_image.columns()) : 0;
}


// Get Image Height
int ImageEditor::getImageHeight() const {
    return m_imageLoaded ? static_cast<int>(m_image.rows()) : 0;
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
		pushUndoState();
        m_image.resize(Geometry(QString("%1x%2!").arg(width).arg(height).toStdString()));
        qDebug() << "Image Resized to:" << width << " x " << height;
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
   		pushUndoState();
        m_image.rotate(degrees);
        qDebug() << "Image Rotated by:" << degrees << "degrees";
        return true;
    } catch (...) {
        return false;
    }
}


// Flip the image horizontally or vertically
bool ImageEditor::flip(const QString &direction) {
    if (!m_imageLoaded) return false;

    try {
        if (direction == "horizontal") {
			pushUndoState();
            m_image.flop();
            qDebug() << "Image Flopped" << direction;
        } else if (direction == "vertical") {
    		pushUndoState();
            m_image.flip();
            qDebug() << "Image Flipped" << direction;
        } else {
            return false;
		}
        return true;
    } catch (...) {
        return false;
    }
}


// Crop the image to a specific rectangle (x, y, width, height)
bool ImageEditor::crop(int x, int y, int width, int height) {
    if (!m_imageLoaded) return false;

    try {
		pushUndoState();
        m_image.crop(Magick::Geometry(width, height, x, y));
        qDebug() << "Image Cropped to: x=" << x << ", y=" << y << ", width=" << width << ", height=" << height;
        return true;
    } catch (...) {
        return false;
    }
}


// Resize image to its original dimensions
bool ImageEditor::resizeToOriginal() {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        Geometry originalSize(m_image.baseColumns(), m_image.baseRows());
        m_image.resize(originalSize);
        qDebug() << "Image Resized to Original Size:" << m_image.baseColumns() << "x" << m_image.baseRows();
        return true;
    } catch (...) {
        return false;
    }
}


// Resize image to half its current size
bool ImageEditor::resizeToHalf() {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.resize(Geometry(m_image.columns() / 2, m_image.rows() / 2));
        qDebug() << "Image Resized to Half:" << m_image.columns()<< "x" << m_image.rows();
        return true;
    } catch (...) {
        return false;
    }
}


// Resize image to double its current size
bool ImageEditor::resizeToDouble() {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.resize(Geometry(m_image.columns() * 2, m_image.rows() * 2));
        qDebug() << "Image Resized to Double:" << m_image.columns() << "x" << m_image.rows();
        return true;
    } catch (...) {
        return false;
    }
}


// Adjust image brightness
bool ImageEditor::adjustBrightness(int brightness) {
    if (!m_imageLoaded) return false;

    try {
        pushUndoState();
        m_brightness = 100.0 + brightness;
        m_image.modulate(m_brightness, m_saturation, m_hue);
        qDebug() << "Brightness adjusted to:" << brightness;
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Brightness adjustment failed:" << e.what();
        return false;
    }
}


// Adjust image hue
bool ImageEditor::adjustHue(int hue) {
    if (!m_imageLoaded) return false;

    try {
        pushUndoState();
        m_hue = 100.0 + hue;
        m_image.modulate(m_brightness, m_saturation, m_hue);
        qDebug() << "Hue adjusted to:" << hue;
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Hue adjustment failed:" << e.what();
        return false;
    }
}


// Adjust image contrast using sigmoidal contrast
bool ImageEditor::adjustContrast(bool increase, double contrastAmount, double midpoint) {
    if (!m_imageLoaded) return false;

    try {
        pushUndoState();
        m_image.sigmoidalContrast(increase, contrastAmount, midpoint);
        qDebug() << "Sigmoidal Contrast adjusted:"
                 << (increase ? "increase" : "decrease")
                 << "amount=" << contrastAmount
                 << "midpoint=" << midpoint;
        return true;
    } catch (const Magick::Exception &e) {
        qWarning() << "Sigmoidal contrast adjustment failed:" << e.what();
        return false;
    }
}


// Adjust image saturation level
bool ImageEditor::adjustSaturation(int saturation) {
    if (!m_imageLoaded) return false;

    try {
        pushUndoState();
        m_saturation = 100.0 + saturation;
        m_image.modulate(m_brightness, m_saturation, m_hue);
        qDebug() << "Saturation adjusted to:" << saturation;
        return true;
    } catch (...) {
        return false;
    }
}


// Apply gamma correction
bool ImageEditor::adjustGamma(double gamma) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.gamma(gamma);    
		qDebug() << "Gamma adjusted to:" << gamma;
        return true;
    } catch (...) {
        return false;
    }
}


// Apply sharpening effect using radius and sigma
bool ImageEditor::sharpenImage(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.sharpen(radius, sigma);
		qDebug() << "Sharpen applied: radius=" << radius << ", sigma=" << sigma;
        return true;
    } catch (...) {
        return false;
    }
}


// Apply Gaussian blur using radius and sigma
bool ImageEditor::applyBlur(double radius, double sigma) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.blur(radius, sigma);
        qDebug() << "Blur applied: radius=" << radius << ", sigma=" << sigma;
        return true;
    } catch (...) {
        return false;
    }
}


// Apply sepia tone effect to image
bool ImageEditor::applySepia(double threshold) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.sepiaTone(threshold);
        qDebug() << "Sepia applied with threshold:" << threshold;
        return true;
    } catch (...) {
        return false;
    }
}


// Apply vignette effect
bool ImageEditor::applyVignette() {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.vignette();
        qDebug() << "Vignette applied";
        return true;
    } catch (...) {
        return false;
    }
}


// Apply swirl distortion effect
bool ImageEditor::applySwirl(double degrees) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.swirl(degrees);
        qDebug() << "Swirl applied:" << degrees << "degrees";
        return true;
    } catch (...) {
        return false;
    }
}


// Apply implode effect using a distortion factor
bool ImageEditor::applyImplode(double factor) {
    if (!m_imageLoaded) return false;
    try {
		pushUndoState();
        m_image.implode(factor);
        qDebug() << "Implode applied: factor=" << factor;
        return true;
    } catch (...) {
        return false;
    }
}


// Draw text on the image at specified coordinates
bool ImageEditor::drawText(const QString &text, int x, int y) {
    if (!m_imageLoaded) return false;

    try {
		pushUndoState();
        DrawableList drawList;
        drawList.push_back(DrawableFont("Arial"));
        drawList.push_back(DrawablePointSize(24));
        drawList.push_back(DrawableText(x, y, text.toStdString()));
        drawList.push_back(DrawableFillColor("white"));
        m_image.draw(drawList);
        qDebug() << "Text drawn:" << "\"" + text + "\"" << "at (" << x << "," << y << ")";
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
		pushUndoState();
        Magick::DrawableList drawList;
        drawList.push_back(Magick::DrawableStrokeColor("red"));
        drawList.push_back(Magick::DrawableFillColor("none"));
        drawList.push_back(Magick::DrawableRectangle(x, y, x + w, y + h));
        m_image.draw(drawList);
        qDebug() << "Rectangle drawn at: x=" << x << ", y=" << y << ", w=" << w << ", h=" << h;
        return true;
    } catch (...) {
        return false;
    }
}


// Private Helper Function to handle State Changes
void ImageEditor::pushUndoState() {
    if (m_imageLoaded) {
        m_undoStack.push_back(m_image);  // Save current state
        m_redoStack.clear();             // Clear redo stack on new action
        qDebug() << "Undo state pushed. Undo stack size:" << m_undoStack.size();
    }
}


// Function to undo edit changes
bool ImageEditor::undo() {
    if (!m_undoStack.empty()) {
        m_redoStack.push_back(m_image);  // Save current for redo
        m_image = m_undoStack.back();    // Restore previous
        m_undoStack.pop_back();
        qDebug() << "Undo applied. Current size:" << m_image.columns() << "x" << m_image.rows();
        return true;
    }
    qDebug() << "Undo stack empty.";
    return false;
}


// Function to Redo last Undo change
bool ImageEditor::redo() {
    if (!m_redoStack.empty()) {
        m_undoStack.push_back(m_image);  // Save current for undo
        m_image = m_redoStack.back();    // Restore redo
        m_redoStack.pop_back();
        qDebug() << "Redo applied. Current size:" << m_image.columns() << "x" << m_image.rows();
        return true;
    }
    qDebug() << "Redo stack empty.";
    return false;
}


// Clears Undo/Redo Stack
void ImageEditor::clearUndoRedoStacks() {
    m_undoStack.clear();
    m_redoStack.clear();
    qDebug() << "Undo and Redo stacks cleared.";
}


// Apply Edits made in the ImpositionView
bool ImageEditor::applyImpositionEdits(const QString &imagePath, int offsetX, int offsetY, QSize paperSize, const QVariantMap &overlayData, double fallbackInputDpi) {
    QString localPath = QUrl(imagePath).toLocalFile();
    if (!QFile::exists(localPath)) {
        qWarning() << "Original image not found:" << localPath;
        return false;
    }

    if (paperSize.width() <= 0 || paperSize.height() <= 0) {
        qWarning() << "Invalid media size provided.";
        return false;
    }

    if (!loadImage(imagePath)) {
        qWarning() << "Failed to load image for imposition edits.";
        return false;
    }

    try {
        const int dpi = 720;
        const int canvasW = static_cast<int>(paperSize.width() / 25.4 * dpi);
        const int canvasH = static_cast<int>(paperSize.height() / 25.4 * dpi);

        const ImagePhysicalSize::Density inputDensity =
            ImagePhysicalSize::resolvedDensity(
                m_image, fallbackInputDpi, fallbackInputDpi);
        const QSize imposedImageSize = ImagePhysicalSize::outputPixelSize(
            m_image.columns(), m_image.rows(), inputDensity, dpi, dpi);
        if (!imposedImageSize.isValid()) {
            qWarning() << "Could not resolve imposed image dimensions.";
            return false;
        }
        if (imposedImageSize.width() != static_cast<int>(m_image.columns()) ||
            imposedImageSize.height() != static_cast<int>(m_image.rows())) {
            m_image.resize(Geometry(
                imposedImageSize.width(), imposedImageSize.height()));
        }
        m_image.resolutionUnits(PixelsPerInchResolution);
        MagickCompatibility::setDensity(m_image, dpi, dpi);

        const auto mmToPixels = [dpi](double mm) {
            return static_cast<int>(std::lround(mm * dpi / 25.4));
        };

        // Create blank canvas
        Image canvas(Geometry(canvasW, canvasH), Color("white"));
        canvas.resolutionUnits(PixelsPerInchResolution);
        MagickCompatibility::setDensity(canvas, dpi, dpi);
        MagickCompatibility::setAlphaChannel(m_image, true);
        canvas.composite(m_image, mmToPixels(offsetX), mmToPixels(offsetY), OverCompositeOp);

        // === Optional: Draw Text ===
        if (overlayData.contains("text") && !overlayData["text"].toString().isEmpty()) {
            const QString text = overlayData["text"].toString();
            const int textX = mmToPixels(overlayData["textX"].toDouble());
            const int textY = mmToPixels(overlayData["textY"].toDouble());

            DrawableList drawText;
            drawText.push_back(DrawableFont("Helvetica"));
            drawText.push_back(DrawablePointSize(36));
            drawText.push_back(DrawableFillColor("blue"));
            drawText.push_back(DrawableText(textX, textY, text.toStdString()));
            canvas.draw(drawText);
        }

        // === Optional: Draw Rectangle ===
        if (overlayData.contains("drawRect") && overlayData["drawRect"].toBool()) {
            const int rx = mmToPixels(overlayData["rectX"].toDouble());
            const int ry = mmToPixels(overlayData["rectY"].toDouble());
            const int rw = mmToPixels(overlayData["rectW"].toDouble());
            const int rh = mmToPixels(overlayData["rectH"].toDouble());

            DrawableList drawRect;
            drawRect.push_back(DrawableStrokeColor("red"));
            drawRect.push_back(DrawableFillColor("none"));
            drawRect.push_back(DrawableStrokeWidth(2));
            drawRect.push_back(DrawableRectangle(rx, ry, rx + rw, ry + rh));
            canvas.draw(drawRect);
        }

        m_image = canvas;

        // Save new image
        QFileInfo info(localPath);
        QString newFileName = info.completeBaseName() + "_imposed." + info.suffix();
        QString newPath = info.absolutePath() + "/" + newFileName;

        if (!saveImage(QUrl::fromLocalFile(newPath).toString())) {
            qWarning() << "Failed to save imposed image:" << newPath;
            return false;
        }

        qDebug() << "Imposition edits saved with canvas to:" << newPath;
        clearUndoRedoStacks();
        return true;

    } catch (const Magick::Exception &e) {
        qWarning() << "Error during canvas composite:" << e.what();
        return false;
    }
}
