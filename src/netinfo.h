// SPDX-License-Identifier: GPL-2.0-or-later
//
// The only two facts the trust check needs from the network, read straight from
// NetworkManager over the system bus: which access point we are on (its BSSID)
// and the MAC of the gateway behind it. Shared by the settings UI -- which
// captures them when you tap "trust this network" -- and by the daemon, which
// re-reads them on every NetworkManager change to decide whether we are home.
//
// Kept deliberately synchronous and cheap: it is a handful of D-Bus property
// reads plus one scan of /proc/net/arp, called on events and on a slow tick,
// never in a hot path.

#pragma once

#include <QString>

struct CurrentWifi {
    bool connected = false; // an activated Wi-Fi device with an access point
    QString bssid;          // upper-case "AA:BB:CC:DD:EE:FF"
    QString ssid;           // decoded from NM's raw bytes, best-effort UTF-8
    QString gatewayIp;      // IPv4 of the default gateway, "" if none
    QString gatewayMac;     // gateway MAC from ARP, "" if not yet resolved
};

namespace NetInfo
{
CurrentWifi currentWifi();

// MAC for an IPv4 address from the kernel's ARP table, upper-cased, or "" if it
// is not there or is the all-zero placeholder. Exposed for the daemon, which
// re-checks the gateway MAC without re-walking all of NetworkManager.
// The ARP table path is a parameter only so the parsing can be tested against
// sample tables; everything in the daemon uses the default.
QString arpLookup(const QString &ipv4, const QString &arpPath = QStringLiteral("/proc/net/arp"));

// Normalise a MAC to the one spelling everything else compares against:
// upper-case, colon-separated. NM already returns that for BSSIDs, but ARP and
// user input do not always, and a case mismatch would silently never match.
QString normalizeMac(const QString &mac);
}
