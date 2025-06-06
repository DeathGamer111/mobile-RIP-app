#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <array>
#include <Magick++.h>


class PrintJobNocai : public QObject {
    Q_OBJECT

public:
    explicit PrintJobNocai(QObject* parent = nullptr);

    // QML-exposed pipeline
    Q_INVOKABLE bool loadInputImage(const QString& imagePath);
    Q_INVOKABLE bool applyICCConversion(const QString& inputProfile, const QString& outputProfile);
    Q_INVOKABLE bool generateFinalPRN(const QString& outputPath, int xdpi, int ydpi);

private:

    // Internal images and data
    Magick::Image inputImage;                        // RGB input (temporary copy)
    Magick::Image cmykImage;                         // CMYK converted image
    std::array<Magick::Image, 4> cmykChannels;       // C, M, Y, K separated
    std::array<Magick::Image, 4> thresholdMasks;     // Blue noise masks per channel
    std::array<std::vector<uint8_t>, 4> dotMaps;     // Dot size maps per channel
    std::array<std::vector<uint8_t>, 4> packedOutput;// 2BPP output per channel

    // Paths and temp handling
    QString originalFilename;
    QString tempImagePath;
    std::unique_ptr<QTemporaryDir> tempDir;

    // Internal helpers
    //std::array<Magick::Image, 4> separateCMYK(const Magick::Image& cmyk);

    std::array<Magick::Image, 4> separateCMYK(Magick::Image& cmyk);


    Magick::Image buildDitherMask(const Magick::Image& baseMask, int width, int height, int offsetX, int offsetY);
    uint8_t classifyDot(uint8_t imagePixel, uint8_t threshold);
    void apply4x4Promotion(std::vector<uint8_t>& dotMap, int width, int height);
    std::vector<uint8_t> packTo2BPP(const std::vector<uint8_t>& dotMap, int width, int height);
    Magick::Blob loadICCProfile(const QString& filePath);
};
