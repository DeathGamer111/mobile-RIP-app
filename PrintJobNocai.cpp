#include "PrintJobNocai.h"
#include <lcms2.h>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDebug>
#include <QUrl>
#include <fstream>


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


bool PrintJobNocai::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi)
{
    try {
        const QString localOutput = QUrl(outputPath).toLocalFile();

        // === Hardcoded precomputed masks ===
        const QString cMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_c.tiff";
        const QString mMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_m.tiff";
        const QString yMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_y.tiff";
        const QString kMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_k.tiff";

        const std::array<QString, 4> maskPaths = {cMaskPath, mMaskPath, yMaskPath, kMaskPath};
        const std::array<int, 4> nocaiOrder = {2, 1, 0, 3}; // Y, M, C, K

        std::array<std::vector<std::vector<uint8_t>>, 4> packedLines;
        int width = static_cast<int>(cmykChannels[0].columns());
        int height = static_cast<int>(cmykChannels[0].rows());

        for (int ch = 0; ch < 4; ++ch) {
            Magick::Image& channelImg = cmykChannels[ch];
            Magick::Image maskImg;
            maskImg.read(maskPaths[ch].toStdString());

            channelImg.type(Magick::GrayscaleType);
            maskImg.type(Magick::GrayscaleType);

            std::vector<uint8_t> channelBytes(width * height);
            std::vector<uint8_t> maskBytes(width * height);
            std::vector<uint8_t> dithered(width * height, 0);

            channelImg.write(0, 0, width, height, "I", Magick::CharPixel, channelBytes.data());
            maskImg.write(0, 0, width, height, "I", Magick::CharPixel, maskBytes.data());

            // === FX Thresholding (equivalent to -fx 'u>=v?1:0') ===
            for (int i = 0; i < width * height; ++i)
                dithered[i] = (channelBytes[i] >= maskBytes[i]) ? 255 : 0;

            // === Dot Classification Phase ===
            std::vector<std::vector<uint8_t>> dotMap(height, std::vector<uint8_t>(width, 0));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (dithered[y * width + x] < 128) continue; // No dot

                    uint8_t t = maskBytes[y * width + x];
                    if (t >= 192) dotMap[y][x] = 1;
                    else if (t >= 128) dotMap[y][x] = 2;
                    else dotMap[y][x] = 3;
                }
            }

            // === 4×4 Neighborhood Promotion ===
            for (int y = 1; y < height - 2; ++y) {
                for (int x = 1; x < width - 2; ++x) {
                    if (dotMap[y][x] == 3) continue;

                    int count = 0;
                    for (int dy = -1; dy <= 2; ++dy)
                        for (int dx = -1; dx <= 2; ++dx)
                            if (dotMap[y + dy][x + dx] > 0)
                                ++count;

                    if (count >= 12)
                        dotMap[y][x] = 3;
                }
            }

            // === Pack to 2BPP ===
            const int bytesPerLine = (width + 3) / 4;
            packedLines[ch].resize(height);

            for (int y = 0; y < height; ++y) {
                std::vector<uint8_t>& line = packedLines[ch][y];
                line.reserve(bytesPerLine);
                uint8_t byte = 0;
                int idx = 0;

                for (int x = 0; x < width; ++x) {
                    uint8_t level = dotMap[y][x] & 0x03;
                    byte |= level << ((3 - (idx % 4)) * 2);
                    ++idx;

                    if (idx % 4 == 0) {
                        line.push_back(byte);
                        byte = 0;
                    }
                }

                if (idx % 4 != 0) line.push_back(byte);
                while (line.size() % 4 != 0) line.push_back(0);
            }
        }

        // === PRN Header ===
        std::ofstream out(localOutput.toStdString(), std::ios::binary);
        const uint32_t bytesPerLineOut = static_cast<uint32_t>(packedLines[0][0].size());

        uint32_t header[12] = {
            0x00005555,
            static_cast<uint32_t>(xdpi),
            static_cast<uint32_t>(ydpi),
            bytesPerLineOut,
            static_cast<uint32_t>(height),
            static_cast<uint32_t>(width),
            0, 4, 1, 1, 0, 0
        };

        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (int row = 0; row < height; ++row)
            for (int ch : nocaiOrder)
                out.write(reinterpret_cast<const char*>(packedLines[ch][row].data()), packedLines[ch][row].size());

        out.close();
        qDebug() << "✅ Final PRN file created:" << localOutput;
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "❌ PRN generation failed:" << e.what();
        return false;
    }
}


std::array<Magick::Image, 4> PrintJobNocai::separateCMYK(Magick::Image& cmykImage) {
    std::array<Magick::Image, 4> channels;
    int width = static_cast<int>(cmykImage.columns());
    int height = static_cast<int>(cmykImage.rows());

    std::vector<uchar> rawCMYK(width * height * 4);
    cmykImage.write(0, 0, width, height, "CMYK", Magick::CharPixel, rawCMYK.data());

    for (int ch = 0; ch < 4; ++ch) {
        std::vector<uchar> channelData(width * height);
        for (int i = 0; i < width * height; ++i)
            channelData[i] = rawCMYK[i * 4 + ch];

        channels[ch] = Magick::Image(Magick::Geometry(width, height), "white");
        channels[ch].depth(8);
        channels[ch].type(Magick::GrayscaleType);
        channels[ch].read(width, height, "I", Magick::CharPixel, channelData.data());
    }

    return channels;
}



/*
bool PrintJobNocai::generateFinalPRN(const QString& outputPath, int xdpi, int ydpi)
{
    try {
        const QString localOutput = QUrl(outputPath).toLocalFile();

        // === Hardcoded precomputed masks ===
        const QString cMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_c.tiff";
        const QString mMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_m.tiff";
        const QString yMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_y.tiff";
        const QString kMaskPath = "/home/mccalla/Downloads/precomputed_masks/mask_k.tiff";

        const std::array<QString, 4> maskPaths = {cMaskPath, mMaskPath, yMaskPath, kMaskPath};
        const std::array<int, 4> nocaiOrder = {2, 1, 0, 3}; // Y, M, C, K

        std::array<std::vector<std::vector<uint8_t>>, 4> packedLines;
        int width = static_cast<int>(cmykChannels[0].columns());
        int height = static_cast<int>(cmykChannels[0].rows());

        for (int ch = 0; ch < 4; ++ch) {
            Magick::Image& channelImg = cmykChannels[ch];
            Magick::Image maskImg;
            maskImg.read(maskPaths[ch].toStdString());

            channelImg.type(Magick::GrayscaleType);
            maskImg.type(Magick::GrayscaleType);

            std::vector<uint8_t> channelBytes(width * height);
            std::vector<uint8_t> maskBytes(width * height);

            channelImg.write(0, 0, width, height, "I", Magick::CharPixel, channelBytes.data());
            maskImg.write(0, 0, width, height, "I", Magick::CharPixel, maskBytes.data());

            // === Dot Classification Phase ===
            std::vector<std::vector<uint8_t>> dotMap(height, std::vector<uint8_t>(width, 0));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    uint8_t ink = channelBytes[y * width + x];
                    if (ink < 128) continue; // No ink

                    uint8_t t = maskBytes[y * width + x];
                    if (t >= 192) dotMap[y][x] = 1;
                    else if (t >= 128) dotMap[y][x] = 2;
                    else dotMap[y][x] = 3;
                }
            }

            // === 4×4 Neighborhood Promotion ===
            for (int y = 1; y < height - 2; ++y) {
                for (int x = 1; x < width - 2; ++x) {
                    if (dotMap[y][x] == 3) continue;

                    int count = 0;
                    for (int dy = -1; dy <= 2; ++dy)
                        for (int dx = -1; dx <= 2; ++dx)
                            if (dotMap[y + dy][x + dx] > 0)
                                ++count;

                    if (count >= 12)
                        dotMap[y][x] = 3;
                }
            }

            // === Pack to 2BPP ===
            const int bytesPerLine = (width + 3) / 4;
            packedLines[ch].resize(height);

            for (int y = 0; y < height; ++y) {
                std::vector<uint8_t>& line = packedLines[ch][y];
                line.reserve(bytesPerLine);
                uint8_t byte = 0;
                int idx = 0;

                for (int x = 0; x < width; ++x) {
                    uint8_t level = dotMap[y][x] & 0x03;
                    byte |= level << ((3 - (idx % 4)) * 2);
                    ++idx;

                    if (idx % 4 == 0) {
                        line.push_back(byte);
                        byte = 0;
                    }
                }

                if (idx % 4 != 0) line.push_back(byte);
                while (line.size() % 4 != 0) line.push_back(0);
            }
        }

        // === PRN Header ===
        std::ofstream out(localOutput.toStdString(), std::ios::binary);
        const uint32_t bytesPerLineOut = static_cast<uint32_t>(packedLines[0][0].size());

        uint32_t header[12] = {
            0x00005555,
            static_cast<uint32_t>(xdpi),
            static_cast<uint32_t>(ydpi),
            bytesPerLineOut,
            static_cast<uint32_t>(height),
            static_cast<uint32_t>(width),
            0, 4, 1, 1, 0, 0
        };

        out.write(reinterpret_cast<const char*>(header), sizeof(header));

        for (int row = 0; row < height; ++row)
            for (int ch : nocaiOrder)
                out.write(reinterpret_cast<const char*>(packedLines[ch][row].data()), packedLines[ch][row].size());

        out.close();
        qDebug() << "Final PRN file created:" << localOutput;
        return true;

    } catch (const Magick::Exception& e) {
        qWarning() << "❌ PRN generation failed:" << e.what();
        return false;
    }
}


// === Private Helpers ===

std::array<Magick::Image, 4> PrintJobNocai::separateCMYK(const Magick::Image& cmyk) {
    std::array<Magick::Image, 4> result;

    std::array<Magick::ChannelType, 4> channels = {
        Magick::CyanChannel, Magick::MagentaChannel,
        Magick::YellowChannel, Magick::BlackChannel
    };

    for (int i = 0; i < 4; ++i) {
        Magick::Image separated = cmyk;
        separated.channel(channels[i]);               // View only this channel
        separated.type(Magick::GrayscaleType);        // Ensure proper type
        separated.quantizeColorSpace(Magick::GRAYColorspace); // Force colorspace
        separated.quantizeColors(256);
        separated.quantize();
        result[i] = separated;
    }

    return result;
}
*/

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
