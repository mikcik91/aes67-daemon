#include "MainWindow.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

namespace sap::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("AES67 / Dante Discovery"));
    resize(760, 420);

    model_ = new DeviceTableModel(this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy_->setFilterKeyColumn(-1); // search across all columns

    // Hardcoded demo rows so the window is not empty before any SAP packet arrives.
    const auto now = QDateTime::currentDateTime();
    model_->upsertDevice("Virtual Q-SYS Core", "192.168.1.100", "239.69.10.20:5004", now);
    model_->upsertDevice("Dante Via 01", "192.168.1.101", "239.69.11.21:5004", now.addSecs(-5));
    model_->upsertDevice("Stagebox SB-16", "192.168.1.102", "239.69.12.22:5004", now.addSecs(-12));

    countLabel_ = new QLabel(this);
    filter_     = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter by name or IP..."));
    filter_->setClearButtonEnabled(true);

    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter:"), this));
    filterRow->addWidget(filter_, 1);
    filterRow->addWidget(countLabel_);

    table_ = new QTableView(this);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);
    layout->addLayout(filterRow);
    layout->addWidget(table_, 1);
    setCentralWidget(central);

    // Live filter: proxy model does the actual matching (no hand-rolled filtering).
    connect(filter_, &QLineEdit::textChanged, proxy_,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(proxy_, &QSortFilterProxyModel::rowsInserted, this, [this] {
        countLabel_->setText(tr("Devices: %1").arg(proxy_->rowCount()));
    });
    connect(proxy_, &QSortFilterProxyModel::rowsRemoved, this, [this] {
        countLabel_->setText(tr("Devices: %1").arg(proxy_->rowCount()));
    });
    countLabel_->setText(tr("Devices: %1").arg(proxy_->rowCount()));

    statusBar()->showMessage(tr("Listening on 239.255.255.255:9875 (SAP) — demo rows only"));
}

} // namespace sap::gui
