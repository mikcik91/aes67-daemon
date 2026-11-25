#include "MainWindow.hpp"

#include "SapListenerQt.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProcessEnvironment>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
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

    // Live SAP listener: replaces the old hardcoded demo rows. Group/port come
    // from AES67_SAP_GROUP / AES67_SAP_PORT env vars (same as the daemon).
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString group = env.value(QStringLiteral("AES67_SAP_GROUP"), QStringLiteral("239.255.255.255"));
    const QString port  = env.value(QStringLiteral("AES67_SAP_PORT"), QStringLiteral("9875"));

    listener_ = new SapListenerQt(this);
    connect(listener_, &SapListenerQt::deviceDiscovered, model_,
            [this](const QString& name, const QString& ip, const QString& stream,
                   const QDateTime& when) {
                model_->upsertDevice(name, ip, stream, when);
            });
    connect(listener_, &SapListenerQt::deviceRemoved, model_,
            [this](const QString& ip) { model_->removeDevice(ip); });
    connect(listener_, &SapListenerQt::errorOccurred, this, [this](const QString& msg) {
        statusBar()->showMessage(tr("SAP socket error: %1").arg(msg), 10000);
    });

    const QString listenErr = listener_->start();
    QString statusMsg = tr("Listening on %1:%2 (SAP)").arg(group, port);
    if (!listenErr.isEmpty())
        statusMsg = tr("Not listening — %1").arg(listenErr);

    // Aging sweep every second: stale after 30 s (red row), removed after 60 s.
    // Same defaults as the daemon's AES67_DEVICE_TIMEOUT=60.
    auto* agingTimer = new QTimer(this);
    connect(agingTimer, &QTimer::timeout, this, [this] {
        model_->refreshAging(QDateTime::currentDateTime(), /*staleSec=*/30, /*removeSec=*/60);
    });
    agingTimer->start(1000);

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

    statusBar()->showMessage(statusMsg);
}

} // namespace sap::gui
