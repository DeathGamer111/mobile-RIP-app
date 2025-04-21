#include "PrintJobModel.h"
#include <QFile>
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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray jsonData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    QJsonArray array = doc.array();
    beginResetModel();
    m_jobs.clear();
    for (const QJsonValue &val : array) {
        QJsonObject obj = val.toObject();
        PrintJob job;
        job.id = obj["id"].toString();
        job.name = obj["name"].toString();
        job.imagePath = obj["imagePath"].toString();
        job.paperSize = QSize(obj["paperSizeWidth"].toInt(), obj["paperSizeHeight"].toInt());
        job.resolution = QSize(obj["resolutionWidth"].toInt(), obj["resolutionHeight"].toInt());
        job.offset.setX(obj["offsetX"].toInt());
        job.offset.setY(obj["offsetY"].toInt());
        job.whiteStrategy = obj["whiteStrategy"].toString();
        job.varnishType = obj["varnishType"].toString();
        job.colorProfile = obj["colorProfile"].toString();
        job.createdAt = QDateTime::fromString(obj["createdAt"].toString(), Qt::ISODate);
        m_jobs.append(job);
    }
    endResetModel();
}

void PrintJobModel::saveToJson(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonArray array;
    for (const PrintJob &job : m_jobs) {
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
        array.append(obj);
    }
    QJsonDocument doc(array);
    file.write(doc.toJson());
    file.close();
}

void PrintJobModel::exportJob(int index, const QString &filePath) {
    if (index < 0 || index >= m_jobs.size()) return;
    const PrintJob &job = m_jobs.at(index);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return;
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
    obj["colorProfile"] = job.colorProfile;
    obj["createdAt"] = job.createdAt.toString(Qt::ISODate);
    QJsonDocument doc(obj);
    file.write(doc.toJson());
    file.close();
}
