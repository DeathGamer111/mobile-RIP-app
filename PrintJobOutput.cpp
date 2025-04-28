#include "PrintJobOutput.h"
#include "PrintJob.h"

#include <cups/ipp.h>
#include <cups/http.h>

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QMimeDatabase>
#include <QUrl>
#include <QSet>


PrintJobOutput::PrintJobOutput(QObject *parent) : QObject(parent) { refreshDetectedPrinters(); }

PrintJobOutput::~PrintJobOutput() {
    if (printerInfo) {
        cupsFreeDestInfo(printerInfo);
    }
    if (ppd) {
        ppdClose(ppd);
    }
}

bool PrintJobOutput::loadPrinter(const QString &printerName) {
    this->printerName = printerName;

    int num_dests;
    cups_dest_t *dests, *dest;
    num_dests = cupsGetDests(&dests);
    dest = cupsGetDest(printerName.toUtf8().constData(), nullptr, num_dests, dests);

    if (!dest) {
        qWarning() << "Printer not found:" << printerName;
        cupsFreeDests(num_dests, dests);
        return false;
    }

    if (printerInfo) {
        cupsFreeDestInfo(printerInfo);
    }

    printerInfo = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);

    cupsFreeDests(num_dests, dests);

    if (!printerInfo) {
        qWarning() << "Failed to get detailed printer info for:" << printerName;
        return false;
    }

    return true;
}


bool PrintJobOutput::loadPPDFile(const QString &ppdPath) {

    QString localInputPath = QUrl(ppdPath).toLocalFile();

    ppd_file_t *ppd = ppdOpenFile(localInputPath.toUtf8().constData());
    if (!ppd) {
        qWarning() << "Failed to load PPD file:" << ppdPath;
        return false;
    }

    ppdMarkDefaults(ppd);
    cups_option_t *options = nullptr;
    int num_options = 0;

    // Optionally store this ppd in a member variable if you plan to use it later
    if (this->ppd) {
        ppdClose(this->ppd);
    }
    this->ppd = ppd;

    qDebug() << "PPD file loaded successfully.";
    return true;
}


bool PrintJobOutput::registerPrinterFromPPD(const QString &printerName, const QString &ppdPath) {

    http_t *http = httpConnect2("localhost", ippPort(), nullptr, AF_UNSPEC, HTTP_ENCRYPT_IF_REQUESTED, 1, 3000, nullptr);

    if (!http) {
        qWarning() << "Failed to connect to CUPS server.";
        return false;
    }

    ipp_t *request = ippNewRequest(CUPS_ADD_PRINTER);
    if (!request) {
        qWarning() << "Failed to create IPP request.";
        httpClose(http);
        return false;
    }

    QString uri = "ipp://localhost/printers/" + printerName;

    QString filename = printerName + ".ppd";

    // Set printer URI
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, uri.toUtf8().constData());

    // Set printer location or info (optional)
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", nullptr, printerName.toUtf8().constData());

    // Set device URI to a "null" backend, we only need the filter pipeline
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "device-uri", nullptr, "file:/dev/null");

    // Set PPD file path (important!)
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME, "ppd-name", nullptr, filename.toUtf8().constData());

    // Send the request
    ipp_t *response = cupsDoRequest(http, request, "/admin/");

    if (!response || ippGetStatusCode(response) > IPP_OK_CONFLICT) {
        qWarning() << "IPP Add Printer failed:" << cupsLastErrorString();
        if (response) ippDelete(response);
        httpClose(http);
        return false;
    }

    ippDelete(response);
    httpClose(http);

    qDebug() << "Printer" << printerName << "registered from PPD!";
    return true;
}


QStringList PrintJobOutput::detectedPrinters() const {
    return m_detectedPrinters;
}


void PrintJobOutput::refreshDetectedPrinters() {
    QStringList printerList;

    int num_dests = 0;
    cups_dest_t* dests = nullptr;

    num_dests = cupsGetDests(&dests);
    if (num_dests > 0 && dests) {
        for (int i = 0; i < num_dests; ++i) {
            const char* name = dests[i].name;
            if (name) {
                printerList.append(QString::fromUtf8(name));
            }
        }
    }

    cupsFreeDests(num_dests, dests);

    // Only update if changed
    if (m_detectedPrinters != printerList) {
        m_detectedPrinters = printerList;
        emit detectedPrintersChanged();
    }
}


void PrintJobOutput::markPpdOptionsFromJob(const PrintJob &job) {
    if (!ppd) {
        qWarning() << "PPD not loaded. Cannot mark options.";
        return;
    }

    // You can adapt these mappings to actual PPD keys as needed
    if (!job.whiteStrategy.isEmpty()) {
        ppdMarkOption(ppd, "WhiteStrategy", job.whiteStrategy.toUtf8().constData());
    }

    if (!job.varnishType.isEmpty()) {
        ppdMarkOption(ppd, "VarnishType", job.varnishType.toUtf8().constData());
    }

    if (!job.colorProfile.isEmpty()) {
        ppdMarkOption(ppd, "ColorProfile", job.colorProfile.toUtf8().constData());
    }

    // Example: Handle page size from job
    if (job.paperSize == QSize(210, 297)) {
        ppdMarkOption(ppd, "PageSize", "A4");
    } else if (job.paperSize == QSize(216, 279)) {
        ppdMarkOption(ppd, "PageSize", "Letter");
    } else if (job.paperSize == QSize(279, 432)) {
        ppdMarkOption(ppd, "PageSize", "Tabloid");
    } else {
        // Could define a "Custom" mapping or fallback
        qDebug() << "Custom paper size:" << job.paperSize;
    }
}


QStringList PrintJobOutput::supportedResolutions() const {
    return getSupportedValues("Resolution");
}


QStringList PrintJobOutput::supportedMediaSizes() const {
    return getSupportedValues("media");
}


QStringList PrintJobOutput::supportedDuplexModes() const {
    return getSupportedValues("Duplex");
}


QStringList PrintJobOutput::supportedColorModes() const {
    return getSupportedValues("PrintColorMode");
}


bool PrintJobOutput::isOptionSupported(const QString &option) const {
    if (!printerInfo || printerName.isEmpty()) return false;

    int num_dests;
    cups_dest_t *dests, *dest;
    num_dests = cupsGetDests(&dests);
    dest = cupsGetDest(printerName.toUtf8().constData(), nullptr, num_dests, dests);

    if (!dest) {
        cupsFreeDests(num_dests, dests);
        return false;
    }

    bool supported = cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, printerInfo, option.toUtf8().constData(), nullptr);

    cupsFreeDests(num_dests, dests);
    return supported;
}

bool PrintJobOutput::isOptionValueSupported(const QString &option, const QString &value) const {
    if (!printerInfo || printerName.isEmpty()) return false;

    int num_dests;
    cups_dest_t *dests, *dest;
    num_dests = cupsGetDests(&dests);
    dest = cupsGetDest(printerName.toUtf8().constData(), nullptr, num_dests, dests);

    if (!dest) {
        cupsFreeDests(num_dests, dests);
        return false;
    }

    bool supported = cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, printerInfo,
                                            option.toUtf8().constData(),
                                            value.toUtf8().constData());

    cupsFreeDests(num_dests, dests);
    return supported;
}

QString PrintJobOutput::getDefaultOptionValue(const QString &option) const {
    if (!printerInfo || printerName.isEmpty()) return QString();

    int num_dests;
    cups_dest_t *dests, *dest;
    num_dests = cupsGetDests(&dests);
    dest = cupsGetDest(printerName.toUtf8().constData(), nullptr, num_dests, dests);

    if (!dest) {
        cupsFreeDests(num_dests, dests);
        return QString();
    }

    ipp_attribute_t *attr = cupsFindDestDefault(CUPS_HTTP_DEFAULT, dest, printerInfo, option.toUtf8().constData());

    QString def;
    if (attr && ippGetCount(attr) > 0) {
        def = QString::fromUtf8(ippGetString(attr, 0, nullptr));
    }

    cupsFreeDests(num_dests, dests);
    return def;
}

QStringList PrintJobOutput::getSupportedValues(const QString &option) const {
    QStringList values;

    if (!printerInfo || printerName.isEmpty()) return values;

    int num_dests;
    cups_dest_t *dests, *dest;
    num_dests = cupsGetDests(&dests);
    dest = cupsGetDest(printerName.toUtf8().constData(), nullptr, num_dests, dests);

    if (!dest) {
        cupsFreeDests(num_dests, dests);
        return values;
    }

    ipp_attribute_t *attr = cupsFindDestSupported(CUPS_HTTP_DEFAULT, dest, printerInfo, option.toUtf8().constData());

    if (attr) {
        int count = ippGetCount(attr);
        for (int i = 0; i < count; ++i) {
            const char *val = ippGetString(attr, i, nullptr);
            if (val) values.append(QString::fromUtf8(val));
        }
    }

    cupsFreeDests(num_dests, dests);
    return values;
}


static const char* inferCupsMimeType(const QString &path) {
    QMimeDatabase db;
    QString mime = db.mimeTypeForFile(path).name();

    if (mime.startsWith("image/jpeg"))      return "image/jpeg";
    if (mime.startsWith("image/png"))       return "image/png";
    if (mime.startsWith("image/tiff"))      return "image/tiff";
    if (mime.startsWith("application/pdf")) return "application/pdf";
    if (mime.startsWith("image/svg+xml"))   return "image/svg+xml";

    return "application/octet-stream"; // fallback
}


// Called by frontend to pass the Job into PrintJobOutput
bool PrintJobOutput::generatePRN(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath) {
    PrintJob job;
    job.name = jobMap["name"].toString();
    job.imagePath = jobMap["imagePath"].toString();
    job.paperSize = jobMap["paperSize"].toSize();
    job.resolution = jobMap["resolution"].toSize();
    job.offset = jobMap["offset"].toPoint();
    job.whiteStrategy = jobMap["whiteStrategy"].toString();
    job.varnishType = jobMap["varnishType"].toString();
    job.colorProfile = jobMap["colorProfile"].toString();

    // Add other fields as needed

    return generatePRN(job, inputFile, outputPath);
}


bool PrintJobOutput::generatePRN(const PrintJob &job, const QString &inputFile, const QString &outputPath) {
    if (printerName.isEmpty()) {
        qWarning() << "Printer not loaded.";
        return false;
    }

    QString localPath = QUrl(inputFile).toLocalFile();
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open input file:" << inputFile;
        return false;
    }

    const char *format = inferCupsMimeType(localPath);
    QByteArray fileData = file.readAll();
    file.close();

    http_t *http = httpConnect2("localhost", ippPort(), nullptr, AF_UNSPEC,
                                HTTP_ENCRYPT_IF_REQUESTED, 1, 3000, nullptr);
    if (!http) {
        qWarning() << "Failed to connect to CUPS server.";
        return false;
    }

    cups_dest_t *dest = cupsGetNamedDest(http, printerName.toUtf8().constData(), nullptr);
    if (!dest) {
        qWarning() << "Failed to get printer destination:" << printerName;
        httpClose(http);
        return false;
    }

    cups_option_t *options = nullptr;
    int num_options = 0;

    // Only add outputfile option if printing to a simulated printer
    if (!outputPath.isEmpty()) {
        num_options = cupsAddOption("outputfile", outputPath.toUtf8().constData(), num_options, &options);
    }

    // Create the print job
    int job_id = cupsCreateJob(http, dest->name, job.name.toUtf8().constData(), num_options, options);
    if (job_id <= 0) {
        qWarning() << "Failed to create CUPS job:" << cupsLastErrorString();
        cupsFreeOptions(num_options, options);
        cupsFreeDests(1, dest);
        httpClose(http);
        return false;
    }

    // Start the document
    if (!cupsStartDocument(http, dest->name, job_id, localPath.toUtf8().constData(), format, 1)) {
        qWarning() << "Failed to start CUPS document:" << cupsLastErrorString();
        cupsFreeOptions(num_options, options);
        cupsFreeDests(1, dest);
        httpClose(http);
        return false;
    }

    // Write document data
    if (!cupsWriteRequestData(http, fileData.constData(), fileData.size())) {
        qWarning() << "Failed to write document data.";
        cupsFreeOptions(num_options, options);
        cupsFreeDests(1, dest);
        httpClose(http);
        return false;
    }

    // Finish document
    ipp_status_t status = cupsLastError();
    if (!cupsFinishDocument(http, dest->name)) {
        if (status < IPP_OK || status >= IPP_REDIRECTION_OTHER_SITE) {
            qWarning() << "Failed to finish document:" << cupsLastErrorString();
            cupsFreeOptions(num_options, options);
            cupsFreeDests(1, dest);
            httpClose(http);
            return false;
        } else {
            qDebug() << "cupsFinishDocument returned false, but IPP status is OK:" << cupsLastErrorString();
        }
    }

    cupsFreeOptions(num_options, options);
    cupsFreeDests(1, dest);
    httpClose(http);

    // Optional check for PRN existence
    if (!outputPath.isEmpty() && !QFile::exists(outputPath)) {
        qWarning() << "Expected PRN file not found at:" << outputPath;
        return false;
    }

    return true;
}


bool PrintJobOutput::generatePRNviaFilter(const QVariantMap &jobMap, const QString &inputFile, const QString &outputPath) {
    PrintJob job;
    job.name = jobMap["name"].toString();
    job.imagePath = jobMap["imagePath"].toString();
    job.paperSize = jobMap["paperSize"].toSize();
    job.resolution = jobMap["resolution"].toSize();
    job.offset = jobMap["offset"].toPoint();
    job.whiteStrategy = jobMap["whiteStrategy"].toString();
    job.varnishType = jobMap["varnishType"].toString();
    job.colorProfile = jobMap["colorProfile"].toString();
    QString ppdPath = "/home/mccalla/Downloads/Epson_SC_T5000.ppd";
    return generatePRNviaFilter(job, ppdPath, inputFile, outputPath);
}


bool PrintJobOutput::generatePRNviaFilter(const PrintJob &job, const QString ppdPath, const QString &inputFile, const QString &outputPath) {

    QString localOutputPath = QUrl(outputPath).toLocalFile();
    QString localInputPath = QUrl(inputFile).toLocalFile();

    QString command = QString("cupsfilter -P \"%1\" -m application/vnd.cups-raster \"%2\" > \"%3\"")
                          .arg(ppdPath)
                          .arg(localInputPath)
                          .arg(localOutputPath);

    qDebug() << "Executing:" << command;

    int result = system(command.toUtf8().constData());
    if (result != 0) {
        qWarning() << "cupsfilter failed with code:" << result;
        return false;
    }

    return QFile::exists(outputPath);
}
