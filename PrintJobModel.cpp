#include "PrintJobModel.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

PrintJobModel::PrintJobModel(QObject *parent) : QAbstractListModel(parent) {}

int PrintJobModel::rowCount(const QModelIndex &) const {
    return m_jobs.count();
}

QVariant PrintJobModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) return QVariant();
    const PrintJob &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole: return job.id;
    case NameRole: return job.name;
    case ImagePathRole: return job.imagePath;
    case PaperSizeRole: return QVariant::fromValue(job.paperSize);
    case ResolutionRole: return QVariant::fromValue(job.resolution);
    case OffsetRole: return QVariant::fromValue(job.offset);
    case WhiteStrategyRole: return job.whiteStrategy;
    case VarnishTypeRole: return job.varnishType;
    case ColorProfileRole: return job.colorProfile;
    case CreatedAtRole: return job.createdAt;
    default: return QVariant();
    }
}

QHash<int, QByteArray> PrintJobModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {ImagePathRole, "imagePath"},
        {PaperSizeRole, "paperSize"},
        {ResolutionRole, "resolution"},
        {OffsetRole, "offset"},
        {WhiteStrategyRole, "whiteStrategy"},
        {VarnishTypeRole, "varnishType"},
        {ColorProfileRole, "colorProfile"},
        {CreatedAtRole, "createdAt"}
    };
}

void PrintJobModel::addJob(const QString &name) {
    beginInsertRows(QModelIndex(), m_jobs.size(), m_jobs.size());
    PrintJob job;
    job.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    job.name = name;
    job.createdAt = QDateTime::currentDateTime();
    job.paperSize = QSize(210, 297);    // Dfault to A4 Paper Size
    job.resolution = QSize(300, 300);   // optional default
    job.offset = QPoint(0, 0);          // optional default
    job.whiteStrategy = "None";         // optional default
    job.varnishType = "None";           // optional default
    job.colorProfile = "sRGB";          // optional default
    m_jobs.append(job);
    endInsertRows();
}

void PrintJobModel::removeJob(int index) {
    if (index < 0 || index >= m_jobs.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    m_jobs.removeAt(index);
    endRemoveRows();
}

QVariantMap PrintJobModel::getJob(int index) const {
    QVariantMap map;
    if (index < 0 || index >= m_jobs.size()) return map;
    const PrintJob &job = m_jobs.at(index);
    map["id"] = job.id;
    map["name"] = job.name;
    map["imagePath"] = job.imagePath;
    map["paperSize"] = QVariant::fromValue(job.paperSize);
    map["resolution"] = QVariant::fromValue(job.resolution);
    map["offset"] = QVariant::fromValue(job.offset);
    map["whiteStrategy"] = job.whiteStrategy;
    map["varnishType"] = job.varnishType;
    map["colorProfile"] = job.colorProfile;
    map["createdAt"] = job.createdAt;
    return map;
}

void PrintJobModel::updateJob(int index, const QVariantMap &jobData) {
    if (index < 0 || index >= m_jobs.size()) return;
    PrintJob &job = m_jobs[index];
    job.name = jobData["name"].toString();
    job.imagePath = jobData["imagePath"].toString();
    job.paperSize = jobData["paperSize"].toSize();
    job.resolution = jobData["resolution"].toSize();
    job.offset = jobData["offset"].toPoint();
    job.whiteStrategy = jobData["whiteStrategy"].toString();
    job.varnishType = jobData["varnishType"].toString();
    job.colorProfile = jobData["colorProfile"].toString();
    emit dataChanged(this->index(index), this->index(index));
}

void PrintJobModel::loadFromJson(const QString &filePath) {
    const QString localPath = QUrl(filePath).toLocalFile();
    qDebug() << "[LOAD JSON]" << localPath;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading:" << localPath;
        return;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QList<PrintJob> newJobs;

    auto parseObject = [&](const QJsonObject &obj) {
        PrintJob job;
        job.id = obj["id"].toString();
        job.name = obj["name"].toString();
        job.imagePath = obj["imagePath"].toString();  // May be overwritten below
        job.paperSize = QSize(obj["paperSizeWidth"].toInt(), obj["paperSizeHeight"].toInt());
        job.resolution = QSize(obj["resolutionWidth"].toInt(), obj["resolutionHeight"].toInt());
        job.offset.setX(obj["offsetX"].toInt());
        job.offset.setY(obj["offsetY"].toInt());
        job.whiteStrategy = obj["whiteStrategy"].toString();
        job.varnishType = obj["varnishType"].toString();
        job.colorProfile = obj["colorProfile"].toString();
        job.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);

        // If embedded image is present
        if (obj.contains("imageData")) {
            QByteArray imageData = QByteArray::fromBase64(obj["imageData"].toString().toUtf8());

            QString originalExt = QFileInfo(job.imagePath).suffix();
            if (originalExt.isEmpty())
                originalExt = "png";

            QString newPath = QFileInfo(localPath).absoluteDir().filePath(job.id + "." + originalExt);

            QFile outImage(newPath);
            if (outImage.open(QIODevice::WriteOnly)) {
                outImage.write(imageData);
                outImage.close();
                job.imagePath = QUrl::fromLocalFile(newPath).toString();
            }
        }

        newJobs.append(job);
    };

    if (doc.isArray()) {
        for (const QJsonValue &val : doc.array()) {
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

    beginInsertRows(QModelIndex(), m_jobs.size(), m_jobs.size() + newJobs.size() - 1);
    m_jobs.append(newJobs);
    endInsertRows();
}


void PrintJobModel::saveToJson(const QString &filePath, const QList<int> &selectedIndexes) {
    const QString localPath = QUrl(filePath).toLocalFile();
    qDebug() << "[SAVE JSON]" << localPath;

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
        obj["resolutionWidth"] = job.resolution.width();
        obj["resolutionHeight"] = job.resolution.height();
        obj["offsetX"] = job.offset.x();
        obj["offsetY"] = job.offset.y();
        obj["whiteStrategy"] = job.whiteStrategy;
        obj["varnishType"] = job.varnishType;
        obj["colorProfile"] = job.colorProfile;
        obj["createdAt"] = job.createdAt.toString(Qt::ISODate);

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
