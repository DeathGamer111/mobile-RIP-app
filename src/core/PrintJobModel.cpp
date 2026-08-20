#include "PrintJobModel.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime> 
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>


/*********************************************************************************************
    PrintJobModel constructor, 
    QAbstractListModel storing print jobs; JSON import/export with optional embedded image data.
**********************************************************************************************/
PrintJobModel::PrintJobModel(QObject *parent) : QAbstractListModel(parent) {}


// Return number of jobs in the model
int PrintJobModel::rowCount(const QModelIndex &) const {
    return m_jobs.count();
}


// Return data for a given job and role
QVariant PrintJobModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) return QVariant();
    const PrintJob &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole: return job.id;
    case NameRole: return job.name;
    case ImagePathRole: return job.imagePath;
    case PaperSizeRole: return QVariant::fromValue(job.paperSize);
    case MediaHeightMmRole: return job.mediaHeightMm;
    case ResolutionRole: return QVariant::fromValue(job.resolution);
    case OffsetRole: return QVariant::fromValue(job.offset);
    case FeatheringRole: return job.feathering;
    case WhiteStrategyRole: return job.whiteStrategy;
    case VarnishTypeRole: return job.varnishType;
    case ColorProfileRole: return job.colorProfile;
    case CreatedAtRole: return job.createdAt;
    case WhitePlatePathRole: return job.whitePlatePath;
    case VarnishPlatePathRole: return job.varnishPlatePath;
    default: return QVariant();
    }
}


// Map internal role IDs to role names for QML
QHash<int, QByteArray> PrintJobModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {ImagePathRole, "imagePath"},
        {PaperSizeRole, "paperSize"},
        {MediaHeightMmRole, "mediaHeightMm"},
        {ResolutionRole, "resolution"},
        {OffsetRole, "offset"},
        {FeatheringRole, "feathering"},
        {WhiteStrategyRole, "whiteStrategy"},
        {VarnishTypeRole, "varnishType"},
        {ColorProfileRole, "colorProfile"},
        {CreatedAtRole, "createdAt"},
        {WhitePlatePathRole, "whitePlatePath"},
        {VarnishPlatePathRole, "varnishPlatePath"}
    };
}


PrintJob PrintJobModel::makeDefaultJob(const QString &name) const
{
    PrintJob job;
    job.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    job.name = name;
    job.createdAt = QDateTime::currentDateTime();
    job.paperSize = QSize(210, 297);    // Default A4 media size in millimeters.
    job.mediaHeightMm = -1.0;           // Preserve the printer's current height until enabled.
    job.resolution = QSize(720, 1440);	// RIP default DPI.
    job.offset = QPoint(0, 0);
    job.feathering = 2;                 // Medium SDK feathering.
    job.whiteStrategy = "None";
    job.varnishType = "None";
    job.colorProfile = "sRGB";
    job.whitePlatePath = "";
    job.varnishPlatePath = "";
    return job;
}

// Add a new print job with default values
void PrintJobModel::addJob(const QString &name) {
    beginInsertRows(QModelIndex(), m_jobs.size(), m_jobs.size());
    PrintJob job = makeDefaultJob(name);
    m_jobs.append(job);
    endInsertRows();
    emit countChanged();
}

int PrintJobModel::addJobFromImage(const QString &sourcePath, const QString &name)
{
    const QString importedPath = importImageToJobStorage(sourcePath);
    if (importedPath.isEmpty())
        return -1;

    const int row = m_jobs.size();
    const QUrl sourceUrl(sourcePath);
    QFileInfo info(sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : sourcePath);
    QString defaultName = info.completeBaseName().trimmed();
    defaultName.remove(QRegularExpression(QStringLiteral("^\\d{10,}_")));
    defaultName.remove(QRegularExpression(QStringLiteral("_\\d{10,}$")));
    defaultName.replace(QRegularExpression(QStringLiteral("[_\\s]+")), QStringLiteral(" "));
    if (defaultName.isEmpty()) {
        QFileInfo importedInfo(QUrl(importedPath).toLocalFile());
        defaultName = importedInfo.completeBaseName().trimmed();
    }
    if (defaultName.isEmpty())
        defaultName = QStringLiteral("Untitled Image");
    PrintJob job = makeDefaultJob(name.trimmed().isEmpty()
        ? defaultName
        : name.trimmed());
    job.imagePath = importedPath;

    beginInsertRows(QModelIndex(), row, row);
    m_jobs.append(job);
    endInsertRows();
    emit countChanged();
    return row;
}


// Remove a job by index position
void PrintJobModel::removeJob(int index) {
    if (index < 0 || index >= m_jobs.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    m_jobs.removeAt(index);
    endRemoveRows();
    emit countChanged();
}


// Return a print job as a QVariantMap
QVariantMap PrintJobModel::getJob(int index) const {
    QVariantMap map;
    if (index < 0 || index >= m_jobs.size()) return map;
    const PrintJob &job = m_jobs.at(index);
    map["id"] = job.id;
    map["name"] = job.name;
    map["imagePath"] = job.imagePath;
    map["paperSize"] = QVariant::fromValue(job.paperSize);
    map["mediaHeightMm"] = job.mediaHeightMm;
    map["resolution"] = QVariant::fromValue(job.resolution);
    map["offset"] = QVariant::fromValue(job.offset);
    map["feathering"] = job.feathering;
    map["whiteStrategy"] = job.whiteStrategy;
    map["varnishType"] = job.varnishType;
    map["colorProfile"] = job.colorProfile;
    map["createdAt"] = job.createdAt;
    map["whitePlatePath"] = job.whitePlatePath;
    map["varnishPlatePath"] = job.varnishPlatePath;
        
    return map;
}


// Update a print job from a QVariantMap
void PrintJobModel::updateJob(int index, const QVariantMap &jobData) {
    if (index < 0 || index >= m_jobs.size()) return;

    PrintJob &job = m_jobs[index];

    // Only update fields that are present, so partial updates don't wipe values.
    if (jobData.contains("name"))          job.name = jobData.value("name").toString();
    if (jobData.contains("imagePath"))     job.imagePath = jobData.value("imagePath").toString();

    if (jobData.contains("paperSize"))     job.paperSize = jobData.value("paperSize").toSize();
    if (jobData.contains("mediaHeightMm"))
        job.mediaHeightMm = std::clamp(jobData.value("mediaHeightMm").toDouble(), -1.0, 152.0);
    if (jobData.contains("resolution"))    job.resolution = jobData.value("resolution").toSize();
    if (jobData.contains("offset"))        job.offset = jobData.value("offset").toPoint();
    if (jobData.contains("feathering"))
        job.feathering = std::clamp(jobData.value("feathering").toInt(), 1, 3);

    if (jobData.contains("whiteStrategy")) job.whiteStrategy = jobData.value("whiteStrategy").toString();
    if (jobData.contains("varnishType"))   job.varnishType = jobData.value("varnishType").toString();
    if (jobData.contains("colorProfile"))  job.colorProfile = jobData.value("colorProfile").toString();
    
    if (jobData.contains("whitePlatePath"))    job.whitePlatePath = jobData.value("whitePlatePath").toString();
    if (jobData.contains("varnishPlatePath"))  job.varnishPlatePath = jobData.value("varnishPlatePath").toString();

    // Optional: allow createdAt updates if you ever want it from QML.
    if (jobData.contains("createdAt"))     job.createdAt = jobData.value("createdAt").toDateTime();

    emit dataChanged(this->index(index), this->index(index));
}

bool PrintJobModel::updateJobImage(int index, const QString &sourcePath)
{
    if (index < 0 || index >= m_jobs.size()) {
        m_lastError = QStringLiteral("No job is selected.");
        return false;
    }

    const QString importedPath = importImageToJobStorage(sourcePath);
    if (importedPath.isEmpty())
        return false;

    m_jobs[index].imagePath = importedPath;
    emit dataChanged(this->index(index), this->index(index), {ImagePathRole});
    return true;
}

QString PrintJobModel::lastError() const
{
    return m_lastError;
}

QString PrintJobModel::importImageToJobStorage(const QString &sourcePath)
{
    m_lastError.clear();

    const QUrl url(sourcePath);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : sourcePath;
    QFileInfo sourceInfo(localPath);
    const QString extension = sourceInfo.suffix().toLower();

    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        m_lastError = QStringLiteral("The selected image could not be opened.");
        return {};
    }

    if (!isSupportedImportExtension(extension)) {
        m_lastError = QStringLiteral("Unsupported image type: .%1").arg(extension);
        return {};
    }

    if (!validateImportedSource(localPath, extension)) {
        m_lastError = QStringLiteral("The selected file is not a readable image.");
        return {};
    }

    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        m_lastError = QStringLiteral("App storage is not available.");
        return {};
    }

    QDir dir(root);
    if (!dir.mkpath(QStringLiteral("job_images"))) {
        m_lastError = QStringLiteral("Could not create job image storage.");
        return {};
    }

    QString base = sourceInfo.completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("image");

    const QString fileName = QStringLiteral("%1_%2.%3")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(base)
        .arg(extension);
    const QString destination = dir.filePath(QStringLiteral("job_images/") + fileName);

    if (!QFile::copy(localPath, destination)) {
        m_lastError = QStringLiteral("Could not copy image into app storage.");
        return {};
    }

    return QUrl::fromLocalFile(destination).toString();
}

bool PrintJobModel::isSupportedImportExtension(const QString &extension) const
{
    static const QStringList supported = {
        QStringLiteral("jpeg"), QStringLiteral("jpg"), QStringLiteral("png"),
        QStringLiteral("bmp"), QStringLiteral("tiff"), QStringLiteral("tif"),
        QStringLiteral("svg"), QStringLiteral("pdf")
    };
    return supported.contains(extension);
}

bool PrintJobModel::validateImportedSource(const QString &localPath, const QString &extension) const
{
    if (extension == QStringLiteral("pdf") || extension == QStringLiteral("svg"))
        return QFileInfo::exists(localPath);

    QImageReader reader(localPath);
    if (reader.canRead())
        return true;

    // Qt's Android image plugins do not include a TIFF decoder. The native
    // RIP validates and decodes TIFF through ImageMagick, so accept a TIFF
    // here only when its byte-order/version signature is well formed.
    if (extension == QStringLiteral("tif") || extension == QStringLiteral("tiff")) {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray signature = file.read(4);
        return signature == QByteArray("II\x2a\0", 4)
            || signature == QByteArray("MM\0\x2a", 4)
            || signature == QByteArray("II\x2b\0", 4)
            || signature == QByteArray("MM\0\x2b", 4);
    }

    return false;
}


// Load jobs from a JSON file (with optional embedded images)
void PrintJobModel::loadFromJson(const QString &filePath) {
    const QString localPath = QUrl(filePath).toLocalFile();
    qDebug() << "[LOAD JSON]" << localPath;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading:" << localPath;
        return;
    }

    const QByteArray jsonData = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QList<PrintJob> newJobs;

    auto parseObject = [&](const QJsonObject &obj) {
        PrintJob job;
        job.id = obj["id"].toString();
        job.name = obj["name"].toString();
        job.imagePath = obj["imagePath"].toString();  // May be overwritten below

        job.paperSize = QSize(obj["paperSizeWidth"].toInt(), obj["paperSizeHeight"].toInt());
        job.mediaHeightMm = obj.contains("mediaHeightMm")
            ? std::clamp(obj["mediaHeightMm"].toDouble(-1.0), -1.0, 152.0)
            : -1.0;
        job.resolution = QSize(obj["resolutionWidth"].toInt(), obj["resolutionHeight"].toInt());
        job.offset.setX(obj["offsetX"].toInt());
        job.offset.setY(obj["offsetY"].toInt());
        job.feathering = obj.contains("feathering")
            ? std::clamp(obj["feathering"].toInt(2), 1, 3)
            : 2;

        job.whiteStrategy = obj["whiteStrategy"].toString();
        job.varnishType = obj["varnishType"].toString();
        job.colorProfile = obj["colorProfile"].toString();
        
		job.whitePlatePath = obj["whitePlatePath"].toString();
		job.varnishPlatePath = obj["varnishPlatePath"].toString();

        job.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        if (!job.createdAt.isValid()) {
            // Backward compatibility / missing field
            job.createdAt = QDateTime::currentDateTime();
        }

        // If image data is embedded, reconstruct the image file
        if (obj.contains("imageData")) {
            const QByteArray imageData = QByteArray::fromBase64(obj["imageData"].toString().toUtf8());

            QString originalExt = QFileInfo(job.imagePath).suffix();
            if (originalExt.isEmpty())
                originalExt = "png";

            const QString newPath = QFileInfo(localPath).absoluteDir().filePath(job.id + "." + originalExt);

            QFile outImage(newPath);
            if (outImage.open(QIODevice::WriteOnly)) {
                outImage.write(imageData);
                outImage.close();
                job.imagePath = QUrl::fromLocalFile(newPath).toString();
            }
        }

        // If ID is missing, generate one (defensive)
        if (job.id.isEmpty())
            job.id = QString::number(QDateTime::currentMSecsSinceEpoch());

        newJobs.append(job);
    };

    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &val : arr) {
            if (val.isObject())
                parseObject(val.toObject());
        }
    }
    else if (doc.isObject()) {
        parseObject(doc.object());
    }
    else {
        qWarning() << "Invalid JSON structure in" << filePath;
        return;
    }

    if (newJobs.isEmpty()) {
        qDebug() << "[LOAD JSON] No jobs found in" << localPath;
        return;
    }

    const int start = m_jobs.size();
    const int end = start + newJobs.size() - 1;

    beginInsertRows(QModelIndex(), start, end);
    m_jobs.append(newJobs);
    endInsertRows();
    emit countChanged();
}


// Save selected jobs to JSON file, embedding image data as base64
void PrintJobModel::saveToJson(const QString &filePath, const QList<int> &selectedIndexes) {
    const QString localPath = QUrl(filePath).toLocalFile();

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << localPath;
        return;
    }

    QJsonArray array;
    for (int index : selectedIndexes) {
        if (index < 0 || index >= m_jobs.size()) continue;
        const PrintJob &job = m_jobs[index];

        QJsonObject obj;
        obj["id"] = job.id;
        obj["name"] = job.name;
        obj["imagePath"] = job.imagePath;
        obj["paperSizeWidth"] = job.paperSize.width();
        obj["paperSizeHeight"] = job.paperSize.height();
        obj["mediaHeightMm"] = job.mediaHeightMm;
        obj["resolutionWidth"] = job.resolution.width();
        obj["resolutionHeight"] = job.resolution.height();
        obj["offsetX"] = job.offset.x();
        obj["offsetY"] = job.offset.y();
        obj["feathering"] = job.feathering;
        obj["whiteStrategy"] = job.whiteStrategy;
        obj["varnishType"] = job.varnishType;
        obj["colorProfile"] = job.colorProfile;
        obj["createdAt"] = job.createdAt.toString(Qt::ISODate);
        obj["whitePlatePath"] = job.whitePlatePath;
        obj["varnishPlatePath"] = job.varnishPlatePath;
                

        // Embed image base64 if available
        QFile imageFile(QUrl(job.imagePath).toLocalFile());
        if (imageFile.open(QIODevice::ReadOnly)) {
            QByteArray imageData = imageFile.readAll();
            obj["imageData"] = QString::fromUtf8(imageData.toBase64());
            imageFile.close();
        }

        array.append(obj);
    }

    QJsonDocument doc(array);
    file.write(doc.toJson());
    file.close();
}
