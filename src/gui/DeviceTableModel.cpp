#include "DeviceTableModel.hpp"

#include <QBrush>
#include <QColor>

namespace sap::gui {

DeviceTableModel::DeviceTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int DeviceTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int DeviceTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant DeviceTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const auto& row = rows_[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:     return row.name;
        case ColIp:       return row.ip;
        case ColStream:   return row.stream;
        case ColLastSeen: return row.lastSeen.toString("HH:mm:ss");
        default:          return {};
        }
    }
    if (role == Qt::ToolTipRole)
        return QStringLiteral("%1\n%2\nStream: %3\nLast seen: %4")
            .arg(row.name, row.ip, row.stream,
                 row.lastSeen.toString("yyyy-MM-dd HH:mm:ss"));
    if (role == Qt::BackgroundRole && row.stale)
        return QBrush{QColor{255, 190, 190}}; // light red — device went silent
    return {};
}

QVariant DeviceTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColName:     return tr("Device Name");
    case ColIp:       return tr("IP Address");
    case ColStream:   return tr("Stream");
    case ColLastSeen: return tr("Last Seen");
    default:          return {};
    }
}

bool DeviceTableModel::upsertDevice(const QString& name, const QString& ip,
                                    const QString& stream, const QDateTime& when) {
    const auto it = indexByIp_.constFind(ip);
    if (it != indexByIp_.constEnd()) {
        const int row = it.value();
        auto&     r   = rows_[row];
        r.name      = name;
        r.stream    = stream;
        r.lastSeen  = when;
        r.stale     = false; // fresh packet clears the stale flag
        emit dataChanged(index(row, 0), index(row, ColCount - 1));
        return false;
    }

    const int row = static_cast<int>(rows_.size());
    beginInsertRows({}, row, row);
    rows_.append(DeviceRow{name, ip, stream, when, false});
    endInsertRows();
    indexByIp_.insert(ip, row);
    return true;
}

int DeviceTableModel::refreshAging(const QDateTime& now, int staleSec, int removeSec) {
    int removed = 0;

    // 1) flip stale flags where needed
    for (int row = 0; row < rows_.size(); ++row) {
        const auto ageSec = rows_[row].lastSeen.secsTo(now);
        const bool shouldBeStale = ageSec >= staleSec && !rows_[row].stale;
        const bool shouldBeFresh = ageSec < staleSec && rows_[row].stale;
        if (shouldBeStale || shouldBeFresh) {
            rows_[row].stale = shouldBeStale;
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
        }
    }

    // 2) remove rows older than removeSec (iterate backwards so indexes stay valid)
    for (int row = static_cast<int>(rows_.size()) - 1; row >= 0; --row) {
        if (rows_[row].lastSeen.secsTo(now) >= removeSec) {
            beginRemoveRows({}, row, row);
            rows_.removeAt(row);
            endRemoveRows();
            ++removed;
        }
    }

    if (removed > 0)
        rebuildIndex();
    return removed;
}

void DeviceTableModel::rebuildIndex() {
    indexByIp_.clear();
    for (int i = 0; i < rows_.size(); ++i)
        indexByIp_.insert(rows_[i].ip, i);
}

} // namespace sap::gui
