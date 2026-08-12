#pragma once

#include "IPrintOutputClient.h"

#include <QByteArray>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace PrintFlowPrinterServiceProtocol {

constexpr quint32 Magic = 0x50465053; // "PFPS"
constexpr quint16 Version = 1;
constexpr quint32 MaximumFrameSize = 512u * 1024u * 1024u;

enum class DecodeStatus
{
    Incomplete,
    Complete,
    Invalid
};

QString defaultSocketName();
QByteArray encodeFrame(const QVariantMap& message);
DecodeStatus takeFrame(QByteArray& buffer, QVariantMap* message,
                       QString* errorMessage = nullptr);

QVariantMap settingsToMap(const DirectPrintSettings& settings);
bool settingsFromMap(const QVariantMap& map, DirectPrintSettings* settings,
                     QString* errorMessage = nullptr);

QVariantMap rasterMetadata(const DirectPrintRaster& raster);
bool serializeRaster(const DirectPrintRaster& raster, QByteArray* payload,
                     QString* errorMessage = nullptr);
bool deserializeRaster(
    const QVariantMap& metadata, const QByteArray& payload,
    std::vector<std::vector<std::vector<uint8_t>>>* storage,
    DirectPrintRaster* raster, QString* errorMessage = nullptr);

} // namespace PrintFlowPrinterServiceProtocol
