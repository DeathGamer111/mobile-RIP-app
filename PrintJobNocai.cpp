#include "PrintJobNocai.h"
#include <lcms2.h>
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


// Apply ICC Conversion to the CMYK Colorspace
bool PrintJobNocai::applyICCConversion(const QString& inputProfile, const QString& outputProfile) {
    try {
        QString inPath = QUrl(inputProfile).toLocalFile();
        QString outPath = QUrl(outputProfile).toLocalFile();

        // Load ICC profiles
        cmsHPROFILE inputICC = cmsOpenProfileFromFile(inPath.toStdString().c_str(), "r");
        cmsHPROFILE outputICC = cmsOpenProfileFromFile(outPath.toStdString().c_str(), "r");
        if (!inputICC || !outputICC) {
            qWarning() << "❌ Failed to load one or both ICC profiles.";
            if (inputICC) cmsCloseProfile(inputICC);
            if (outputICC) cmsCloseProfile(outputICC);
            return false;
        }

        // Create Little CMS transform
        cmsHTRANSFORM transform = cmsCreateTransform(inputICC, TYPE_RGB_8,
                                                     outputICC, TYPE_CMYK_8,
                                                     INTENT_PERCEPTUAL, 0);
        if (!transform) {
            qWarning() << "❌ Failed to create ICC transform.";
            cmsCloseProfile(inputICC);
            cmsCloseProfile(outputICC);
            return false;
        }

        // Prepare input and output buffers
        int width = static_cast<int>(inputImage.columns());
        int height = static_cast<int>(inputImage.rows());
        std::vector<uchar> rgbBuffer(width * height * 3);
        std::vector<uchar> cmykBuffer(width * height * 4);

        inputImage.type(Magick::TrueColorType);
        inputImage.colorSpace(Magick::RGBColorspace);
        inputImage.write(0, 0, width, height, "RGB", Magick::CharPixel, rgbBuffer.data());

        // Apply color conversion
        cmsDoTransform(transform, rgbBuffer.data(), cmykBuffer.data(), width * height);

        cmsDeleteTransform(transform);
        cmsCloseProfile(inputICC);
        cmsCloseProfile(outputICC);

        // Create CMYK Magick image
        cmykImage = Magick::Image(Magick::Geometry(width, height), "white");
        cmykImage.depth(8);
        cmykImage.type(Magick::TrueColorType);
        cmykImage.colorSpace(Magick::CMYKColorspace);
        cmykImage.read(width, height, "CMYK", Magick::CharPixel, cmykBuffer.data());

        // Embed the output ICC profile into CMYK image
        std::ifstream profileFile(outPath.toStdString(), std::ios::binary);
        if (profileFile) {
            std::vector<char> profileData((std::istreambuf_iterator<char>(profileFile)), {});
            Magick::Blob profileBlob(profileData.data(), profileData.size());
            cmykImage.profile("icc", profileBlob);
        }

        // Extract CMYK channels
        cmykChannels = separateCMYK(cmykImage);

        qDebug() << "✅ ICC conversion succeeded using:" << inPath << "→" << outPath;
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "❌ Exception during ICC conversion:" << e.what();
        return false;
    }
}


// Final PRN generator including halftoning and packing
bool PrintJobNocai::generateFinalPRN(const QString& maskPath, const QString& outputPath, int xdpi, int ydpi) {
    try {
        QString localOutput = QUrl(outputPath).toLocalFile();
        Magick::Image baseMask;
        baseMask.read(QUrl(maskPath).toLocalFile().toStdString());

        std::array<int, 4> offsetX = {0, 64, 128, 192};
        std::array<int, 4> offsetY = {0, 64, 128, 192};

        int width = static_cast<int>(cmykChannels[0].columns());
        int height = static_cast<int>(cmykChannels[0].rows());
        int bytesPerLine = (width + 3) / 4; // 4 pixels per byte, padded later

        const double quantumRange = (sizeof(Magick::Quantum) == 2) ? 65535.0 : 255.0;
        double thresholdInk = quantumRange / 2.0;

        std::array<std::vector<std::vector<uint8_t>>, 4> dotMaps;

        for (int ch = 0; ch < 4; ++ch) {
            Magick::Image ditherMask = buildDitherMask(baseMask, width, height, offsetX[ch], offsetY[ch]);
            dotMaps[ch].resize(height, std::vector<uint8_t>(width, 0));

            // Get raw pixel buffers once per channel
            Magick::Pixels inkView(cmykChannels[ch]);
            const Magick::PixelPacket* inkPixels = inkView.getConst(0, 0, width, height);

            Magick::Pixels maskView(ditherMask);
            const Magick::PixelPacket* maskPixels = maskView.getConst(0, 0, width, height);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int idx = y * width + x;

                    double inkVal;
                    switch (ch) {
                    case 0: inkVal = inkPixels[idx].red; break;       // Cyan
                    case 1: inkVal = inkPixels[idx].green; break;     // Magenta
                    case 2: inkVal = inkPixels[idx].blue; break;      // Yellow
                    case 3: inkVal = inkPixels[idx].opacity; break;   // Black (K channel = opacity)
                    }
                    inkVal /= quantumRange;

                    double maskVal = maskPixels[idx].red / quantumRange;

                    if (inkVal >= maskVal) {
                        if (maskVal >= 0.75)
                            dotMaps[ch][y][x] = 1;  // small
                        else if (maskVal >= 0.5)
                            dotMaps[ch][y][x] = 2;  // medium
                        else
                            dotMaps[ch][y][x] = 3;  // large
                    }
                }
            }

            // Promote dot sizes with 4×4 neighborhood
            for (int y = 1; y < height - 2; ++y) {
                for (int x = 1; x < width - 2; ++x) {
                    if (dotMaps[ch][y][x] == 3) continue;

                    int count = 0;
                    for (int dy = -1; dy <= 2; ++dy)
                        for (int dx = -1; dx <= 2; ++dx)
                            if (dotMaps[ch][y + dy][x + dx] > 0)
                                ++count;

                    if (count >= 12)
                        dotMaps[ch][y][x] = 3;
                }
            }
        }

        // Pack to 2BPP per channel
        std::array<std::vector<std::vector<uint8_t>>, 4> packedLines;

        for (int ch = 0; ch < 4; ++ch) {
            packedLines[ch].resize(height);
            for (int y = 0; y < height; ++y) {
                std::vector<uint8_t>& line = packedLines[ch][y];
                line.reserve(bytesPerLine);
                uint8_t byte = 0;
                int idx = 0;

                for (int x = 0; x < width; ++x) {
                    uint8_t level = dotMaps[ch][y][x] & 0x03;
                    byte |= level << ((3 - (idx % 4)) * 2);
                    ++idx;

                    if (idx % 4 == 0) {
                        line.push_back(byte);
                        byte = 0;
                    }
                }

                if (idx % 4 != 0)
                    line.push_back(byte);

                while (line.size() % 4 != 0)
                    line.push_back(0);
            }
        }

        // Construct PRN header
        std::ofstream out(localOutput.toStdString(), std::ios::binary);
        uint32_t header[12] = {
            0x00005555,              // Signature
            static_cast<uint32_t>(xdpi),
            static_cast<uint32_t>(ydpi),
            static_cast<uint32_t>(packedLines[0][0].size()), // bytes per line
            static_cast<uint32_t>(height),
            static_cast<uint32_t>(width),
            0, 4, 1, 1, 0, 0
        };
        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        static const std::array<int, 4> nocaiChannelOrder = {2, 1, 0, 3}; // Y, M, C, K

        for (int row = 0; row < height; ++row) {
            for (int ch : nocaiChannelOrder) {
                out.write(reinterpret_cast<const char*>(packedLines[ch][row].data()), packedLines[ch][row].size());
            }
        }

        out.close();
        qDebug() << "✅ PRN file written to:" << localOutput;
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
    const int tileSize = static_cast<int>(base.columns());  // Assumes square tile
    const int tilesX = (width + tileSize - 1) / tileSize;
    const int tilesY = (height + tileSize - 1) / tileSize;

    std::vector<Magick::Image> rowTiles;
    std::vector<Magick::Image> fullRows;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> rot(0, 3);

    // Build full mask with randomized per-tile rotation
    for (int ty = 0; ty < tilesY; ++ty) {
        rowTiles.clear();
        for (int tx = 0; tx < tilesX; ++tx) {
            Magick::Image tile = base;
            tile.rotate(rot(gen) * 90);
            tile.type(Magick::GrayscaleType);
            rowTiles.push_back(tile);
        }

        Magick::Image rowImage;
        Magick::appendImages(&rowImage, rowTiles.begin(), rowTiles.end(), /*stack=*/true);
        fullRows.push_back(rowImage);
    }

    Magick::Image fullMask;
    Magick::appendImages(&fullMask, fullRows.begin(), fullRows.end(), /*stack=*/false);

    // Crop to final size
    fullMask.crop(Magick::Geometry(width, height));

    // Set virtual-pixel method to tile (wrap) and apply offset
    fullMask.virtualPixelMethod(Magick::TileVirtualPixelMethod);
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
