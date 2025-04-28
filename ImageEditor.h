#include <QObject>
#include <QString>
#include <Magick++.h>


class ImageEditor : public QObject {
    Q_OBJECT

public:
    explicit ImageEditor(QObject *parent = nullptr);

    // Load/save
    Q_INVOKABLE bool loadImage(const QString &path);
    Q_INVOKABLE bool saveImage(const QString &outputPath);
    Q_INVOKABLE bool deleteFile(const QString &path);

    // Transformations
    Q_INVOKABLE bool rotate(double degrees);
    Q_INVOKABLE bool flip(const QString &direction); // "horizontal" or "vertical"
    Q_INVOKABLE bool crop(int x, int y, int width, int height);

    // Color Space Conversion
    Q_INVOKABLE bool convertColorSpace(const QString &targetSpace); // "CMYK", "sRGB", "GRAY"

    // Resize Operations
    Q_INVOKABLE bool resizeImage(int width, int height);
    Q_INVOKABLE bool resizeToOriginal();
    Q_INVOKABLE bool resizeToHalf();
    Q_INVOKABLE bool resizeToDouble();

    // Enhancement Adjustments
    Q_INVOKABLE bool adjustHue(int hue);                  // +/- 100
    Q_INVOKABLE bool adjustSaturation(int saturation);    // +/- 100
    Q_INVOKABLE bool adjustBrightnessContrast(int brightness, int contrast); // +/-
    Q_INVOKABLE bool adjustGamma(double gamma);
    Q_INVOKABLE bool sharpenImage(double radius = 1.0, double sigma = 0.5);

    // Effects
    Q_INVOKABLE bool applyBlur(double radius = 0.0, double sigma = 1.0);
    Q_INVOKABLE bool applySepia(double threshold = 80.0);
    Q_INVOKABLE bool applyVignette();
    Q_INVOKABLE bool applySwirl(double degrees);
    Q_INVOKABLE bool applyImplode(double factor);

    // Drawing
    Q_INVOKABLE bool drawText(const QString &text, int x, int y);
    Q_INVOKABLE bool drawRectangle(int x, int y, int width, int height);

    QString currentImagePath() const { return imagePath; }

private:
    Magick::Image m_image;
    QString imagePath;
    bool m_imageLoaded = false;
};
