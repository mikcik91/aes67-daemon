#pragma once

#include "DeviceTableModel.hpp"
#include <QMainWindow>

class QLineEdit;
class QSortFilterProxyModel;
class QTableView;
class QLabel;

namespace sap::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    DeviceTableModel*       model_{};
    QSortFilterProxyModel*  proxy_{};
    QTableView*             table_{};
    QLineEdit*              filter_{};
    QLabel*                 countLabel_{};
};

} // namespace sap::gui
