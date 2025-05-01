#include <QObject>
#include <QString>
#include <Magick++.h>



/***************************************************************************
    ImageEditor provides image manipulation capabilities using ImageMagick.
    Exposed to QML through Q_INVOKABLE methods.
****************************************************************************/

class ImageEditor : public QObject {
    Q_OBJECT

public:
    explicit ImageEditor(QObject *parent = nullptr);

    // Image I/O operations
    Q_INVOKABLE bool loadImage(const QString &path);                        // Load image from file
    Q_INVOKABLE bool saveImage(const QString &outputPath);                  // Save image to file
    Q_INVOKABLE bool deleteFile(const QString &path);                       // Delete file from disk

    // Transformations
    Q_INVOKABLE bool rotate(double degrees);                                // Rotate image by degrees
    Q_INVOKABLE bool flip(const QString &direction);                        // Flip image horizontally or vertically
    Q_INVOKABLE bool crop(int x, int y, int width, int height);             // Crop image to given rectangle

    // Color space conversions
    Q_INVOKABLE bool convertColorSpace(const QString &targetSpace);         // Convert to CMYK, sRGB, or GRAY

    // Resize operations
    Q_INVOKABLE bool resizeImage(int width, int height);                    // Resize to specific dimensions
    Q_INVOKABLE bool resizeToOriginal();                                    // Reset to original image size
    Q_INVOKABLE bool resizeToHalf();                                        // Scale image to 50%
    Q_INVOKABLE bool resizeToDouble();                                      // Scale image to 200%

    // Enhancement operations
    Q_INVOKABLE bool adjustHue(int hue);                                    // Adjust hue in range +/- 100
    Q_INVOKABLE bool adjustSaturation(int saturation);                      // Adjust saturation in range +/- 100
    Q_INVOKABLE bool adjustBrightnessContrast(int brightness, int contrast);// Adjust brightness/contrast
    Q_INVOKABLE bool adjustGamma(double gamma);                             // Adjust gamma level
    Q_INVOKABLE bool sharpenImage(double radius = 1.0, double sigma = 0.5); // Apply sharpening

    // Effects
    Q_INVOKABLE bool applyBlur(double radius = 0.0, double sigma = 1.0);    // Apply Gaussian blur
    Q_INVOKABLE bool applySepia(double threshold = 80.0);                   // Apply sepia tone
    Q_INVOKABLE bool applyVignette();                                       // Apply vignette effect
    Q_INVOKABLE bool applySwirl(double degrees);                            // Apply swirl distortion
    Q_INVOKABLE bool applyImplode(double factor);                           // Apply implode effect

    // Drawing functions
    Q_INVOKABLE bool drawText(const QString &text, int x, int y);           // Draw text at (x, y)
    Q_INVOKABLE bool drawRectangle(int x, int y, int width, int height);    // Draw rectangle at (x, y)

    // Accessor
    QString currentImagePath() const { return imagePath; }

private:
    Magick::Image m_image;
    QString imagePath;
    bool m_imageLoaded = false;
};
