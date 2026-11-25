#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class QUdpSocket;

namespace sap::gui {

// Receives SAP (Session Announcement Protocol) multicast packets on
// 239.255.255.255:9875 via QUdpSocket and emits Qt signals, so the GUI
// thread can update the model safely. Mirrors the daemon's SapListener
// (boost::asio) but runs on the Qt event loop. SDP parsing reuses the
// daemon's pure parser (src/SdpParser.hpp).
//
// Group/port can be overridden via AES67_SAP_GROUP / AES67_SAP_PORT env
// vars (same names as the daemon).
class SapListenerQt : public QObject {
    Q_OBJECT

public:
    explicit SapListenerQt(QObject* parent = nullptr);
    ~SapListenerQt() override;

    // Opens the UDP socket and joins the multicast group.
    // Returns an error string on failure, empty string on success.
    QString start();

    void stop();

    [[nodiscard]] bool isActive() const;

signals:
    // A SAP announce arrived and parsed successfully.
    void deviceDiscovered(const QString& name, const QString& ip,
                          const QString& stream, const QDateTime& when);
    // A SAP delete packet (RFC 2974 "T" flag) arrived for this source IP.
    void deviceRemoved(const QString& ip);
    // Socket-level error (bind failure, receive error, ...).
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();

private:
    QUdpSocket* socket_{};
    QString     group_{QStringLiteral("239.255.255.255")};
    quint16     port_{9875};
};

} // namespace sap::gui
