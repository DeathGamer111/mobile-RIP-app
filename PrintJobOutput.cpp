#include "PrintJobOutput.h"
#include "PrintJob.h"

#include <cups/ipp.h>
#include <cups/http.h>

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QMimeDatabase>


PrintJobOutput::PrintJobOutput(QObject *parent) : QObject(parent) {}


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

    cupsFreeDests(num_dests, dests);
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

    // Set printer URI
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, uri.toUtf8().constData());

    // Set printer location or info (optional)
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT, "printer-info", nullptr, printerName.toUtf8().constData());

    // Set device URI to a "null" backend, we only need the filter pipeline
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "device-uri", nullptr, "file:/dev/null");

    // Set PPD file path (important!)
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME, "ppd-name", nullptr, ppdPath.toUtf8().constData());

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


bool PrintJobOutput::loadPPDFile(const QString &ppdPath) {
    ppd_file_t *ppd = ppdOpenFile(ppdPath.toUtf8().constData());
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
    return getOptionValues("Resolution");
}


QStringList PrintJobOutput::supportedMediaSizes() const {
    return getOptionValues("media");
}


QStringList PrintJobOutput::supportedDuplexModes() const {
    return getOptionValues("Duplex");
}


QStringList PrintJobOutput::supportedColorModes() const {
    return getOptionValues("PrintColorMode");
}


QStringList PrintJobOutput::getOptionValues(const QString &optionName) const {
    QStringList values;

    if (printerName.isEmpty()) {
        qWarning() << "Printer name not loaded.";
        return values;
    }

    cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, printerName.toUtf8().constData(), nullptr);
    if (!dest) {
        qWarning() << "Failed to get named destination for printer:" << printerName;
        return values;
    }

    http_t *http = httpConnect2("localhost", ippPort(), nullptr, AF_UNSPEC,
                                HTTP_ENCRYPT_IF_REQUESTED, 1, 3000, nullptr);
    if (!http) {
        qWarning() << "HTTP connection to CUPS server failed.";
        cupsFreeDests(1, dest);
        return values;
    }

    cups_dinfo_t *info = cupsCopyDestInfo(http, dest);
    if (!info) {
        qWarning() << "Failed to retrieve destination info.";
        httpClose(http);
        cupsFreeDests(1, dest);
        return values;
    }

    // Scan through options and collect any matching key names
    for (int i = 0; i < dest->num_options; ++i) {
        const char *name = dest->options[i].name;
        const char *val  = dest->options[i].value;
        if (QString::fromUtf8(name).compare(optionName, Qt::CaseInsensitive) == 0) {
            values.append(QString::fromUtf8(val));
        }
    }

    cupsFreeDestInfo(info);
    cupsFreeDests(1, dest);
    httpClose(http);

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

    QFile file(QUrl(inputFile).toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open input file:" << inputFile;
        return false;
    }

    const char *format = inferCupsMimeType(inputFile);

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

    // Set output file destination using IPP attribute
    num_options = cupsAddOption("outputfile", outputPath.toUtf8().constData(), num_options, &options);

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
    if (!cupsStartDocument(http, dest->name, job_id, inputFile.toUtf8().constData(), format, 1)) {
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
    if (!cupsFinishDocument(http, dest->name)) {
        qWarning() << "Failed to finish document:" << cupsLastErrorString();
        cupsFreeOptions(num_options, options);
        cupsFreeDests(1, dest);
        httpClose(http);
        return false;
    }

    // Clean up
    cupsFreeOptions(num_options, options);
    cupsFreeDests(1, dest);
    httpClose(http);

    return true;
}
