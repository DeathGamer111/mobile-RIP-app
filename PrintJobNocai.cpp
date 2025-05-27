#include "PrintJobNocai.h"
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDebug>
#include <QUrl>
#include <fstream>
#include <random>


// Constructor
PrintJobNocai::PrintJobNocai(QObject* parent) : QObject(parent) {}


// Load image and copy to a temporary location
bool PrintJobNocai::loadInputImage(const QString& imagePath) {
    try {
        QString localPath = QUrl(imagePath).toLocalFile();
        inputImage.read(localPath.toStdString());

        QFileInfo fileInfo(localPath);
        originalFilename = fileInfo.fileName();

        tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            qWarning() << "Failed to create temp dir";
            return false;
        }

        tempImagePath = tempDir->filePath(originalFilename);
        inputImage.write(tempImagePath.toStdString());

        qDebug() << "Loaded and copied input image to:" << tempImagePath;
        return true;
    } catch (const Magick::Exception& e) {
        qWarning() << "Image load failed:" << e.what();
        return false;
    }
}


// Apply ICC conversion (RGB -> CMYK)
bool PrintJobNocai::applyICCConversion(const QString& inputProfile, const QString& outputProfile) {
    try {
        QString inPath = QUrl(inputProfile).toLocalFile();
        QString outPath = QUrl(outputProfile).toLocalFile();

        Magick::Blob inProfile = loadICCProfile(inPath);
        Magick::Blob outProfile = loadICCProfile(outPath);

        inputImage.profile("ICC", inProfile);
        inputImage.profile("ICC", outProfile);
        inputImage.colorSpace(Magick::CMYKColorspace);

        cmykImage = inputImage;
        cmykChannels = separateCMYK(cmykImage);

        qDebug() << "Applied ICC conversion with profiles:"
                 << inPath << "→" << outPath;
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "ICC conversion failed:" << e.what();
        return false;
    }
}


// Final PRN generator including halftoning and packing
bool PrintJobNocai::generateFinalPRN(const QString& maskPath, const QString& outputPath, int xdpi, int ydpi) {
    try {
        QString localOutput = QUrl(outputPath).toLocalFile();
        Magick::Image baseMask(QUrl(maskPath).toLocalFile().toStdString());

        // Per-channel offsets
        std::array<int, 4> offsetX = {0, 64, 128, 192};
        std::array<int, 4> offsetY = {0, 64, 128, 192};

        int width = static_cast<int>(cmykChannels[0].columns());
        int height = static_cast<int>(cmykChannels[0].rows());

        // Detect quantum depth (Q8 or Q16)
        int quantumDepth = static_cast<int>(sizeof(Magick::Quantum) * 8);
        Magick::Quantum thresholdValue = (quantumDepth == 16) ? 32768 : 128;
        qDebug() << "Detected quantum depth:" << quantumDepth << "→ Threshold value:" << thresholdValue;

        std::array<std::vector<uint8_t>, 4> finalDotMaps;

        for (int i = 0; i < 4; ++i) {
            // Build channel-specific dither mask
            thresholdMasks[i] = buildDitherMask(baseMask, width, height, offsetX[i], offsetY[i]);

            std::vector<uint8_t>& map = dotMaps[i];
            map.resize(width * height, 0);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    Magick::Quantum intensity = cmykChannels[i].pixelColor(x, y).redQuantum();
                    if (intensity < thresholdValue)
                        continue; // not a dot

                    uint8_t t = thresholdMasks[i].pixelColor(x, y).redQuantum() >> 8;
                    if (t >= 192) map[y * width + x] = 1;
                    else if (t >= 128) map[y * width + x] = 2;
                    else map[y * width + x] = 3;
                }
            }

            apply4x4Promotion(map, width, height);
            packedOutput[i] = packTo2BPP(map, width, height);
        }

        // Write PRN with header
        std::ofstream out(localOutput.toStdString(), std::ios::binary);
        uint32_t bytesPerLine = static_cast<uint32_t>(packedOutput[0].size() / height);

        uint32_t header[12] = {
            0x00005555,             // Signature
            static_cast<uint32_t>(xdpi),
            static_cast<uint32_t>(ydpi),
            bytesPerLine,
            static_cast<uint32_t>(height),
            static_cast<uint32_t>(width),
            0,                      // Paper Width
            4,                      // Channels
            1,                      // Bits (dot size)
            1,                      // Pass
            0,                      // VSDMode
            0                       // Reserved
        };

        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (int row = 0; row < height; ++row) {
            // Output in Y, M, C, K order
            for (int ch : {2, 1, 0, 3}) {
                const uint8_t* line = &packedOutput[ch][row * bytesPerLine];
                out.write(reinterpret_cast<const char*>(line), bytesPerLine);
            }
        }

        out.close();
        qDebug() << "PRN file generated at:" << localOutput;
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "PRN generation failed:" << e.what();
        return false;
    }
}


// === Private Helpers ===

std::array<Magick::Image, 4> PrintJobNocai::separateCMYK(const Magick::Image& cmyk) {
    std::array<Magick::Image, 4> result;

    result[0] = cmyk;
    result[0].channel(Magick::CyanChannel);

    result[1] = cmyk;
    result[1].channel(Magick::MagentaChannel);

    result[2] = cmyk;
    result[2].channel(Magick::YellowChannel);

    result[3] = cmyk;
    result[3].channel(Magick::BlackChannel);

    return result;
}


Magick::Image PrintJobNocai::buildDitherMask(const Magick::Image& base, int width, int height, int offsetX, int offsetY) {
    Magick::Image tiled = base;
    int tilesX = (width + base.columns() - 1) / base.columns();
    int tilesY = (height + base.rows() - 1) / base.rows();

    std::list<Magick::Image> row;
    std::list<Magick::Image> full;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> rot(0, 3);

    for (int y = 0; y < tilesY; ++y) {
        row.clear();
        for (int x = 0; x < tilesX; ++x) {
            Magick::Image tile = base;
            tile.rotate(rot(gen) * 90);
            row.push_back(tile);
        }
        Magick::Image rowAppended;
        Magick::appendImages(&rowAppended, row.begin(), row.end(), true);
        full.push_back(rowAppended);
    }

    Magick::Image fullMask;
    Magick::appendImages(&fullMask, full.begin(), full.end(), false);
    fullMask.crop(Magick::Geometry(width, height));
    fullMask.roll(offsetX, offsetY);
    return fullMask;
}


void PrintJobNocai::apply4x4Promotion(std::vector<uint8_t>& map, int width, int height) {
    for (int y = 1; y < height - 2; ++y) {
        for (int x = 1; x < width - 2; ++x) {
            uint8_t& dot = map[y * width + x];
            if (dot == 3) continue;
            int count = 0;
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 4; ++dx)
                    if (map[(y + dy - 1) * width + (x + dx - 1)] > 0)
                        ++count;
            if (count >= 12) dot = 3;
        }
    }
}


std::vector<uint8_t> PrintJobNocai::packTo2BPP(const std::vector<uint8_t>& map, int width, int height) {
    std::vector<uint8_t> packed((width * height + 3) / 4);
    for (int i = 0; i < width * height; ++i) {
        uint8_t dot = map[i] & 0x03;
        packed[i / 4] |= dot << ((3 - (i % 4)) * 2);
    }
    return packed;
}


Magick::Blob PrintJobNocai::loadICCProfile(const QString& path) {
    std::ifstream file(path.toStdString(), std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(file)), {});
    return Magick::Blob(buf.data(), buf.size());
}
