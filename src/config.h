// SPDX-License-Identifier: GPL-2.0-or-later
//
// The on-disk shape of the whole feature, in one place. Everything reads and
// writes ~/.config/smart-unlockrc through here: the settings UI (backend.cpp),
// the Settings module and the daemon (smart-unlockd). One schema, one loader, so
// the front-end that WRITES a value and the daemon that ACTS on it can never
// disagree about where it lives or what it means.
//
// Why a plain struct and free functions instead of a QObject: the daemon has no
// UI and no properties to bind, it just needs the values; and the backend wants
// to edit a working copy and only commit it on Apply. A value type does both
// without either side inheriting the other's baggage.

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QTime>

// One Wi-Fi the phone trusts. Identified by the access point's BSSID (its MAC),
// not the SSID: the SSID is a label anyone can retype, while the BSSID at least
// has to be cloned. It is still spoofable -- a beacon can carry any MAC -- so a
// second factor is stored next to it: the MAC of the default gateway, learned
// from the ARP table while actually connected. Faking both at once, on the same
// subnet, is a good deal more work than renaming a hotspot. See netinfo.cpp.
struct TrustedNetwork {
    QString bssid;    // "AA:BB:CC:DD:EE:FF", upper-case, colon-separated
    QString ssid;     // human label, captured when the network was added
    QString gateway;  // gateway MAC, same format; empty if it could not be read
    bool active = true; // in the trusted set right now, or just remembered
};

// A recurring stretch of time the phone trusts on its own, with no network
// involved: "at home in the evenings", "all weekend". The day flags name the
// day the window STARTS on, so a window that runs past midnight (end < start)
// still belongs to the evening you set it for. See windowActiveAt().
struct ScheduleWindow {
    QString days;   // subset of "1234567", 1=Monday .. 7=Sunday (QDate::dayOfWeek)
    QTime start;
    QTime end;      // may be earlier than start: the window wraps past midnight
};

struct Settings {
    // Master switch. With it off the daemon keeps running but asserts the
    // normal locked-by-default behaviour and never grants trust, so turning the
    // feature off can never leave the phone stuck unlocked.
    bool enabled = false;

    // "Don't ask again for a while after I just proved it's me." Started by the
    // unlock itself, not by a button: see smart-unlockd.
    bool graceAfterUnlock = false;
    int graceMinutes = 5;

    bool scheduleEnabled = false;
    QList<ScheduleWindow> windows;

    QList<TrustedNetwork> networks;
};

namespace SmartUnlockConfig
{
// ~/.config/smart-unlockrc, resolved through QStandardPaths so it follows
// XDG_CONFIG_HOME like every other KConfig file on the phone.
QString filePath();

Settings load();
void save(const Settings &settings);

// True if `when` (local time) falls inside the window, including the
// past-midnight case. Pure, so the daemon and the tests can both call it
// without a running clock.
bool windowActiveAt(const ScheduleWindow &window, const QDateTime &when);
}
