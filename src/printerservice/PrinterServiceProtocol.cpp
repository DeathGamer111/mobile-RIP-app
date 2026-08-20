#include "PrinterServiceProtocol.h"

#include <QDataStream>
#include <QDir>
#include <QIODevice>
#include <QStandardPaths>
#include <QVariantList>

#include <algorithm>
#include <limits>

namespace PrintFlowPrinterServiceProtocol {
namespace {

void setError(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}

bool checkedRasterSize(int height, int channelCount, int bytesPerLine,
                       qsizetype* size)
{
    if (height <= 0 || channelCount <= 0 || bytesPerLine <= 0)
        return false;
    const quint64 total = quint64(height) * quint64(channelCount) *
                          quint64(bytesPerLine);
    if (total > quint64(MaximumFrameSize) ||
        total > quint64(std::numeric_limits<qsizetype>::max())) {
        return false;
    }
    if (size)
        *size = static_cast<qsizetype>(total);
    return true;
}

} // namespace

QString defaultSocketName()
{
    const QString override = qEnvironmentVariable(
        "PRINTFLOW_PRINTER_SERVICE_SOCKET").trimmed();
    if (!override.isEmpty())
        return override;

    QString runtimeDirectory = QStandardPaths::writableLocation(
        QStandardPaths::RuntimeLocation);
    if (runtimeDirectory.isEmpty())
        runtimeDirectory = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
    return QDir(runtimeDirectory).absoluteFilePath(
        QStringLiteral("printflow-printer-service-v1"));
}

QByteArray encodeFrame(const QVariantMap& message)
{
    QByteArray body;
    QDataStream bodyStream(&body, QIODevice::WriteOnly);
    bodyStream.setVersion(QDataStream::Qt_6_0);
    bodyStream << quint32(Magic) << quint16(Version) << message;
    if (bodyStream.status() != QDataStream::Ok ||
        body.size() > qsizetype(MaximumFrameSize)) {
        return {};
    }

    QByteArray frame;
    QDataStream frameStream(&frame, QIODevice::WriteOnly);
    frameStream.setVersion(QDataStream::Qt_6_0);
    frameStream << quint32(body.size());
    frame.append(body);
    return frame;
}

DecodeStatus takeFrame(QByteArray& buffer, QVariantMap* message,
                       QString* errorMessage)
{
    if (!message) {
        setError(errorMessage, QStringLiteral("Message destination is null."));
        return DecodeStatus::Invalid;
    }
    if (buffer.size() < qsizetype(sizeof(quint32)))
        return DecodeStatus::Incomplete;

    QDataStream lengthStream(buffer.left(sizeof(quint32)));
    lengthStream.setVersion(QDataStream::Qt_6_0);
    quint32 bodySize = 0;
    lengthStream >> bodySize;
    if (lengthStream.status() != QDataStream::Ok || bodySize == 0 ||
        bodySize > MaximumFrameSize) {
        setError(errorMessage, QStringLiteral("Printer-service frame length is invalid."));
        return DecodeStatus::Invalid;
    }

    const qsizetype frameSize = qsizetype(sizeof(quint32)) + qsizetype(bodySize);
    if (buffer.size() < frameSize)
        return DecodeStatus::Incomplete;

    const QByteArray body = buffer.mid(sizeof(quint32), bodySize);
    buffer.remove(0, frameSize);
    QDataStream bodyStream(body);
    bodyStream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    QVariantMap decoded;
    bodyStream >> magic >> version >> decoded;
    if (bodyStream.status() != QDataStream::Ok || magic != Magic) {
        setError(errorMessage, QStringLiteral("Printer-service frame is corrupt."));
        return DecodeStatus::Invalid;
    }
    if (version != Version) {
        setError(errorMessage,
                 QStringLiteral("Printer-service protocol version %1 is unsupported; expected %2.")
                     .arg(version).arg(Version));
        return DecodeStatus::Invalid;
    }
    *message = decoded;
    return DecodeStatus::Complete;
}

bool isAllowedBridgeAddress(const QHostAddress& address)
{
    return address == QHostAddress::LocalHost ||
           address == QHostAddress::LocalHostIPv6;
}

QVariantMap settingsToMap(const DirectPrintSettings& settings)
{
    return {
        {QStringLiteral("printerIndex"), settings.printerIndex},
        {QStringLiteral("printDirection"), settings.printDirection},
        {QStringLiteral("printSpeed"), settings.printSpeed},
        {QStringLiteral("wcSequence"), settings.wcSequence},
        {QStringLiteral("eclosionGrade"), settings.eclosionGrade},
        {QStringLiteral("headSelect"), settings.headSelect},
        {QStringLiteral("whiteInkPercent"), settings.whiteInkPercent},
        {QStringLiteral("whiteInkPassCount"), settings.whiteInkPassCount},
        {QStringLiteral("varnishInkPercent"), settings.varnishInkPercent},
        {QStringLiteral("varnishInkPassCount"), settings.varnishInkPassCount},
        {QStringLiteral("headVoltage"), settings.headVoltage},
        {QStringLiteral("disableUv0"), settings.disableUv0},
        {QStringLiteral("disableUv1"), settings.disableUv1},
        {QStringLiteral("disableUv2"), settings.disableUv2},
        {QStringLiteral("disableUv3"), settings.disableUv3},
        {QStringLiteral("disableUv4"), settings.disableUv4},
        {QStringLiteral("disableUv5"), settings.disableUv5},
        {QStringLiteral("carReset"), settings.carReset},
        {QStringLiteral("stripBlank"), settings.stripBlank},
        {QStringLiteral("blankDistance"), settings.blankDistance},
        {QStringLiteral("printOffsetXmm"), settings.printOffsetXmm},
        {QStringLiteral("printOffsetYmm"), settings.printOffsetYmm},
        {QStringLiteral("mediaHeightMm"), settings.mediaHeightMm},
        {QStringLiteral("pass"), settings.pass},
        {QStringLiteral("vsdMode"), settings.vsdMode},
    };
}

bool settingsFromMap(const QVariantMap& map, DirectPrintSettings* settings,
                     QString* errorMessage)
{
    if (!settings) {
        setError(errorMessage, QStringLiteral("Print settings destination is null."));
        return false;
    }
    DirectPrintSettings value;
#define PF_SETTING_INT(name) value.name = map.value(QStringLiteral(#name), value.name).toInt()
    PF_SETTING_INT(printerIndex);
    PF_SETTING_INT(printDirection);
    PF_SETTING_INT(printSpeed);
    PF_SETTING_INT(wcSequence);
    PF_SETTING_INT(eclosionGrade);
    PF_SETTING_INT(headSelect);
    PF_SETTING_INT(whiteInkPercent);
    PF_SETTING_INT(whiteInkPassCount);
    PF_SETTING_INT(varnishInkPercent);
    PF_SETTING_INT(varnishInkPassCount);
    PF_SETTING_INT(headVoltage);
    PF_SETTING_INT(disableUv0);
    PF_SETTING_INT(disableUv1);
    PF_SETTING_INT(disableUv2);
    PF_SETTING_INT(disableUv3);
    PF_SETTING_INT(disableUv4);
    PF_SETTING_INT(disableUv5);
    PF_SETTING_INT(carReset);
    PF_SETTING_INT(stripBlank);
    PF_SETTING_INT(blankDistance);
    PF_SETTING_INT(printOffsetXmm);
    PF_SETTING_INT(printOffsetYmm);
    PF_SETTING_INT(pass);
    PF_SETTING_INT(vsdMode);
#undef PF_SETTING_INT
    value.mediaHeightMm = map.value(
        QStringLiteral("mediaHeightMm"), value.mediaHeightMm).toDouble();
    *settings = value;
    return true;
}

QVariantMap rasterMetadata(const DirectPrintRaster& raster)
{
    QVariantList channels;
    for (const int channel : raster.channelOrder)
        channels.append(channel);
    QVariantList header;
    for (const uint32_t word : raster.canonicalHeader)
        header.append(qulonglong(word));
    return {
        {QStringLiteral("width"), raster.width},
        {QStringLiteral("height"), raster.height},
        {QStringLiteral("xdpi"), raster.xdpi},
        {QStringLiteral("ydpi"), raster.ydpi},
        {QStringLiteral("bytesPerLine"), raster.bytesPerLine},
        {QStringLiteral("format"), int(raster.format)},
        {QStringLiteral("channels"), channels},
        {QStringLiteral("canonicalHeader"), header},
    };
}

QVariantMap spoolMetadata(const DirectPrintSpool& spool)
{
    QVariantList channels;
    for (const int channel : spool.channelOrder)
        channels.append(channel);
    QVariantList header;
    for (const uint32_t word : spool.canonicalHeader)
        header.append(qulonglong(word));
    return {
        {QStringLiteral("width"), spool.width},
        {QStringLiteral("height"), spool.height},
        {QStringLiteral("xdpi"), spool.xdpi},
        {QStringLiteral("ydpi"), spool.ydpi},
        {QStringLiteral("bytesPerLine"), spool.bytesPerLine},
        {QStringLiteral("format"), int(spool.format)},
        {QStringLiteral("logicalChannelCount"), spool.logicalChannelCount},
        {QStringLiteral("channels"), channels},
        {QStringLiteral("canonicalHeader"), header},
        {QStringLiteral("bodyOffset"), qulonglong(spool.bodyOffset)},
        {QStringLiteral("bodyBytes"), qulonglong(spool.bodyBytes)},
        {QStringLiteral("sha256"), spool.sha256},
    };
}

bool spoolMetadataFromMap(const QVariantMap& map, DirectPrintSpool* spool,
                          QString* errorMessage)
{
    if (!spool) {
        setError(errorMessage, QStringLiteral("Raster spool destination is null."));
        return false;
    }
    DirectPrintSpool value;
    value.width = map.value(QStringLiteral("width")).toInt();
    value.height = map.value(QStringLiteral("height")).toInt();
    value.xdpi = map.value(QStringLiteral("xdpi")).toInt();
    value.ydpi = map.value(QStringLiteral("ydpi")).toInt();
    value.bytesPerLine = map.value(QStringLiteral("bytesPerLine")).toInt();
    value.logicalChannelCount = map.value(
        QStringLiteral("logicalChannelCount")).toInt();
    value.bodyOffset = map.value(QStringLiteral("bodyOffset")).toULongLong();
    value.bodyBytes = map.value(QStringLiteral("bodyBytes")).toULongLong();
    value.sha256 = map.value(QStringLiteral("sha256")).toByteArray();
    const int format = map.value(QStringLiteral("format")).toInt();
    if (format < int(DirectPrintRasterFormat::Unspecified) ||
        format > int(DirectPrintRasterFormat::NocaiMultiInk)) {
        setError(errorMessage, QStringLiteral("Raster spool format is invalid."));
        return false;
    }
    value.format = static_cast<DirectPrintRasterFormat>(format);
    const QVariantList channels = map.value(QStringLiteral("channels")).toList();
    for (const QVariant& item : channels)
        value.channelOrder.push_back(item.toInt());
    const QVariantList header = map.value(
        QStringLiteral("canonicalHeader")).toList();
    if (header.size() != int(value.canonicalHeader.size())) {
        setError(errorMessage, QStringLiteral("Raster spool canonical header is invalid."));
        return false;
    }
    for (int index = 0; index < header.size(); ++index)
        value.canonicalHeader[size_t(index)] = header[index].toUInt();
    if (value.sha256.size() != 32 || value.bodyOffset == 0 ||
        value.bodyBytes == 0 || value.channelOrder.empty()) {
        setError(errorMessage, QStringLiteral("Raster spool metadata is incomplete."));
        return false;
    }
    for (const int channel : value.channelOrder) {
        if (channel < 0 || channel >= value.logicalChannelCount) {
            setError(errorMessage, QStringLiteral("Raster spool channel order is invalid."));
            return false;
        }
    }
    *spool = std::move(value);
    return true;
}

bool serializeRaster(const DirectPrintRaster& raster, QByteArray* payload,
                     QString* errorMessage)
{
    if (!payload || !raster.packedLines) {
        setError(errorMessage, QStringLiteral("Direct-print raster storage is unavailable."));
        return false;
    }
    qsizetype totalSize = 0;
    if (!checkedRasterSize(raster.height, int(raster.channelOrder.size()),
                           raster.bytesPerLine, &totalSize)) {
        setError(errorMessage, QStringLiteral("Direct-print raster dimensions are invalid or too large."));
        return false;
    }

    payload->clear();
    payload->reserve(totalSize);
    for (int row = 0; row < raster.height; ++row) {
        for (const int channel : raster.channelOrder) {
            if (channel < 0 || channel >= int(raster.packedLines->size()) ||
                row >= int((*raster.packedLines)[channel].size())) {
                setError(errorMessage, QStringLiteral("Direct-print raster channel or row is missing."));
                payload->clear();
                return false;
            }
            const auto& line = (*raster.packedLines)[channel][row];
            if (line.size() != size_t(raster.bytesPerLine)) {
                setError(errorMessage, QStringLiteral("Direct-print raster plane-line size is invalid."));
                payload->clear();
                return false;
            }
            payload->append(reinterpret_cast<const char*>(line.data()),
                            raster.bytesPerLine);
        }
    }
    return payload->size() == totalSize;
}

bool deserializeRaster(
    const QVariantMap& metadata, const QByteArray& payload,
    std::vector<std::vector<std::vector<uint8_t>>>* storage,
    DirectPrintRaster* raster, QString* errorMessage)
{
    if (!storage || !raster) {
        setError(errorMessage, QStringLiteral("Direct-print raster destination is null."));
        return false;
    }

    DirectPrintRaster value;
    value.width = metadata.value(QStringLiteral("width")).toInt();
    value.height = metadata.value(QStringLiteral("height")).toInt();
    value.xdpi = metadata.value(QStringLiteral("xdpi")).toInt();
    value.ydpi = metadata.value(QStringLiteral("ydpi")).toInt();
    value.bytesPerLine = metadata.value(QStringLiteral("bytesPerLine")).toInt();
    const int format = metadata.value(QStringLiteral("format")).toInt();
    if (format < int(DirectPrintRasterFormat::Unspecified) ||
        format > int(DirectPrintRasterFormat::NocaiMultiInk)) {
        setError(errorMessage, QStringLiteral("Direct-print raster format is invalid."));
        return false;
    }
    value.format = static_cast<DirectPrintRasterFormat>(format);

    int maximumChannel = -1;
    const QVariantList channels = metadata.value(QStringLiteral("channels")).toList();
    for (const QVariant& item : channels) {
        const int channel = item.toInt();
        if (channel < 0 || channel > 15) {
            setError(errorMessage, QStringLiteral("Direct-print channel index is invalid."));
            return false;
        }
        value.channelOrder.push_back(channel);
        maximumChannel = std::max(maximumChannel, channel);
    }
    qsizetype expectedSize = 0;
    if (!checkedRasterSize(value.height, int(value.channelOrder.size()),
                           value.bytesPerLine, &expectedSize) ||
        payload.size() != expectedSize) {
        setError(errorMessage, QStringLiteral("Direct-print raster payload length is invalid."));
        return false;
    }

    const QVariantList header = metadata.value(
        QStringLiteral("canonicalHeader")).toList();
    if (header.size() != int(value.canonicalHeader.size())) {
        setError(errorMessage, QStringLiteral("Direct-print canonical header is invalid."));
        return false;
    }
    for (int index = 0; index < header.size(); ++index)
        value.canonicalHeader[size_t(index)] = header[index].toUInt();

    storage->assign(size_t(maximumChannel + 1),
                    std::vector<std::vector<uint8_t>>(size_t(value.height)));
    qsizetype offset = 0;
    for (int row = 0; row < value.height; ++row) {
        for (const int channel : value.channelOrder) {
            const auto* begin = reinterpret_cast<const uint8_t*>(
                payload.constData() + offset);
            (*storage)[size_t(channel)][size_t(row)] =
                std::vector<uint8_t>(begin, begin + value.bytesPerLine);
            offset += value.bytesPerLine;
        }
    }
    value.packedLines = storage;
    *raster = value;
    return true;
}

} // namespace PrintFlowPrinterServiceProtocol
