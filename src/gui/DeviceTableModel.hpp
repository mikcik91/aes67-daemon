#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

namespace sap::gui {

struct DeviceRow {
    QString   name;
    QString   ip;
    QString   stream; // "239.69.10.20:5004"
    QDateTime lastSeen;
    bool      stale{false};
};

// Table model for discovered AES67/Dante devices. Keyed by source IP:
// upsertDevice() refreshes an existing row or inserts a new one.
class DeviceTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColName = 0, ColIp, ColStream, ColLastSeen, ColCount };

    explicit DeviceTableModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Refresh an existing device or insert a new one (keyed by ip).
    // Returns true when a new row was inserted.
    bool upsertDevice(const QString& name, const QString& ip, const QString& stream,
                      const QDateTime& when);

    // Marks rows whose lastSeen is older than staleSec as stale (red background)
    // and removes rows older than removeSec. Returns the number of removed rows.
    int refreshAging(const QDateTime& now, int staleSec, int removeSec);

private:
    void rebuildIndex();

    QVector<DeviceRow>   rows_;
    QHash<QString, int>  indexByIp_; // ip -> row index
};

} // namespace sap::gui
