// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared brain of both front-ends: the Settings module (kcm.cpp) and the
// standalone application (main.cpp). Same split as appsvivas, and for the same
// reason -- a KCM is a plugin tied to the KF6 ABI and an update can leave it
// unable to load, so the plain Qt/Kirigami app is the fallback that keeps the
// settings reachable. Both frame this one object; neither owns a copy of the
// logic.
//
// It edits a WORKING COPY of the settings and only writes ~/.config/smart-unlockrc
// on save(): the standalone app's Apply button means what it says, and the
// Settings module calls save() after every change. Writing the file is the
// whole handoff to the daemon: smart-unlockd watches it and re-reads on change.

#pragma once

#include <QObject>
#include <QVariantList>

#include "src/config.h"

class SmartUnlockBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

    Q_PROPERTY(bool graceEnabled READ graceEnabled WRITE setGraceEnabled NOTIFY changed)
    Q_PROPERTY(int graceMinutes READ graceMinutes WRITE setGraceMinutes NOTIFY changed)

    Q_PROPERTY(bool scheduleEnabled READ scheduleEnabled WRITE setScheduleEnabled NOTIFY changed)
    // [{days, daysLabel, start, end, index}]
    Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)

    // [{bssid, ssid, gateway, hasGateway, active}]
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)

    // The Wi-Fi we are on right now, so the UI can offer "trust this one" with
    // its real name instead of asking the user to type a MAC. Refreshed by
    // refreshCurrent(); read-only.
    Q_PROPERTY(bool currentConnected READ currentConnected NOTIFY currentChanged)
    Q_PROPERTY(QString currentSsid READ currentSsid NOTIFY currentChanged)
    Q_PROPERTY(QString currentBssid READ currentBssid NOTIFY currentChanged)
    Q_PROPERTY(bool currentHasGateway READ currentHasGateway NOTIFY currentChanged)
    Q_PROPERTY(bool currentAlreadyTrusted READ currentAlreadyTrusted NOTIFY changed)
    // True when a saved network carries THIS SSID but a different BSSID -- the
    // dual-band case. Without saying so out loud, the screen shows "you are on
    // HomeWiFi" above a trusted entry also called "HomeWiFi" and looks
    // entirely fine while the phone refuses to trust it.
    Q_PROPERTY(bool currentSsidSavedWithOtherBssid READ currentSsidSavedWithOtherBssid NOTIFY changed)

    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit SmartUnlockBackend(QObject *parent = nullptr);

    bool enabled() const;
    bool graceEnabled() const;
    int graceMinutes() const;
    bool scheduleEnabled() const;
    QVariantList windows() const;
    QVariantList networks() const;

    bool currentConnected() const;
    QString currentSsid() const;
    QString currentBssid() const;
    bool currentHasGateway() const;
    bool currentAlreadyTrusted() const;
    bool currentSsidSavedWithOtherBssid() const;

    bool dirty() const;

    void setEnabled(bool on);
    void setGraceEnabled(bool on);
    void setGraceMinutes(int minutes);
    void setScheduleEnabled(bool on);

    // Add the Wi-Fi we are connected to right now to the trusted set, capturing
    // its BSSID and gateway MAC. No-op if not on Wi-Fi; re-activates it if it
    // was already remembered.
    Q_INVOKABLE void trustCurrentNetwork();
    Q_INVOKABLE void setNetworkActive(const QString &bssid, bool active);
    Q_INVOKABLE void removeNetwork(const QString &bssid);

    // days: subset of "1234567". start/end: "HH:mm". Ignored if unparseable or
    // if no day is selected.
    Q_INVOKABLE void addWindow(const QString &days, const QString &start, const QString &end);
    Q_INVOKABLE void removeWindow(int index);

    Q_INVOKABLE void refreshCurrent();

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void restoreDefaults();

Q_SIGNALS:
    void changed();
    void currentChanged();
    void dirtyChanged();

private:
    void setDirty(bool dirty);
    int indexOfNetwork(const QString &bssid) const;

    Settings m_settings;

    bool m_currentConnected = false;
    QString m_currentSsid;
    QString m_currentBssid;
    bool m_currentHasGateway = false;

    bool m_dirty = false;
};
