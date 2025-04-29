// PrintJobModel.h
#include <QAbstractListModel>
#include "PrintJob.h"

class PrintJobModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum PrintJobRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ImagePathRole,
        PaperSizeRole,
        ResolutionRole,
        OffsetRole,
        WhiteStrategyRole,
        VarnishTypeRole,
        ColorProfileRole,
        CreatedAtRole
    };

    PrintJobModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addJob(const QString &name);
    Q_INVOKABLE void removeJob(int index);
    Q_INVOKABLE QVariantMap getJob(int index) const;
    Q_INVOKABLE void updateJob(int index, const QVariantMap &jobData);
    Q_INVOKABLE void loadFromJson(const QString &filePath);
    Q_INVOKABLE void saveToJson(const QString &filePath, const QList<int> &selectedIndexes);

private:
    QList<PrintJob> m_jobs;
};
