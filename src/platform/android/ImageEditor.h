#pragma once

#include <QObject>
#include <QString>
#include <QSize>
#include <QVariantMap>

class ImageEditor : public QObject
{
    Q_OBJECT

public:
    explicit ImageEditor(QObject* parent = nullptr);

    Q_INVOKABLE bool loadImage(const QString& path);
    Q_INVOKABLE bool saveImage(const QString& outputPath);
    Q_INVOKABLE bool deleteFile(const QString& path);
    Q_INVOKABLE int getImageWidth() const;
    Q_INVOKABLE int getImageHeight() const;

    Q_INVOKABLE bool rotate(double degrees);
    Q_INVOKABLE bool flip(const QString& direction);
    Q_INVOKABLE bool crop(int x, int y, int width, int height);
    Q_INVOKABLE bool resizeImage(int width, int height);
    Q_INVOKABLE bool resizeToOriginal();
    Q_INVOKABLE bool resizeToHalf();
    Q_INVOKABLE bool resizeToDouble();

    Q_INVOKABLE bool adjustHue(int hue);
    Q_INVOKABLE bool adjustSaturation(int saturation);
    Q_INVOKABLE bool adjustBrightness(int brightness);
    Q_INVOKABLE bool adjustBrightnessContrast(int brightness, int contrast);
    Q_INVOKABLE bool adjustGamma(double gamma);
    Q_INVOKABLE bool sharpenImage(double radius = 1.0, double sigma = 0.5);
    Q_INVOKABLE bool adjustContrast(bool increase, double contrastAmount, double midpoint);

    Q_INVOKABLE bool applyBlur(double radius = 0.0, double sigma = 1.0);
    Q_INVOKABLE bool applySepia(double threshold = 80.0);
    Q_INVOKABLE bool applyVignette();
    Q_INVOKABLE bool applySwirl(double degrees);
    Q_INVOKABLE bool applyImplode(double factor);

    Q_INVOKABLE bool drawText(const QString& text, int x, int y);
    Q_INVOKABLE bool drawRectangle(int x, int y, int width, int height);

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearUndoRedoStacks();
    Q_INVOKABLE bool applyImpositionEdits(const QString& imagePath, int offsetX, int offsetY, QSize paperSize, const QVariantMap& overlayData = {}, double fallbackInputDpi = 720.0);

private:
    QString m_imagePath;
    int m_width = 0;
    int m_height = 0;
};
