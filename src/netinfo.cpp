// SPDX-License-Identifier: GPL-2.0-or-later

#include "netinfo.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QStringList>
#include <QVariant>

namespace
{
const QLatin1String NM_SERVICE{"org.freedesktop.NetworkManager"};
const QLatin1String NM_PATH{"/org/freedesktop/NetworkManager"};
const QLatin1String NM_IFACE{"org.freedesktop.NetworkManager"};
const QLatin1String NM_DEVICE{"org.freedesktop.NetworkManager.Device"};
const QLatin1String NM_WIRELESS{"org.freedesktop.NetworkManager.Device.Wireless"};
const QLatin1String NM_AP{"org.freedesktop.NetworkManager.AccessPoint"};
const QLatin1String NM_IP4{"org.freedesktop.NetworkManager.IP4Config"};
const QLatin1String PROPS{"org.freedesktop.DBus.Properties"};

// Same reasoning as in the daemon: D-Bus waits 25 seconds by default, and this
// runs on a timer. A NetworkManager that does not answer promptly must not turn
// into a daemon that is wedged for half a minute at a time -- "no Wi-Fi info"
// is a perfectly good answer, and it means "not trusted", which is the safe way
// to be wrong.
constexpr int NM_TIMEOUT_MS = 3'000;

// NM's device states and types are plain integers on the bus; naming the two we
// branch on keeps the reads below readable.
constexpr uint NM_DEVICE_TYPE_WIFI = 2;
constexpr uint NM_DEVICE_STATE_ACTIVATED = 100;

// One property read. Everything NM exposes goes through the standard Properties
// interface, so a single helper covers object paths, strings, uints and byte
// arrays -- the caller unwraps the QVariant it expects.
QVariant prop(const QString &path, const QString &iface, const QString &name)
{
    QDBusInterface props(NM_SERVICE, path, PROPS, QDBusConnection::systemBus());
    if (!props.isValid()) {
        return {};
    }
    props.setTimeout(NM_TIMEOUT_MS);
    const QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), iface, name);
    if (!reply.isValid()) {
        return {};
    }
    return reply.value().variant();
}

QString pathOf(const QVariant &v)
{
    // Object-path properties come back wrapped; qdbus_cast turns the inner
    // QDBusVariant into the QDBusObjectPath without us hand-rolling the demarshal.
    return qdbus_cast<QDBusObjectPath>(v).path();
}
}

QString NetInfo::normalizeMac(const QString &mac)
{
    return mac.trimmed().toUpper();
}

QString NetInfo::arpLookup(const QString &ipv4, const QString &arpPath)
{
    if (ipv4.isEmpty()) {
        return {};
    }

    // /proc/net/arp over `ip neigh`: it is always present, needs no fork, and
    // its columns are fixed. Format, after the header line:
    //   IP address   HW type   Flags   HW address   Mask   Device
    QFile arp(arpPath);
    if (!arp.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    // readAll() and split, NOT the readLine()/atEnd() loop this used to be.
    // Files under /proc report a size of zero, and QFile::atEnd() answers from
    // the size: it says "already at the end" before a single byte is read, so
    // that loop ran zero times and this function always returned an empty MAC.
    // Silently, and with the only visible symptom being a trusted network that
    // never matched, because a captured gateway that never resolves can never
    // equal the one on file.
    const QList<QByteArray> lines = arp.readAll().split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw);
        const QStringList cols = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (cols.size() < 4) {
            continue;
        }
        // The header line ("IP address  HW type  Flags  HW address ...") splits
        // into perfectly good-looking columns, so it has to be ruled out on
        // content: a real entry starts with a dotted address. Without this,
        // asking for "IP" answers "TYPE".
        if (!cols.at(0).contains(QLatin1Char('.'))) {
            continue;
        }
        if (cols.at(0) != ipv4) {
            continue;
        }
        const QString mac = normalizeMac(cols.at(3));
        // An incomplete entry is 00:00:00:00:00:00 -- the gateway is known by
        // IP but has not answered ARP yet. Treat that as "unknown", not as a
        // real MAC that would then never match anything.
        if (mac == QLatin1String("00:00:00:00:00:00")) {
            return {};
        }
        return mac;
    }
    return {};
}

CurrentWifi NetInfo::currentWifi()
{
    CurrentWifi wifi;

    QDBusInterface manager(NM_SERVICE, NM_PATH, NM_IFACE, QDBusConnection::systemBus());
    if (!manager.isValid()) {
        return wifi;
    }
    manager.setTimeout(NM_TIMEOUT_MS);

    const QDBusReply<QList<QDBusObjectPath>> devices = manager.call(QStringLiteral("GetDevices"));
    if (!devices.isValid()) {
        return wifi;
    }

    for (const QDBusObjectPath &dev : devices.value()) {
        const QString devPath = dev.path();

        if (prop(devPath, NM_DEVICE, QStringLiteral("DeviceType")).toUInt() != NM_DEVICE_TYPE_WIFI) {
            continue;
        }
        if (prop(devPath, NM_DEVICE, QStringLiteral("State")).toUInt() != NM_DEVICE_STATE_ACTIVATED) {
            continue;
        }

        const QString apPath = pathOf(prop(devPath, NM_WIRELESS, QStringLiteral("ActiveAccessPoint")));
        // "/" is NM's null object path: associated but no current AP, so there
        // is nothing to trust.
        if (apPath.isEmpty() || apPath == QLatin1String("/")) {
            continue;
        }

        wifi.connected = true;
        wifi.bssid = normalizeMac(prop(apPath, NM_AP, QStringLiteral("HwAddress")).toString());

        // Ssid is a byte array on the bus, not a string: it can hold bytes that
        // are not valid UTF-8. It is only a label here, so a best-effort decode
        // is fine -- it is never what a trust decision turns on.
        const QByteArray ssidBytes = prop(apPath, NM_AP, QStringLiteral("Ssid")).toByteArray();
        wifi.ssid = QString::fromUtf8(ssidBytes);

        const QString ip4Path = pathOf(prop(devPath, NM_DEVICE, QStringLiteral("Ip4Config")));
        if (!ip4Path.isEmpty() && ip4Path != QLatin1String("/")) {
            wifi.gatewayIp = prop(ip4Path, NM_IP4, QStringLiteral("Gateway")).toString();
            wifi.gatewayMac = arpLookup(wifi.gatewayIp);
        }

        // First activated Wi-Fi wins; a phone has one.
        break;
    }

    return wifi;
}
