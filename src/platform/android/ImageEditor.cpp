#include "ImageEditor.h"

#include <QFile>
#include <QImageReader>
#include <QUrl>
#include <QDebug>

namespace {
QString localPathFor(const QString& path)
{
    return path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)
        ? QUrl(path).toLocalFile()
        : path;
}

bool unavailable(const char* operation)
{
    qWarning() << operation << "requires the Android RIP/image-processing dependency build.";
    return false;
}
}

ImageEditor::ImageEditor(QObject* parent)
    : QObject(parent)
{
}

bool ImageEditor::loadImage(const QString& path)
{
    const QString localPath = localPathFor(path);
    QImageReader reader(localPath);
    if (!reader.canRead())
        return false;

    const QSize size = reader.size();
    m_imagePath = localPath;
    m_width = size.width();
    m_height = size.height();
    return true;
}

bool ImageEditor::saveImage(const QString& outputPath)
{
    Q_UNUSED(outputPath)
    return unavailable("Image save");
}

bool ImageEditor::deleteFile(const QString& path)
{
    return QFile::remove(localPathFor(path));
}

int ImageEditor::getImageWidth() const { return m_width; }
int ImageEditor::getImageHeight() const { return m_height; }

bool ImageEditor::rotate(double degrees) { Q_UNUSED(degrees) return unavailable("Rotate"); }
bool ImageEditor::flip(const QString& direction) { Q_UNUSED(direction) return unavailable("Flip"); }
bool ImageEditor::crop(int x, int y, int width, int height) { Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(width) Q_UNUSED(height) return unavailable("Crop"); }
bool ImageEditor::resizeImage(int width, int height) { Q_UNUSED(width) Q_UNUSED(height) return unavailable("Resize"); }
bool ImageEditor::resizeToOriginal() { return unavailable("Resize"); }
bool ImageEditor::resizeToHalf() { return unavailable("Resize"); }
bool ImageEditor::resizeToDouble() { return unavailable("Resize"); }
bool ImageEditor::adjustHue(int hue) { Q_UNUSED(hue) return unavailable("Hue adjustment"); }
bool ImageEditor::adjustSaturation(int saturation) { Q_UNUSED(saturation) return unavailable("Saturation adjustment"); }
bool ImageEditor::adjustBrightness(int brightness) { Q_UNUSED(brightness) return unavailable("Brightness adjustment"); }
bool ImageEditor::adjustBrightnessContrast(int brightness, int contrast) { Q_UNUSED(brightness) Q_UNUSED(contrast) return unavailable("Brightness/contrast adjustment"); }
bool ImageEditor::adjustGamma(double gamma) { Q_UNUSED(gamma) return unavailable("Gamma adjustment"); }
bool ImageEditor::sharpenImage(double radius, double sigma) { Q_UNUSED(radius) Q_UNUSED(sigma) return unavailable("Sharpen"); }
bool ImageEditor::adjustContrast(bool increase, double contrastAmount, double midpoint) { Q_UNUSED(increase) Q_UNUSED(contrastAmount) Q_UNUSED(midpoint) return unavailable("Contrast adjustment"); }
bool ImageEditor::applyBlur(double radius, double sigma) { Q_UNUSED(radius) Q_UNUSED(sigma) return unavailable("Blur"); }
bool ImageEditor::applySepia(double threshold) { Q_UNUSED(threshold) return unavailable("Sepia"); }
bool ImageEditor::applyVignette() { return unavailable("Vignette"); }
bool ImageEditor::applySwirl(double degrees) { Q_UNUSED(degrees) return unavailable("Swirl"); }
bool ImageEditor::applyImplode(double factor) { Q_UNUSED(factor) return unavailable("Implode"); }
bool ImageEditor::drawText(const QString& text, int x, int y) { Q_UNUSED(text) Q_UNUSED(x) Q_UNUSED(y) return unavailable("Draw text"); }
bool ImageEditor::drawRectangle(int x, int y, int width, int height) { Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(width) Q_UNUSED(height) return unavailable("Draw rectangle"); }
bool ImageEditor::undo() { return unavailable("Undo"); }
bool ImageEditor::redo() { return unavailable("Redo"); }
void ImageEditor::clearUndoRedoStacks() {}
bool ImageEditor::applyImpositionEdits(const QString& imagePath, int offsetX, int offsetY, QSize paperSize, const QVariantMap& overlayData, double fallbackInputDpi)
{
    Q_UNUSED(imagePath)
    Q_UNUSED(offsetX)
    Q_UNUSED(offsetY)
    Q_UNUSED(paperSize)
    Q_UNUSED(overlayData)
    Q_UNUSED(fallbackInputDpi)
    return unavailable("Imposition edits");
}
