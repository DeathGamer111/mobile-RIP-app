#pragma once

#include "IPrintOutputClient.h"

#include <QFile>
#include <QBitArray>
#include <QString>

#include <memory>
#include <functional>

namespace PrintFlowRasterSpool {

constexpr quint32 Magic = 0x50465253u; // PFRS
constexpr quint16 Version = 1;
constexpr quint64 HeaderBytes = 4096;
using CancellationCheck = std::function<bool()>;

QString scratchDirectory();
bool metadataIsValid(const DirectPrintSpool& spool, QString* errorMessage = nullptr);
quint64 expectedBodyBytes(const DirectPrintSpool& spool);

class Writer
{
public:
    Writer() = default;
    ~Writer();

    bool create(const QString& directory, const DirectPrintSpool& metadata,
                QString* errorMessage = nullptr);
    bool writeLine(int logicalChannel, int row, const uint8_t* bytes,
                   qsizetype size, QString* errorMessage = nullptr);
    bool finalize(DirectPrintSpool* result, QString* errorMessage = nullptr,
                  const CancellationCheck& canceled = {});
    void cancel();
    QString partialPath() const;

private:
    bool writeHeader(const QByteArray& checksum, QString* errorMessage);
    quint64 lineOffset(int logicalChannel, int row) const;

    std::unique_ptr<QFile> m_file;
    DirectPrintSpool m_metadata;
    QString m_partialPath;
    QString m_finalPath;
    QBitArray m_linesWritten;
    bool m_finalized = false;
};

class Reader
{
public:
    bool open(const QString& path, bool verifyChecksum = true,
              QString* errorMessage = nullptr,
              const CancellationCheck& canceled = {});
    bool readLine(int logicalChannel, int row, QByteArray* line,
                  QString* errorMessage = nullptr);
    bool readBodyChunk(quint64 offset, qsizetype maximumBytes, QByteArray* data,
                       QString* errorMessage = nullptr);
    const DirectPrintSpool& metadata() const { return m_metadata; }

private:
    quint64 lineOffset(int logicalChannel, int row) const;

    QFile m_file;
    DirectPrintSpool m_metadata;
};

bool readMetadata(const QString& path, DirectPrintSpool* metadata,
                  QString* errorMessage = nullptr);
bool verify(const DirectPrintSpool& spool, QString* errorMessage = nullptr,
            const CancellationCheck& canceled = {});
void remove(const DirectPrintSpool& spool);
void removeStalePartials(const QString& directory);

} // namespace PrintFlowRasterSpool
