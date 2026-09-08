// SPDX-License-Identifier: GPL-2.0-or-later

#include "backend.h"

#include "src/netinfo.h"

namespace
{
// Day letters in the phone's language, indexed 1..7 to match QDate::dayOfWeek.
// Index 0 is a filler so the digit doubles as the array position.
const char *const dayLetters[] = {"", "L", "M", "X", "J", "V", "S", "D"};

QString daysLabel(const QString &days)
{
    QStringList out;
    for (int d = 1; d <= 7; ++d) {
        if (days.contains(QChar(QLatin1Char('0' + d)))) {
            out.append(QString::fromLatin1(dayLetters[d]));
        }
    }
    return out.join(QLatin1Char(' '));
}
}

SmartUnlockBackend::SmartUnlockBackend(QObject *parent)
    : QObject(parent)
{
    load();
    refreshCurrent();
}

bool SmartUnlockBackend::enabled() const
{
    return m_settings.enabled;
}

bool SmartUnlockBackend::graceEnabled() const
{
    return m_settings.graceAfterUnlock;
}

int SmartUnlockBackend::graceMinutes() const
{
    return m_settings.graceMinutes;
}

bool SmartUnlockBackend::scheduleEnabled() const
{
    return m_settings.scheduleEnabled;
}

QVariantList SmartUnlockBackend::windows() const
{
    QVariantList out;
    out.reserve(m_settings.windows.size());
    for (int i = 0; i < m_settings.windows.size(); ++i) {
        const ScheduleWindow &w = m_settings.windows.at(i);
        out.append(QVariantMap{
            {QStringLiteral("days"), w.days},
            {QStringLiteral("daysLabel"), daysLabel(w.days)},
            {QStringLiteral("start"), w.start.toString(QStringLiteral("HH:mm"))},
            {QStringLiteral("end"), w.end.toString(QStringLiteral("HH:mm"))},
            {QStringLiteral("index"), i},
        });
    }
    return out;
}

QVariantList SmartUnlockBackend::networks() const
{
    QVariantList out;
    out.reserve(m_settings.networks.size());
    for (const TrustedNetwork &n : m_settings.networks) {
        out.append(QVariantMap{
            {QStringLiteral("bssid"), n.bssid},
            {QStringLiteral("ssid"), n.ssid},
            {QStringLiteral("gateway"), n.gateway},
            {QStringLiteral("hasGateway"), !n.gateway.isEmpty()},
            {QStringLiteral("active"), n.active},
        });
    }
    return out;
}

bool SmartUnlockBackend::currentConnected() const
{
    return m_currentConnected;
}

QString SmartUnlockBackend::currentSsid() const
{
    return m_currentSsid;
}

QString SmartUnlockBackend::currentBssid() const
{
    return m_currentBssid;
}

bool SmartUnlockBackend::currentHasGateway() const
{
    return m_currentHasGateway;
}

bool SmartUnlockBackend::currentAlreadyTrusted() const
{
    return m_currentConnected && indexOfNetwork(m_currentBssid) >= 0;
}

// A router with two bands hands out one BSSID per band -- often consecutive,
// differing in the last digit. Mark the network from one band and the phone
// stops recognising it the moment it associates with the other, which is a
// thing routers do on their own. The check is by BSSID on purpose (a name is
// trivial to fake), so this is working as intended; what was NOT intended is
// that the screen said nothing about it. Reported here so the interface can.
bool SmartUnlockBackend::currentSsidSavedWithOtherBssid() const
{
    if (!m_currentConnected || m_currentSsid.isEmpty() || currentAlreadyTrusted()) {
        return false;
    }
    for (const TrustedNetwork &n : m_settings.networks) {
        if (n.ssid == m_currentSsid) {
            return true;
        }
    }
    return false;
}

bool SmartUnlockBackend::dirty() const
{
    return m_dirty;
}

void SmartUnlockBackend::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

int SmartUnlockBackend::indexOfNetwork(const QString &bssid) const
{
    for (int i = 0; i < m_settings.networks.size(); ++i) {
        if (m_settings.networks.at(i).bssid == bssid) {
            return i;
        }
    }
    return -1;
}

void SmartUnlockBackend::setEnabled(bool on)
{
    if (m_settings.enabled == on) {
        return;
    }
    m_settings.enabled = on;
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::setGraceEnabled(bool on)
{
    if (m_settings.graceAfterUnlock == on) {
        return;
    }
    m_settings.graceAfterUnlock = on;
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::setGraceMinutes(int minutes)
{
    // Clamp rather than trust the spinbox: a zero would make the grace expire
    // the instant it started, and a wild value would be a lock that never
    // comes back. One day is already far past any "for a while".
    minutes = qBound(1, minutes, 24 * 60);
    if (m_settings.graceMinutes == minutes) {
        return;
    }
    m_settings.graceMinutes = minutes;
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::setScheduleEnabled(bool on)
{
    if (m_settings.scheduleEnabled == on) {
        return;
    }
    m_settings.scheduleEnabled = on;
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::trustCurrentNetwork()
{
    if (!m_currentConnected || m_currentBssid.isEmpty()) {
        return;
    }

    const CurrentWifi now = NetInfo::currentWifi();
    if (!now.connected) {
        return;
    }

    const int existing = indexOfNetwork(now.bssid);
    if (existing >= 0) {
        // Already remembered: re-arm it and refresh what we know, in case the
        // gateway MAC was blank the first time (ARP not yet populated).
        m_settings.networks[existing].active = true;
        if (m_settings.networks[existing].gateway.isEmpty()) {
            m_settings.networks[existing].gateway = now.gatewayMac;
        }
    } else {
        TrustedNetwork n;
        n.bssid = now.bssid;
        n.ssid = now.ssid;
        n.gateway = now.gatewayMac;
        n.active = true;
        m_settings.networks.append(n);
    }

    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::setNetworkActive(const QString &bssid, bool active)
{
    const int i = indexOfNetwork(bssid);
    if (i < 0 || m_settings.networks.at(i).active == active) {
        return;
    }
    m_settings.networks[i].active = active;
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::removeNetwork(const QString &bssid)
{
    const int i = indexOfNetwork(bssid);
    if (i < 0) {
        return;
    }
    m_settings.networks.removeAt(i);
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::addWindow(const QString &days, const QString &start, const QString &end)
{
    // Keep only the day digits, in order, without repeats: the UI hands us a
    // set of toggles and the on-disk form and the label both assume a clean
    // "1234567" subset.
    QString clean;
    for (int d = 1; d <= 7; ++d) {
        if (days.contains(QChar(QLatin1Char('0' + d)))) {
            clean.append(QChar(QLatin1Char('0' + d)));
        }
    }
    if (clean.isEmpty()) {
        return;
    }

    ScheduleWindow w;
    w.days = clean;
    w.start = QTime::fromString(start, QStringLiteral("HH:mm"));
    w.end = QTime::fromString(end, QStringLiteral("HH:mm"));
    if (!w.start.isValid() || !w.end.isValid()) {
        return;
    }

    m_settings.windows.append(w);
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::removeWindow(int index)
{
    if (index < 0 || index >= m_settings.windows.size()) {
        return;
    }
    m_settings.windows.removeAt(index);
    setDirty(true);
    Q_EMIT changed();
}

void SmartUnlockBackend::refreshCurrent()
{
    const CurrentWifi now = NetInfo::currentWifi();
    m_currentConnected = now.connected;
    m_currentSsid = now.ssid;
    m_currentBssid = now.bssid;
    m_currentHasGateway = !now.gatewayMac.isEmpty();
    Q_EMIT currentChanged();
    // currentAlreadyTrusted rides on `changed` because it also depends on the
    // edited network list, not just on what we are connected to.
    Q_EMIT changed();
}

void SmartUnlockBackend::load()
{
    m_settings = SmartUnlockConfig::load();
    setDirty(false);
    Q_EMIT changed();
}

void SmartUnlockBackend::save()
{
    SmartUnlockConfig::save(m_settings);
    setDirty(false);
}

void SmartUnlockBackend::restoreDefaults()
{
    m_settings = Settings{};
    setDirty(true);
    Q_EMIT changed();
}
