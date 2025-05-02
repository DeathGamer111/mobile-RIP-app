// PrintJobModel.h
#include <QAbstractListModel>
#include "PrintJob.h"

/**************************************************************************************************
    PrintJobModel provides a QAbstractListModel-backed interface for managing a list of print jobs.
    Exposes QML-accessible methods for job CRUD operations and JSON serialization.
***************************************************************************************************/

class PrintJobModel : public QAbstractListModel {
    Q_OBJECT

public:

    // Custom roles for QML data binding
    enum PrintJobRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ImagePathRole,
        ImagePositionRole,
        PaperSizeRole,
        ResolutionRole,
        OffsetRole,
        WhiteStrategyRole,
        VarnishTypeRole,
        ColorProfileRole,
        CreatedAtRole
    };

    PrintJobModel(QObject *parent = nullptr);

    // Property exposed to QML
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // QML-callable methods
    Q_INVOKABLE void addJob(const QString &name);                                               // Create new print job
    Q_INVOKABLE void removeJob(int index);                                                      // Remove job at index
    Q_INVOKABLE QVariantMap getJob(int index) const;                                            // Get job as map for QML
    Q_INVOKABLE void updateJob(int index, const QVariantMap &jobData);                          // Update job using map
    Q_INVOKABLE void loadFromJson(const QString &filePath);                                     // Load jobs from file
    Q_INVOKABLE void saveToJson(const QString &filePath, const QList<int> &selectedIndexes);    // Save jobs to file

private:
    QList<PrintJob> m_jobs;

signals:
    void countChanged(); // Emitted when job list changes
};
