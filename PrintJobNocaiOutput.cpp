#include "PrintJobNocaiOutput.h"
#include <QProcess>
#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <QDebug>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDataStream>

PrintJobNocaiOutput::PrintJobNocaiOutput(QObject* parent) : QObject(parent) {}


PrintJobNocaiOutput::~PrintJobNocaiOutput() {}


bool PrintJobNocaiOutput::generatePRN(const QString& imagePath, const QString& outputPath) {
    qDebug() << "Starting PRN generation...";

    // Step 1: Rasterize to temporary PRN
    QTemporaryFile tempRasterFile;
    tempRasterFile.setAutoRemove(false); // Keep file for header merge
    if (!tempRasterFile.open()) {
        qWarning() << "Unable to open temporary raster file.";
        return false;
    }
    QString tempRasterPath = tempRasterFile.fileName();
    tempRasterFile.close(); // Will write with cupsfilter


    QString jobPath = QUrl(imagePath).toLocalFile();
    QFile file(jobPath);
    if (!rasterizeJob(jobPath, tempRasterPath)) {
        qWarning() << "Rasterization failed.";
        return false;
    }


    // Step 2: Compose PRN with optional 48-byte header
    QString outputJobPath = QUrl(outputPath).toLocalFile();
    QFile finalOutput(outputJobPath);
    if (!finalOutput.open(QIODevice::WriteOnly)) {
        qWarning() << "Unable to open output PRN file for writing.";
        return false;
    }

    QDataStream out(&finalOutput);

    // === BEGIN OPTIONAL HEADER PREPEND ===
    // struct PrintJobProperty (48 bytes), fake placeholder example
    // QByteArray header(48, 0); // All zeros
    // qToLittleEndian<quint32>(0x5555, reinterpret_cast<uchar*>(header.data())); // HDR_FLAG
    // out.writeRawData(header.data(), header.size());
    // === END OPTIONAL HEADER PREPEND ===

    // Step 3: Append raster data
    QFile rasterIn(tempRasterPath);
    if (!rasterIn.open(QIODevice::ReadOnly)) {
        qWarning() << "Unable to open raster file.";
        return false;
    }

    out.writeRawData(rasterIn.readAll().constData(), rasterIn.size());
    rasterIn.close();

    finalOutput.close();
    QFile::remove(tempRasterPath); // Cleanup temp file

    qDebug() << "PRN created successfully at" << outputPath;
    return true;
}


bool PrintJobNocaiOutput::rasterizeJob(const QString& inputPath, const QString& outputPath) {
    if (!QFile::exists(inputPath)) {
        qWarning() << "Input file does not exist:" << inputPath;
        return false;
    }

    QString cupsfilterPath = QStandardPaths::findExecutable("cupsfilter");
    if (cupsfilterPath.isEmpty()) {
        qWarning() << "cupsfilter not found in PATH.";
        return false;
    }

    QStringList arguments;
    arguments << inputPath
              << "-e"
              << "-m" << "image/pwg-raster"
              << "-o" << "media=A4"
              << "-o" << "resolution=600dpi";

    qDebug() << "Running:" << cupsfilterPath << arguments;

    QProcess process;
    process.setStandardOutputFile(outputPath);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(cupsfilterPath, arguments);

    if (!process.waitForStarted()) {
        qWarning() << "Failed to start cupsfilter.";
        return false;
    }

    if (!process.waitForFinished()) {
        qWarning() << "cupsfilter did not finish properly.";
        return false;
    }

    if (process.exitCode() != 0) {
        qWarning() << "cupsfilter exited with error:" << process.exitCode();
        qDebug() << "cupsfilter stderr:" << process.readAllStandardOutput();
        return false;
    }

    return true;
}


// ==== Stubbed DLL functions (to be implemented later) ====
bool PrintJobNocaiOutput::loadDLL() {
    return false;
}

bool PrintJobNocaiOutput::printFromPRN(const QString& prnPath) {
    return false;
}

void PrintJobNocaiOutput::exitPrinter() {

}
