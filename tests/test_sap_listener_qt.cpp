// Qt signal-level test for SapListenerQt: verifies that a real SAP announce
// datagram emits deviceDiscovered and a SAP delete datagram (RFC 2974 "T"
// flag) emits deviceRemoved - then exits cleanly. Guards against regressions
// in the delete path (a previous harness segfaulted on delete due to a
// dangling socket reference; this version captures sockets via shared_ptr).
//
// Sends packets to 239.255.255.255:9875 (override with AES67_SAP_GROUP /
// AES67_SAP_PORT). Requires the Qt event loop; run via CTest.
#include "gui/SapListenerQt.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QProcessEnvironment>
#include <QDebug>

#include <memory>

namespace {

QByteArray makeSapPacket(bool isDelete, const QByteArray& sdp) {
    QByteArray pkt;
    // 0x20 = announce (V=1); 0x24 = announce + deletion flag (T=1, bit 2)
    pkt.append(static_cast<char>(isDelete ? 0x24 : 0x20));
    pkt.append("\x00\x00\x00\x7f\x00\x00\x01");
    pkt.append("application/sdp\x00");
    pkt.append(sdp);
    return pkt;
}

const QByteArray kSdp =
    "v=0\r\n"
    "o=- 1 1 IN IP4 127.0.0.1\r\n"
    "s=Unit Test Dev\r\n"
    "c=IN IP4 239.69.99.99/32\r\n"
    "t=0 0\r\n"
    "m=audio 4464 RTP/AVP 97\r\n";

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const auto env = QProcessEnvironment::systemEnvironment();
    const QString groupStr = env.value(QStringLiteral("AES67_SAP_GROUP"),
                                       QStringLiteral("239.255.255.255"));
    const quint16 port = static_cast<quint16>(
        env.value(QStringLiteral("AES67_SAP_PORT"), QStringLiteral("9875")).toUShort());

    sap::gui::SapListenerQt listener;
    const QString err = listener.start();
    if (!err.isEmpty()) {
        qWarning() << "listener start failed:" << err;
        return 2;
    }
    qInfo() << "listener started, active=" << listener.isActive();

    int discovered = 0;
    int removed = 0;
    QObject::connect(&listener, &sap::gui::SapListenerQt::deviceDiscovered,
                     [&](const QString& name, const QString& ip, const QString& stream,
                         const QDateTime&) {
        ++discovered;
        qInfo() << "DISCOVERED name=" << name << "ip=" << ip << "stream=" << stream;
    });
    QObject::connect(&listener, &sap::gui::SapListenerQt::deviceRemoved,
                     [&](const QString& ip) {
        ++removed;
        qInfo() << "REMOVED ip=" << ip;
    });
    QObject::connect(&listener, &sap::gui::SapListenerQt::errorOccurred,
                     [&](const QString& m) { qWarning() << "ERROR" << m; });

    const QHostAddress group(groupStr);

    // Each step owns its socket via shared_ptr captured by value - no dangling
    // references when the timer lambda fires after the outer scope returns.
    QTimer::singleShot(100, &app, [group, port] {
        auto sock = std::make_shared<QUdpSocket>();
        sock->writeDatagram(makeSapPacket(false, kSdp), group, port);
        qInfo() << "sent ANNOUNCE";
    });
    QTimer::singleShot(400, &app, [group, port] {
        auto sock = std::make_shared<QUdpSocket>();
        sock->writeDatagram(makeSapPacket(true, kSdp), group, port);
        qInfo() << "sent DELETE";
    });
    QTimer::singleShot(800, &app, [&] {
        qInfo() << "RESULT discovered=" << discovered << "removed=" << removed;
        const bool ok = (discovered == 1 && removed == 1);
        qInfo() << (ok ? "PASS" : "FAIL");
        app.exit(ok ? 0 : 1);
    });

    return app.exec();
}
