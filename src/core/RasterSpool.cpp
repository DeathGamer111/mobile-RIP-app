#include "RasterSpool.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <algorithm>
#include <limits>

namespace PrintFlowRasterSpool {
namespace {

void setError(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}

QByteArray encodeHeader(const DirectPrintSpool& spool, const QByteArray& checksum)
{
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(Magic) << quint16(Version) << quint64(HeaderBytes)
        << qint32(spool.width) << qint32(spool.height)
        << qint32(spool.xdpi) << qint32(spool.ydpi)
        << qint32(spool.bytesPerLine) << quint8(spool.format)
        << qint32(spool.logicalChannelCount) << quint64(spool.bodyBytes)
        << checksum;
    for (const uint32_t word : spool.canonicalHeader)
        out << quint32(word);
    out << qint32(spool.channelOrder.size());
    for (const int channel : spool.channelOrder)
        out << qint32(channel);
    if (out.status() != QDataStream::Ok || header.size() > qsizetype(HeaderBytes))
        return {};
    header.resize(qsizetype(HeaderBytes), '\0');
    return header;
}

bool decodeHeader(const QByteArray& header, const QString& path,
                  DirectPrintSpool* spool, QString* errorMessage)
{
    if (!spool || header.size() != qsizetype(HeaderBytes)) {
        setError(errorMessage, QStringLiteral("Raster spool header is truncated."));
        return false;
    }
    QDataStream in(header);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    quint64 headerBytes = 0;
    qint32 width = 0, height = 0, xdpi = 0, ydpi = 0, bytesPerLine = 0;
    quint8 format = 0;
    qint32 logicalChannels = 0, physicalChannels = 0;
    quint64 bodyBytes = 0;
    QByteArray checksum;
    in >> magic >> version >> headerBytes >> width >> height >> xdpi >> ydpi
       >> bytesPerLine >> format >> logicalChannels >> bodyBytes >> checksum;
    DirectPrintSpool decoded;
    decoded.path = path;
    decoded.width = width;
    decoded.height = height;
    decoded.xdpi = xdpi;
    decoded.ydpi = ydpi;
    decoded.bytesPerLine = bytesPerLine;
    decoded.format = static_cast<DirectPrintRasterFormat>(format);
    decoded.logicalChannelCount = logicalChannels;
    decoded.bodyOffset = HeaderBytes;
    decoded.bodyBytes = bodyBytes;
    decoded.sha256 = checksum;
    for (uint32_t& word : decoded.canonicalHeader) {
        quint32 value = 0;
        in >> value;
        word = value;
    }
    in >> physicalChannels;
    if (physicalChannels > 0 && physicalChannels <= 16) {
        decoded.channelOrder.reserve(physicalChannels);
        for (int index = 0; index < physicalChannels; ++index) {
            qint32 channel = -1;
            in >> channel;
            decoded.channelOrder.push_back(channel);
        }
    }
    if (in.status() != QDataStream::Ok || magic != Magic || version != Version ||
        headerBytes != HeaderBytes || !metadataIsValid(decoded, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("Raster spool header is invalid or unsupported.");
        return false;
    }
    *spool = std::move(decoded);
    return true;
}

QByteArray spoolChecksum(QFile& file, const DirectPrintSpool& spool,
                         QString* errorMessage,
                         const CancellationCheck& canceled)
{
    if (!file.seek(qint64(HeaderBytes))) {
        setError(errorMessage, QStringLiteral("Could not seek to raster spool data."));
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Hash a canonical zero-checksum header plus the body. This protects the
    // job dimensions, plane order, format, and printer header as well as the
    // packed raster bytes while avoiding a self-referential checksum field.
    const QByteArray canonicalHeader = encodeHeader(spool, {});
    if (canonicalHeader.size() != qsizetype(HeaderBytes)) {
        setError(errorMessage, QStringLiteral("Raster spool metadata could not be hashed."));
        return {};
    }
    hash.addData(canonicalHeader);
    quint64 remaining = spool.bodyBytes;
    while (remaining > 0) {
        if (canceled && canceled()) {
            setError(errorMessage, QStringLiteral("Raster spool hashing was canceled."));
            return {};
        }
        const qsizetype requested = qsizetype(std::min<quint64>(remaining, 1024 * 1024));
        const QByteArray chunk = file.read(requested);
        if (chunk.size() != requested) {
            setError(errorMessage, QStringLiteral("Raster spool data is truncated."));
            return {};
        }
        hash.addData(chunk);
        remaining -= quint64(chunk.size());
    }
    return hash.result();
}

} // namespace

QString scratchDirectory()
{
#if defined(Q_OS_ANDROID)
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
#endif
    if (root.isEmpty())
        root = QDir::tempPath();
    const QString path = QDir(root).filePath(QStringLiteral("PrintFlow-raster"));
    QDir().mkpath(path);
    return path;
}

quint64 expectedBodyBytes(const DirectPrintSpool& spool)
{
    if (spool.logicalChannelCount <= 0 || spool.height <= 0 ||
        spool.bytesPerLine <= 0)
        return 0;
    const quint64 lines = quint64(spool.logicalChannelCount) * quint64(spool.height);
    if (lines > std::numeric_limits<quint64>::max() / quint64(spool.bytesPerLine))
        return 0;
    return lines * quint64(spool.bytesPerLine);
}

bool metadataIsValid(const DirectPrintSpool& spool, QString* errorMessage)
{
    if (spool.width <= 0 || spool.height <= 0 || spool.xdpi <= 0 ||
        spool.ydpi <= 0 || spool.bytesPerLine <= 0 ||
        spool.logicalChannelCount <= 0 || spool.logicalChannelCount > 16 ||
        spool.channelOrder.empty() || spool.channelOrder.size() > 16) {
        setError(errorMessage, QStringLiteral("Raster spool dimensions or channel metadata are invalid."));
        return false;
    }
    const quint64 expectedLineBytes =
        ((quint64(spool.width) + 3ULL) / 4ULL + 3ULL) & ~3ULL;
    const quint64 logicalLines = quint64(spool.logicalChannelCount) *
        quint64(spool.height);
    if (expectedLineBytes != quint64(spool.bytesPerLine) ||
        logicalLines > quint64(std::numeric_limits<int>::max())) {
        setError(errorMessage, QStringLiteral("Raster spool 2-bpp line layout is invalid."));
        return false;
    }
    for (const int channel : spool.channelOrder) {
        if (channel < 0 || channel >= spool.logicalChannelCount) {
            setError(errorMessage, QStringLiteral("Raster spool channel order is invalid."));
            return false;
        }
    }
    const quint64 expected = expectedBodyBytes(spool);
    if (expected == 0 || (spool.bodyBytes != 0 && spool.bodyBytes != expected)) {
        setError(errorMessage, QStringLiteral("Raster spool byte count is invalid."));
        return false;
    }
    return true;
}

Writer::~Writer()
{
    if (!m_finalized)
        cancel();
}

bool Writer::create(const QString& directory, const DirectPrintSpool& metadata,
                    QString* errorMessage)
{
    cancel();
    m_metadata = metadata;
    m_metadata.bodyOffset = HeaderBytes;
    m_metadata.bodyBytes = expectedBodyBytes(m_metadata);
    m_metadata.sha256.clear();
    if (!metadataIsValid(m_metadata, errorMessage))
        return false;
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        setError(errorMessage, QStringLiteral("Could not create the raster spool directory."));
        return false;
    }
    const QString token = QString::number(QRandomGenerator::global()->generate64(), 16);
    m_partialPath = dir.filePath(QStringLiteral("printflow-%1.pfrs.partial").arg(token));
    m_finalPath = dir.filePath(QStringLiteral("printflow-%1.pfrs").arg(token));
    m_file = std::make_unique<QFile>(m_partialPath);
    if (!m_file->open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        setError(errorMessage, QStringLiteral("Could not create raster spool: %1")
                                   .arg(m_file->errorString()));
        cancel();
        return false;
    }
    if (!writeHeader({}, errorMessage) ||
        !m_file->resize(qint64(HeaderBytes + m_metadata.bodyBytes))) {
        if (errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("Could not allocate raster spool storage: %1")
                                .arg(m_file->errorString());
        cancel();
        return false;
    }
    m_linesWritten.resize(m_metadata.logicalChannelCount * m_metadata.height);
    m_linesWritten.fill(false);
    return true;
}

quint64 Writer::lineOffset(int logicalChannel, int row) const
{
    return HeaderBytes +
        (quint64(logicalChannel) * quint64(m_metadata.height) + quint64(row)) *
            quint64(m_metadata.bytesPerLine);
}

bool Writer::writeLine(int logicalChannel, int row, const uint8_t* bytes,
                       qsizetype size, QString* errorMessage)
{
    if (!m_file || !m_file->isOpen() || !bytes ||
        logicalChannel < 0 || logicalChannel >= m_metadata.logicalChannelCount ||
        row < 0 || row >= m_metadata.height || size != m_metadata.bytesPerLine) {
        setError(errorMessage, QStringLiteral("Raster spool line write is invalid."));
        return false;
    }
    if (!m_file->seek(qint64(lineOffset(logicalChannel, row))) ||
        m_file->write(reinterpret_cast<const char*>(bytes), size) != size) {
        setError(errorMessage, QStringLiteral("Could not write raster spool line: %1")
                                   .arg(m_file->errorString()));
        return false;
    }
    m_linesWritten.setBit(logicalChannel * m_metadata.height + row);
    return true;
}

bool Writer::writeHeader(const QByteArray& checksum, QString* errorMessage)
{
    const QByteArray header = encodeHeader(m_metadata, checksum);
    if (header.isEmpty() || !m_file || !m_file->seek(0) ||
        m_file->write(header) != header.size()) {
        setError(errorMessage, QStringLiteral("Could not write raster spool header."));
        return false;
    }
    return true;
}

bool Writer::finalize(DirectPrintSpool* result, QString* errorMessage,
                      const CancellationCheck& canceled)
{
    if (!result || !m_file || !m_file->isOpen()) {
        setError(errorMessage, QStringLiteral("Raster spool is not open."));
        return false;
    }
    if (m_linesWritten.count(true) != m_linesWritten.size()) {
        setError(errorMessage, QStringLiteral("Raster spool is incomplete; one or more plane-lines were not written."));
        return false;
    }
    if (!m_file->flush()) {
        setError(errorMessage, QStringLiteral("Could not flush raster spool: %1")
                                   .arg(m_file->errorString()));
        return false;
    }
    const QByteArray checksum = spoolChecksum(
        *m_file, m_metadata, errorMessage, canceled);
    if (checksum.size() != 32 || !writeHeader(checksum, errorMessage) || !m_file->flush())
        return false;
    m_file->close();
    if (!QFile::rename(m_partialPath, m_finalPath)) {
        setError(errorMessage, QStringLiteral("Could not finalize raster spool."));
        return false;
    }
    m_metadata.path = m_finalPath;
    m_metadata.sha256 = checksum;
    *result = m_metadata;
    m_finalized = true;
    return true;
}

void Writer::cancel()
{
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
    if (!m_partialPath.isEmpty())
        QFile::remove(m_partialPath);
    m_partialPath.clear();
    m_finalPath.clear();
    m_linesWritten.clear();
    m_finalized = false;
}

QString Writer::partialPath() const
{
    return m_partialPath;
}

bool readMetadata(const QString& path, DirectPrintSpool* metadata,
                  QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Could not open raster spool: %1")
                                   .arg(file.errorString()));
        return false;
    }
    const QByteArray header = file.read(qsizetype(HeaderBytes));
    if (!decodeHeader(header, path, metadata, errorMessage))
        return false;
    if (quint64(file.size()) != HeaderBytes + metadata->bodyBytes) {
        setError(errorMessage, QStringLiteral("Raster spool file length is invalid."));
        return false;
    }
    return true;
}

bool verify(const DirectPrintSpool& spool, QString* errorMessage,
            const CancellationCheck& canceled)
{
    DirectPrintSpool diskMetadata;
    if (!readMetadata(spool.path, &diskMetadata, errorMessage))
        return false;
    QFile file(spool.path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    const QByteArray checksum = spoolChecksum(
        file, diskMetadata, errorMessage, canceled);
    if (checksum.size() != 32) {
        if (errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("Raster spool checksum could not be calculated.");
        return false;
    }
    if (checksum != diskMetadata.sha256 ||
        (!spool.sha256.isEmpty() && checksum != spool.sha256)) {
        setError(errorMessage, QStringLiteral("Raster spool checksum does not match."));
        return false;
    }
    return true;
}

bool Reader::open(const QString& path, bool verifyChecksum,
                  QString* errorMessage, const CancellationCheck& canceled)
{
    m_file.close();
    if (!readMetadata(path, &m_metadata, errorMessage))
        return false;
    if (verifyChecksum && !verify(m_metadata, errorMessage, canceled))
        return false;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, m_file.errorString());
        return false;
    }
    return true;
}

quint64 Reader::lineOffset(int logicalChannel, int row) const
{
    return m_metadata.bodyOffset +
        (quint64(logicalChannel) * quint64(m_metadata.height) + quint64(row)) *
            quint64(m_metadata.bytesPerLine);
}

bool Reader::readLine(int logicalChannel, int row, QByteArray* line,
                      QString* errorMessage)
{
    if (!line || !m_file.isOpen() || logicalChannel < 0 ||
        logicalChannel >= m_metadata.logicalChannelCount || row < 0 ||
        row >= m_metadata.height ||
        !m_file.seek(qint64(lineOffset(logicalChannel, row)))) {
        setError(errorMessage, QStringLiteral("Raster spool line read is invalid."));
        return false;
    }
    *line = m_file.read(m_metadata.bytesPerLine);
    if (line->size() != m_metadata.bytesPerLine) {
        setError(errorMessage, QStringLiteral("Raster spool line is truncated."));
        return false;
    }
    return true;
}

bool Reader::readBodyChunk(quint64 offset, qsizetype maximumBytes, QByteArray* data,
                           QString* errorMessage)
{
    if (!data || !m_file.isOpen() || maximumBytes <= 0 || offset > m_metadata.bodyBytes) {
        setError(errorMessage, QStringLiteral("Raster spool chunk read is invalid."));
        return false;
    }
    const quint64 remaining = m_metadata.bodyBytes - offset;
    const qsizetype size = qsizetype(std::min<quint64>(remaining, quint64(maximumBytes)));
    if (!m_file.seek(qint64(m_metadata.bodyOffset + offset))) {
        setError(errorMessage, QStringLiteral("Could not seek within raster spool."));
        return false;
    }
    *data = m_file.read(size);
    if (data->size() != size) {
        setError(errorMessage, QStringLiteral("Raster spool chunk is truncated."));
        return false;
    }
    return true;
}

void remove(const DirectPrintSpool& spool)
{
    if (!spool.path.isEmpty())
        QFile::remove(spool.path);
}

void removeStalePartials(const QString& directory)
{
    QDir dir(directory);
    const QFileInfoList files = dir.entryInfoList(
        {QStringLiteral("printflow-*.pfrs.partial")}, QDir::Files);
    for (const QFileInfo& file : files)
        QFile::remove(file.absoluteFilePath());
}

} // namespace PrintFlowRasterSpool
