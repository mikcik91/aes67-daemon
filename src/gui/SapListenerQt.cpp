#include "SapListenerQt.hpp"

#include "SdpParser.hpp" // daemon's pure SAP/SDP parser (shared with aes67_daemon)

#include <QHostAddress>
#include <QUdpSocket>
#include <QProcessEnvironment>

namespace sap::gui {

SapListenerQt::SapListenerQt(QObject* parent) : QObject(parent), socket_(new QUdpSocket(this)) {
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("AES67_SAP_GROUP")))
        group_ = env.value(QStringLiteral("AES67_SAP_GROUP"));
    if (env.contains(QStringLiteral("AES67_SAP_PORT")))
        port_ = static_cast<quint16>(env.value(QStringLiteral("AES67_SAP_PORT")).toUShort());
}

SapListenerQt::~SapListenerQt() {
    stop();
}

QString SapListenerQt::start() {
    if (socket_->state() == QAbstractSocket::BoundState)
        return {};

    // Share the port with other SAP listeners (e.g. aes67_daemon running
    // alongside) and bind to all interfaces.
    if (!socket_->bind(QHostAddress::AnyIPv4, port_, QUdpSocket::ShareAddress |
                                                        QUdpSocket::ReuseAddressHint)) {
        return socket_->errorString();
    }
    if (!socket_->joinMulticastGroup(QHostAddress(group_))) {
        const QString err = socket_->errorString();
        socket_->close();
        return tr("joinMulticastGroup(%1) failed: %2").arg(group_, err);
    }

    connect(socket_, &QUdpSocket::readyRead, this, &SapListenerQt::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { emit errorOccurred(socket_->errorString()); });
    return {};
}

void SapListenerQt::stop() {
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->leaveMulticastGroup(QHostAddress(group_));
        socket_->close();
    }
}

bool SapListenerQt::isActive() const {
    return socket_->state() == QAbstractSocket::BoundState;
}

void SapListenerQt::onReadyRead() {
    while (socket_->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(socket_->pendingDatagramSize()));
        QHostAddress sender;
        quint16      senderPort = 0;
        socket_->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        const std::string_view payload{datagram.constData(),
                                        static_cast<std::size_t>(datagram.size())};
        const QString sourceIp = sender.toString();

        if (sap::is_sap_delete(payload)) {
            emit deviceRemoved(sourceIp);
            continue;
        }
        if (const auto info = sap::parse_sdp(payload, sourceIp.toStdString())) {
            const QString stream = info->stream_address.empty()
                ? QStringLiteral("-")
                : QStringLiteral("%1:%2")
                      .arg(QString::fromStdString(info->stream_address))
                      .arg(info->stream_port);
            emit deviceDiscovered(QString::fromStdString(info->name), sourceIp, stream,
                                  QDateTime::currentDateTime());
        }
    }
}

} // namespace sap::gui
