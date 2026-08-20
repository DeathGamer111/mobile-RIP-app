#include <QObject>
#include <QString>
#include <QSize>
#include <QFileInfo>
#include <QVariant>
#include <vector>
#include <Magick++.h>



/***************************************************************************
    ImageEditor provides image manipulation capabilities using ImageMagick.
    Exposed to QML through Q_INVOKABLE methods.
    Methods generally return bool and log on failure in the implementation
****************************************************************************/

class ImageEditor : public QObject {
	Q_OBJECT

public:
	explicit ImageEditor(QObject *parent = nullptr);

    // Image I/O operations
    Q_INVOKABLE bool loadImage(const QString &path);                       // Load image from file (updates internal state).
    Q_INVOKABLE bool saveImage(const QString &outputPath);                 // Save current image to file.
    Q_INVOKABLE bool deleteFile(const QString &path);                      // Delete file on disk (utility).
    Q_INVOKABLE int  getImageWidth() const;                                // Current image width (px).
    Q_INVOKABLE int  getImageHeight() const;                               // Current image height (px).

	// Transformations
    Q_INVOKABLE bool rotate(double degrees);                               // Rotate by degrees (positive = CW).
    Q_INVOKABLE bool flip(const QString &direction);                       // Flip "horizontal" or "vertical".
    Q_INVOKABLE bool crop(int x, int y, int width, int height);            // Crop to rect (x,y,w,h).

    // Resize operations
	Q_INVOKABLE bool resizeImage(int width, int height);                    // Resize to exact dimensions
	Q_INVOKABLE bool resizeToOriginal();                                    // Reset to original image size
	Q_INVOKABLE bool resizeToHalf();                                        // Scale image to 50%
	Q_INVOKABLE bool resizeToDouble();                                      // Scale image to 200%

    // Enhancement operations
	Q_INVOKABLE bool adjustHue(int hue);                                    				// Adjust hue in range +/- 100
	Q_INVOKABLE bool adjustSaturation(int saturation);                      				// Adjust saturation in range +/- 100
	Q_INVOKABLE bool adjustBrightness(int brightness);										// Adjust brightness +/- 100
	Q_INVOKABLE bool adjustGamma(double gamma);                             				// Adjust gamma level (e.g., 0.1..5.0).
	Q_INVOKABLE bool sharpenImage(double radius = 1.0, double sigma = 0.5); 				// Apply sharpening 
	Q_INVOKABLE bool adjustContrast(bool increase, double contrastAmount, double midpoint);	// Adjust contrast
	
    // Effects
    Q_INVOKABLE bool applyBlur(double radius = 0.0, double sigma = 1.0);   // Gaussian blur (radius 0 => auto).
    Q_INVOKABLE bool applySepia(double threshold = 80.0);                  // Sepia tone (0..100 typical).
    Q_INVOKABLE bool applyVignette();                                      // Vignette with internal defaults.
    Q_INVOKABLE bool applySwirl(double degrees);                           // Swirl distortion (degrees).
    Q_INVOKABLE bool applyImplode(double factor);                          // Implode (negative explodes).

    // Drawing functions
	Q_INVOKABLE bool drawText(const QString &text, int x, int y);           // Draw text at (x, y)
	Q_INVOKABLE bool drawRectangle(int x, int y, int width, int height);    // Draw rectangle at (x, y)

    // Accessor
	QString currentImagePath() const { return imagePath; }					// Current bound image path.
    
	// Undo/Redo (operates on full Magick::Image snapshots; can be memory-heavy on large images)
	Q_INVOKABLE bool undo();
	Q_INVOKABLE bool redo();
	Q_INVOKABLE void clearUndoRedoStacks();
	
	// Imposition updates
    // overlayData keys (optional): text, textX, textY, rectX, rectY, rectW, rectH, strokeWidth, etc.
	Q_INVOKABLE bool applyImpositionEdits(const QString &imagePath, int offsetX, int offsetY, QSize paperSize, const QVariantMap &overlayData = {}, double fallbackInputDpi = 720.0);


private:
	Magick::Image m_image;                 // Working image buffer.
    QString       imagePath;               // Bound image path (for save/IO helpers).
    bool          m_imageLoaded = false;   // True when m_image holds valid content.

    void pushUndoState();                  // Push current image onto undo stack.
    std::vector<Magick::Image> m_undoStack;
    std::vector<Magick::Image> m_redoStack;

    // Stateful sliders (percent-like semantics expected by UI).
    double m_brightness = 100.0;           // 100 = neutral; UI may pass deltas around this baseline.
    double m_saturation = 100.0;           // 100 = neutral.
    double m_hue        = 100.0;           // 100 = neutral or 0 delta depending on implementation.

};
