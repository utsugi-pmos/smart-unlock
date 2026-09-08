// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.h"

#include <QStandardPaths>

#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
const QLatin1String kConfigName{"smart-unlockrc"};

// Networks live one per group, named "Red <BSSID>", so a BSSID with its colons
// is a legal group name and nothing has to be escaped into a single line. The
// ordered list of which ones exist -- and in what order the UI shows them --
// is kept separately, because a QSet/groupList() has no stable order and the
// list would otherwise reshuffle on every save.
const QLatin1String kNetGroupPrefix{"Red "};

QString netGroup(const QString &bssid)
{
    return kNetGroupPrefix + bssid;
}
}

QString SmartUnlockConfig::filePath()
{
    // openConfig() with a bare name already lands here; this is only so the
    // daemon can hand the exact path to a QFileSystemWatcher.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1Char('/') + kConfigName;
}

Settings SmartUnlockConfig::load()
{
    Settings s;

    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(kConfigName);

    const KConfigGroup gen(cfg, QStringLiteral("General"));
    // false, always, and never anything else. A fresh install has no config
    // file at all, and this is what decides what that means: the phone locks
    // exactly as it did before the package existed. Trusted unlock is something
    // the user turns on, knowing what they are turning on -- it is never the
    // state you get by installing something.
    s.enabled = gen.readEntry("Activado", false);

    const KConfigGroup gr(cfg, QStringLiteral("Gracia"));
    s.graceAfterUnlock = gr.readEntry("TrasDesbloqueo", false);
    s.graceMinutes = gr.readEntry("Minutos", 5);

    const KConfigGroup ho(cfg, QStringLiteral("Horario"));
    s.scheduleEnabled = ho.readEntry("Activado", false);
    const QStringList windows = ho.readEntry("Ventanas", QStringList());
    for (const QString &raw : windows) {
        // "days|HH:mm|HH:mm". Anything that does not parse is dropped rather
        // than guessed: a half-read window that silently defaults to all-day
        // would be a lock the user never asked to drop.
        const QStringList parts = raw.split(QLatin1Char('|'));
        if (parts.size() != 3) {
            continue;
        }
        ScheduleWindow w;
        w.days = parts.at(0);
        w.start = QTime::fromString(parts.at(1), QStringLiteral("HH:mm"));
        w.end = QTime::fromString(parts.at(2), QStringLiteral("HH:mm"));
        if (w.days.isEmpty() || !w.start.isValid() || !w.end.isValid()) {
            continue;
        }
        s.windows.append(w);
    }

    const KConfigGroup redes(cfg, QStringLiteral("Redes"));
    const QStringList order = redes.readEntry("Orden", QStringList());
    for (const QString &bssid : order) {
        const KConfigGroup ng(cfg, netGroup(bssid));
        if (!ng.exists()) {
            continue;
        }
        TrustedNetwork n;
        n.bssid = bssid;
        n.ssid = ng.readEntry("Ssid", QString());
        n.gateway = ng.readEntry("Gateway", QString());
        n.active = ng.readEntry("Activa", true);
        s.networks.append(n);
    }

    return s;
}

void SmartUnlockConfig::save(const Settings &s)
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(kConfigName);

    KConfigGroup(cfg, QStringLiteral("General")).writeEntry("Activado", s.enabled);

    KConfigGroup gr(cfg, QStringLiteral("Gracia"));
    gr.writeEntry("TrasDesbloqueo", s.graceAfterUnlock);
    gr.writeEntry("Minutos", s.graceMinutes);

    KConfigGroup ho(cfg, QStringLiteral("Horario"));
    ho.writeEntry("Activado", s.scheduleEnabled);
    QStringList windows;
    windows.reserve(s.windows.size());
    for (const ScheduleWindow &w : s.windows) {
        windows.append(QStringLiteral("%1|%2|%3")
                           .arg(w.days,
                                w.start.toString(QStringLiteral("HH:mm")),
                                w.end.toString(QStringLiteral("HH:mm"))));
    }
    ho.writeEntry("Ventanas", windows);

    // Networks: wipe the ones we own and rewrite from the current list, the way
    // appsvivas rewrites its window rules. Only "Red *" groups are touched, so
    // nothing else in the file is at risk if the schema ever grows a sibling.
    const QStringList existing = cfg->groupList();
    for (const QString &g : existing) {
        if (g.startsWith(kNetGroupPrefix)) {
            cfg->deleteGroup(g);
        }
    }

    QStringList order;
    order.reserve(s.networks.size());
    for (const TrustedNetwork &n : s.networks) {
        order.append(n.bssid);
        KConfigGroup ng(cfg, netGroup(n.bssid));
        ng.writeEntry("Ssid", n.ssid);
        ng.writeEntry("Gateway", n.gateway);
        ng.writeEntry("Activa", n.active);
    }
    KConfigGroup(cfg, QStringLiteral("Redes")).writeEntry("Orden", order);

    cfg->sync();
}

bool SmartUnlockConfig::windowActiveAt(const ScheduleWindow &w, const QDateTime &when)
{
    const QTime t = when.time();
    const int dow = when.date().dayOfWeek();       // 1..7, Mon..Sun
    const int prevDow = (dow == 1) ? 7 : dow - 1;

    const auto dayEnabled = [&w](int d) {
        return w.days.contains(QChar(QLatin1Char('0' + d)));
    };

    if (w.start < w.end) {
        // Same-day window: [start, end) on an enabled day.
        return dayEnabled(dow) && t >= w.start && t < w.end;
    }
    if (w.start > w.end) {
        // Wraps past midnight. Two ways to be inside it: after `start` on the
        // day it began, or before `end` on the morning after -- and the morning
        // after belongs to YESTERDAY's flag, which is the day the user set.
        return (dayEnabled(dow) && t >= w.start) || (dayEnabled(prevDow) && t < w.end);
    }
    // start == end: read as "the whole of each enabled day".
    return dayEnabled(dow);
}
