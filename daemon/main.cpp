// SPDX-License-Identifier: GPL-2.0-or-later
//
// smart-unlockd -- the trust engine. The settings UI only writes a file; this is
// what makes the phone behave. It watches three things and, whenever any of
// them says "this is a moment I trust", stops the phone locking; the instant
// none of them do, it re-arms the lock and, if the screen is already off,
// locks now so the next wake asks for the PIN.
//
// HOW "don't ask for the PIN" IS DONE, and why this way.
// Not by faking an unlock -- kscreenlocker's greeter goes through PAM and there
// is no honest way to dismiss it from outside. Instead we stop the lock from
// engaging in the first place, with two supported knobs in kscreenlockerrc
// [Daemon]: Autolock (locking on idle) and LockOnResume (locking on wake from
// suspend). BOTH are needed here, because the surya suspends when the screen
// goes off: clearing only Autolock still asks for the PIN on the way back, which
// is exactly what it did until this was found. With both cleared the screen
// still blanks and the phone still suspends (battery is fine), it just does not
// put the greeter up, so turning the screen back on lands on the desktop.
//
// FAIL SAFE. If this daemon is not running, nothing here applies and the phone
// locks the normal way -- the secure default is the absence of the feature.
// On any clean stop we force Autolock back on (see --rearmar, wired to the
// unit's ExecStopPost), and we never lock the user out on our own startup.
//
// HOW "is the screen already off" IS ANSWERED. Not by the screen locker's
// session idle time: on this phone (Plasma Mobile on Wayland) that call answers
// org.freedesktop.DBus.Error.NotSupported, measured on the device, so a daemon
// leaning on it would fall through to its fail-closed branch every single time
// and lock the screen in the user's face mid-tap. The backlight is the honest
// answer instead -- /sys/class/backlight/*/bl_power is 0 while lit -- and the
// idle call is still tried first, so a platform that does implement it keeps
// the better signal.

#include <optional>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QTimer>
#include <QtGlobal>

#include <KConfigGroup>
#include <KSharedConfig>

#include "src/config.h"
#include "src/netinfo.h"

namespace
{
// This daemon decides, silently, whether the phone asks for a PIN. When it gets
// that wrong there is nothing to look at -- no window, no output, and the one
// visible symptom (the phone locks, or does not) is the same whether the logic
// said no or the daemon never ran. So it says what it decided and why, into the
// journal: journalctl --user -u smart-unlockd -f
// QtInfoMsg, not the default. Qt leaves a bare category at 'warning', so the
// qCInfo lines below -- the ones that say what was decided -- would be dropped
// before reaching the journal, and the daemon would be exactly as silent as it
// was when it cost an evening to work out what it was doing. Debug stays off
// until asked for: QT_LOGGING_RULES='desbloqueo.debug=true'
Q_LOGGING_CATEGORY(LOG, "smart-unlock", QtInfoMsg)

// Only used where the screen locker actually implements GetSessionIdleTime --
// not on this phone. Where it does, being idle at least this long means "off /
// not in the user's hand" and we lock immediately; below it we assume the phone
// is in use and leave it, and the next blank locks it anyway. Milliseconds,
// because that call reports KIdleTime's units.
constexpr uint IDLE_LOCK_MS = 30'000;

// Where the backlight lives. bl_power follows the old framebuffer blanking
// convention: 0 is lit, anything else (4 = FB_BLANK_POWERDOWN) is off. Some
// panels never move bl_power and just drop the brightness to 0 instead, so both
// are read and either one saying "dark" is taken as dark.
const QLatin1String BACKLIGHT_DIR{"/sys/class/backlight"};

// Belt-and-braces re-check even when no signal fires: catches a schedule
// boundary, a grace expiry, and any Wi-Fi roam that did not raise a D-Bus
// signal. Event-driven for the rest.
constexpr int TICK_MS = 20'000;

// How long every call to the screen locker is allowed to take. D-Bus defaults
// to 25 SECONDS, and a daemon stuck that long against a service that is still
// starting is half of what reboot-looped this phone. Nothing here is worth
// waiting on: if the locker cannot answer in two seconds, treat it as absent
// and decide without it.
constexpr int SAVER_TIMEOUT_MS = 2'000;

// Nothing at all happens for this long after the daemon starts. It is started
// by graphical-session.target, i.e. WHILE the session is still coming up, and
// talking to the screen locker at that moment takes the session down with it --
// which the boot watchdog answers by rebooting. There is nothing to decide in
// the first minute anyway: the phone was just unlocked to get here.
constexpr int STARTUP_DELAY_MS = 60'000;

// A gateway MAC that has not resolved YET is not evidence of a different
// network, and treating it as one was the single biggest source of noise this
// daemon produced. Measured over one day on this phone: 15 of 33 trust changes
// came from an ARP table that had not filled in, each one flipping trust and
// flipping it back within ten seconds. That is not free -- every flip restarts
// PowerDevil (see setLockBeforeScreenOff), and a PowerDevil that has just
// started re-registers its idle timeouts, so the screen's countdown to blanking
// begins again from zero. Enough flips and the screen simply never turns off.
//
// So an unresolved gateway answers "unknown" and we look again shortly. The
// wait is bounded on purpose: it is the second factor against a cloned BSSID,
// and an attacker who can answer the beacon can also decline to answer ARP.
// Past the deadline, unknown becomes untrusted and the phone locks.
//
// Thirty seconds is measured, not guessed. A plain Wi-Fi reconnect keeps the
// gateway in the neighbour cache and resolves instantly; the worst case is a
// FLUSHED table, and that took 15.3 s on this phone. The first version of this
// used 20 s, which cleared that measurement by too little to be worth trusting.
// Overshooting the deadline is not dangerous -- it just locks the phone, which
// is what the old code did immediately -- so the margin is cheap and the
// alternative is the flapping this whole thing exists to stop.
constexpr int GATEWAY_RETRY_MS = 2'000;
constexpr qint64 GATEWAY_WAIT_MS = 30'000;

// Try the reload/lock call on both names the screen locker answers to, so a
// difference between Plasma versions does not silently leave us writing a
// config nobody re-reads.
const char *const SAVER_SERVICES[] = {"org.freedesktop.ScreenSaver", "org.kde.screensaver"};
const QLatin1String SAVER_PATH{"/ScreenSaver"};
const QLatin1String SAVER_IFACE{"org.freedesktop.ScreenSaver"};

// Every call to the screen locker goes through here, and deliberately NOT
// through QDBusInterface: constructing one introspects the remote object first,
// synchronously, which is an extra blocking round trip against a service that
// may not be up yet. A plain message with an explicit timeout has neither
// problem. Returns an invalid message if nobody answered.
QDBusMessage saverCall(const QString &iface, const QString &method)
{
    for (const char *service : SAVER_SERVICES) {
        QDBusMessage msg =
            QDBusMessage::createMethodCall(QLatin1String(service), SAVER_PATH, iface, method);
        const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, SAVER_TIMEOUT_MS);
        if (reply.type() == QDBusMessage::ReplyMessage) {
            return reply;
        }
    }
    return {};
}

// Set kscreenlockerrc [Daemon] Autolock and, ONLY if that actually changed
// something, make the locker re-read it.
//
// The early return is not an optimisation, it is the fix for a phone that
// rebooted in a loop. Asking the locker to reload its configuration is not
// free: done while the session is still starting it took the session down, and
// the watchdog turned that into a reboot. The overwhelmingly common case --
// feature idle, Autolock already true -- used to reach that reload on every
// recompute for no reason at all. Now an unchanged value touches nothing.
// THE ONE THAT ACTUALLY LOCKS THIS PHONE.
//
// PowerDevil locks the screen before turning it off, and on a phone it does so
// BY DEFAULT. From its own source (Plasma/6.7, daemon/actions/bundled/dpms.cpp):
//
//     if (m_lockBeforeTurnOff && (type == u"TurnOff" || type == u"ToggleOnOff")) {
//         lockScreen();
//     }
//
// and the default comes from defaultLockBeforeTurnOffDisplay(bool isMobile),
// whose entire body is `return isMobile;`.
//
// That is why nothing else worked. Autolock covers locking on idle and
// LockOnResume locking on wake from suspend; neither is this path. Even the
// power button's own action is beside the point -- the condition fires for both
// TurnOff and ToggleOnOff, so no choice of button action avoids it. Measured on
// the phone with busctl: every press produced one Lock() from
// /usr/lib/libexec/org_kde_powerdevil, and with this key cleared, none.
//
// The key lives in the Display subgroup of each power profile.
const char *const POWER_PROFILES[] = {"AC", "Battery", "LowBattery"};

void setLockBeforeScreenOff(bool locking)
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("powerdevilrc"));

    bool changed = false;
    for (const char *profile : POWER_PROFILES) {
        KConfigGroup display = KConfigGroup(cfg, QLatin1String(profile)).group(QStringLiteral("Display"));
        if (locking) {
            // Back to PowerDevil's own default, which on a phone is to lock.
            // Deleting beats writing `true`: it restores whatever the system
            // decided rather than pinning our guess at it.
            if (display.hasKey("LockBeforeTurnOffDisplay")) {
                display.deleteEntry("LockBeforeTurnOffDisplay");
                changed = true;
            }
        } else if (display.readEntry("LockBeforeTurnOffDisplay", true)) {
            display.writeEntry("LockBeforeTurnOffDisplay", false);
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    cfg->sync();
    qCInfo(LOG) << "powerdevilrc LockBeforeTurnOffDisplay ->" << !locking
                << (locking ? "(locks again when the screen turns off)" : "(no longer locks when the screen turns off)");

    // RESTART, not reparseConfiguration. PowerDevil answers that call happily and
    // goes on using the old value -- measured: the key was on disk and the very
    // next press still locked. Restarting the unit is what actually takes. It is
    // a user service that comes back in well under a second, and this runs only
    // when trust changes, which is a couple of times a day.
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                     QStringLiteral("/org/freedesktop/systemd1"),
                                                     QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                     QStringLiteral("RestartUnit"));
    msg << QStringLiteral("plasma-powerdevil.service") << QStringLiteral("replace");
    QDBusConnection::sessionBus().call(msg, QDBus::Block, SAVER_TIMEOUT_MS);
}

// The screen locker's own two knobs. Autolock governs locking on idle;
// LockOnResume governs locking when the machine wakes from suspend. Neither is
// what the power button uses (see setLockBeforeScreenOff), but both still have to be
// cleared: otherwise the phone locks itself after the idle timeout, or on the
// way back from a suspend, and the button being polite would not save it.
void setAutolock(bool locking)
{
    setLockBeforeScreenOff(locking);

    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    KConfigGroup daemon(cfg, QStringLiteral("Daemon"));

    if (daemon.readEntry("Autolock", true) == locking
        && daemon.readEntry("LockOnResume", true) == locking) {
        return;
    }

    daemon.writeEntry("Autolock", locking);
    daemon.writeEntry("LockOnResume", locking);
    cfg->sync();
    qCInfo(LOG) << "kscreenlockerrc Autolock + LockOnResume ->" << locking;
    saverCall(QStringLiteral("org.kde.screensaver"), QStringLiteral("configure"));
}

bool screensaverActive()
{
    const QList<QVariant> args = saverCall(SAVER_IFACE, QStringLiteral("GetActive")).arguments();
    return !args.isEmpty() && args.first().toBool();
}

// Idle time in milliseconds, or -1 if the locker would not answer -- in which
// case the caller fails closed and locks.
qint64 sessionIdleMs()
{
    const QList<QVariant> args = saverCall(SAVER_IFACE, QStringLiteral("GetSessionIdleTime")).arguments();
    if (args.isEmpty()) {
        return -1;
    }
    bool ok = false;
    const qint64 ms = args.first().toLongLong(&ok);
    return ok ? ms : -1;
}

// Read one integer out of a sysfs attribute, or -1 if it is not there.
qint64 readSysfsInt(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    bool ok = false;
    const qint64 v = f.readAll().trimmed().toLongLong(&ok);
    return ok ? v : -1;
}

// Is the panel dark right now? Returns nullopt when there is no backlight to
// ask -- the caller decides what to do with "no idea", rather than getting a
// false "it is lit" that would quietly skip locking.
std::optional<bool> screenIsOff()
{
    const QStringList panels = QDir(BACKLIGHT_DIR).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    bool answered = false;

    for (const QString &panel : panels) {
        const QString base = QString(BACKLIGHT_DIR) + QLatin1Char('/') + panel + QLatin1Char('/');

        const qint64 power = readSysfsInt(base + QStringLiteral("bl_power"));
        if (power > 0) {
            return true; // blanked
        }

        // actual_brightness is what the panel really shows; brightness is only
        // what was last asked for, so it is the fallback of the fallback.
        qint64 lit = readSysfsInt(base + QStringLiteral("actual_brightness"));
        if (lit < 0) {
            lit = readSysfsInt(base + QStringLiteral("brightness"));
        }
        if (lit == 0) {
            return true; // lit by nothing
        }
        if (power == 0 || lit > 0) {
            answered = true; // this panel is definitely on
        }
    }

    if (answered) {
        return false;
    }
    return std::nullopt;
}

void lockNow()
{
    saverCall(SAVER_IFACE, QStringLiteral("Lock"));
}
}

// Three answers, not two. "Unknown" is the gateway MAC that has not resolved
// yet -- the network may well be the trusted one, we cannot tell yet, and
// saying "no" to that question is what made the phone flap.
enum class NetTrust { No, Yes, Unknown };

class TrustEngine : public QObject
{
    Q_OBJECT

public:
    // NOTHING here may touch the session. This object is constructed by
    // graphical-session.target, which means the session is still coming up, and
    // a daemon that talks to the screen locker at that moment takes the session
    // down with it -- and the boot watchdog turns an unfinished startup into a
    // reboot. That is not hypothetical: it reboot-looped this phone, four
    // seconds after the unit started, every boot, until the unit was disabled
    // over SSH between reboots.
    //
    // So the constructor does only what is safe in any state: read a file and
    // arm timers. Everything that speaks D-Bus waits for start(), a minute in.
    TrustEngine()
    {
        m_settings = SmartUnlockConfig::load();

        // Re-read the file when the UI writes it. Two traps here, both hit:
        //
        //  - QFileSystemWatcher drops the path when the file is replaced, and
        //    KConfig replaces it (writes a temp file and renames), so the path
        //    has to be re-added on every change.
        //  - addPath() FAILS on a file that does not exist yet, which is
        //    exactly the state of a fresh install. Watching the directory too
        //    is what notices the settings being saved for the very first time.
        const QString cfgPath = SmartUnlockConfig::filePath();
        m_watcher.addPath(cfgPath);
        m_watcher.addPath(QFileInfo(cfgPath).absolutePath());
        const auto onChange = [this, cfgPath](const QString &) {
            if (!m_watcher.files().contains(cfgPath) && QFile::exists(cfgPath)) {
                m_watcher.addPath(cfgPath);
            }
            reload();
        };
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, onChange);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, onChange);

        // The tick re-reads the settings, it does not just recompute with the
        // ones it already has. Watching a config file is best-effort by nature
        // -- editors, syncs and first writes all defeat it in different ways --
        // and re-reading one small file every 20s is not worth being clever
        // about. Without this, a watcher that missed an edit meant a daemon
        // running on stale settings forever, with no symptom other than
        // "it just does not work".
        m_tick.setInterval(TICK_MS);
        connect(&m_tick, &QTimer::timeout, this, [this] { reload(); });

        m_graceTimer.setSingleShot(true);
        connect(&m_graceTimer, &QTimer::timeout, this, [this] { recompute(); });

        m_gatewayTimer.setSingleShot(true);
        connect(&m_gatewayTimer, &QTimer::timeout, this, [this] { recompute(); });

        QTimer::singleShot(STARTUP_DELAY_MS, this, [this] { start(); });
    }

    // The session has had its minute. From here on the daemon behaves normally.
    void start()
    {
        // Seed the known lock state so that the FIRST unlock after this --
        // e.g. we came up with the phone already locked -- is still recognised
        // as an unlock and can arm the grace window.
        m_wasActive = screensaverActive();

        connectSignals();
        m_tick.start();
        m_started = true;
        qCInfo(LOG) << "starting: enabled" << m_settings.enabled << "networks"
                    << m_settings.networks.size() << "windows" << m_settings.windows.size();
        recompute();
    }

    // One-shot used by the unit's ExecStopPost: whatever we were doing, put the
    // normal lock back before the process is gone, so a stop can never leave the
    // phone unlockable-by-design.
    static void rearm()
    {
        setAutolock(true);
    }

private:
    void connectSignals()
    {
        QDBusConnection system = QDBusConnection::systemBus();

        // Overall connectivity flips (joined / left a network).
        system.connect(QStringLiteral("org.freedesktop.NetworkManager"),
                       QStringLiteral("/org/freedesktop/NetworkManager"),
                       QStringLiteral("org.freedesktop.NetworkManager"),
                       QStringLiteral("StateChanged"), this, SLOT(onNetworkChanged()));

        // Per-device transitions (empty path = any device): this is what fires
        // precisely on the Wi-Fi going up or down.
        system.connect(QStringLiteral("org.freedesktop.NetworkManager"), QString(),
                       QStringLiteral("org.freedesktop.NetworkManager.Device"),
                       QStringLiteral("StateChanged"), this, SLOT(onNetworkChanged()));

        // The screen locker announces lock/unlock. An unlock (active -> false)
        // is the "I just proved it's me" that arms the grace window.
        QDBusConnection session = QDBusConnection::sessionBus();
        for (const char *service : SAVER_SERVICES) {
            session.connect(QLatin1String(service), SAVER_PATH, SAVER_IFACE,
                            QStringLiteral("ActiveChanged"), this, SLOT(onActiveChanged(bool)));
        }
    }

    NetTrust networkTrusted() const
    {
        bool anyActive = false;
        for (const TrustedNetwork &n : m_settings.networks) {
            if (n.active) {
                anyActive = true;
                break;
            }
        }
        if (!anyActive) {
            return NetTrust::No;
        }

        const CurrentWifi w = NetInfo::currentWifi();
        if (!w.connected) {
            qCDebug(LOG) << "network: not on Wi-Fi";
            return NetTrust::No;
        }
        qCDebug(LOG) << "network: on" << w.ssid << "bssid" << w.bssid << "gateway" << w.gatewayMac;

        for (const TrustedNetwork &n : m_settings.networks) {
            if (!n.active || n.bssid != w.bssid) {
                continue;
            }
            // BSSID matched. If a gateway MAC was captured, it must match too --
            // the second factor. If it was never captured (blank), fall back to
            // BSSID alone rather than lock the user out of a network they added
            // on purpose. If it was captured but the current gateway is unknown
            // or different, this is NOT the trusted network: keep it locked.
            if (n.gateway.isEmpty()) {
                qCDebug(LOG) << "network: trusted by BSSID alone (no gateway captured)";
                return NetTrust::Yes;
            }
            if (w.gatewayMac == n.gateway) {
                return NetTrust::Yes;
            }
            // Blank is "not resolved yet", NOT "a different MAC". The caller
            // holds the previous verdict for a few seconds rather than
            // announcing a change the network never made.
            if (w.gatewayMac.isEmpty()) {
                qCDebug(LOG) << "network: BSSID matched, gateway not resolved yet -- waiting";
                return NetTrust::Unknown;
            }
            qCInfo(LOG) << "network: BSSID matched but gateway did not --" << w.gatewayMac
                        << "expected" << n.gateway;
            return NetTrust::No;
        }
        return NetTrust::No;
    }

    // Collapse the three-valued answer into the yes/no the decision needs.
    // "Unknown" keeps whatever the network last said and looks again in a
    // couple of seconds, so a gateway that is merely slow to appear in the ARP
    // table never registers as a change of network. The wait is bounded: past
    // GATEWAY_WAIT_MS an unresolved gateway becomes untrusted, which is the
    // direction that locks the phone.
    bool resolveNet()
    {
        const NetTrust net = networkTrusted();
        if (net != NetTrust::Unknown) {
            if (m_gatewayUnknownSince.isValid()) {
                qCInfo(LOG) << "network: gateway resolved after"
                            << m_gatewayUnknownSince.msecsTo(QDateTime::currentDateTime())
                            << "ms";
            }
            m_gatewayUnknownSince = QDateTime();
            m_gatewayTimer.stop();
            m_lastNet = (net == NetTrust::Yes);
            return m_lastNet;
        }

        const QDateTime now = QDateTime::currentDateTime();
        if (!m_gatewayUnknownSince.isValid()) {
            m_gatewayUnknownSince = now;
            // Said out loud, once per episode. The lesson this daemon was built
            // on is that a decision nobody can see is a decision nobody can
            // debug: "the gateway has not resolved" and "you are not home" used
            // to look identical from outside, and telling them apart is the
            // whole point of the wait.
            qCInfo(LOG) << "network: gateway not resolved yet -- holding trust ="
                        << m_lastNet << "for up to" << GATEWAY_WAIT_MS / 1000 << "s";
        }
        if (m_gatewayUnknownSince.msecsTo(now) >= GATEWAY_WAIT_MS) {
            qCInfo(LOG) << "network: gateway still unresolved after"
                        << GATEWAY_WAIT_MS / 1000 << "s -- treating as untrusted";
            m_gatewayTimer.stop();
            m_lastNet = false;
            return false;
        }
        m_gatewayTimer.start(GATEWAY_RETRY_MS);
        return m_lastNet;
    }

    bool scheduleActive() const
    {
        if (!m_settings.scheduleEnabled) {
            return false;
        }
        const QDateTime now = QDateTime::currentDateTime();
        for (const ScheduleWindow &w : m_settings.windows) {
            if (SmartUnlockConfig::windowActiveAt(w, now)) {
                return true;
            }
        }
        return false;
    }

    bool graceActive() const
    {
        if (!m_settings.graceAfterUnlock || !m_graceUntil.isValid()) {
            return false;
        }
        return QDateTime::currentDateTime() < m_graceUntil;
    }

    void recompute()
    {
        // The file watcher can fire during the startup delay -- if it did any
        // of this then, the delay would buy nothing.
        if (!m_started) {
            return;
        }

        const bool net = resolveNet();
        const bool grace = graceActive();
        const bool sched = scheduleActive();

        // LOST MODE BEATS EVERYTHING. If lost-phone has been told the phone is
        // lost, this daemon must not grant trust -- not even on the network you
        // marked, and especially not on that network, because "somebody took it
        // from my flat" is the case where a trusted Wi-Fi and a thief are the
        // same place. Without this, a remote lock would be undone seconds later
        // by us, and the only symptom would be a phone that unlocks itself.
        //
        // Read from a file rather than asked over D-Bus on purpose: it has to
        // hold even if lost-phoned is not running, and a dead daemon must fail
        // towards locked. The path is a documented contract between the two
        // packages; see lost-phone's src/lostmode.h.
        const bool lost = lostPhoneActive();
        const bool trusted = m_settings.enabled && !lost && (net || grace || sched);

        if (trusted != m_applied.value_or(!trusted)) {
            qCInfo(LOG) << "trust:" << trusted << "(enabled" << m_settings.enabled
                        << "lost" << lost << "network" << net << "grace" << grace
                        << "schedule" << sched << ")";
        }
        applyTrust(trusted);

        // Keep a precise wake-up for the grace edge, so trust does not linger up
        // to a full tick past its minute.
        if (graceActive()) {
            const qint64 ms = QDateTime::currentDateTime().msecsTo(m_graceUntil);
            if (ms > 0) {
                m_graceTimer.start(ms);
            }
        }
    }

    // Is lost-phone holding this phone in lost mode? One small file read, cheap
    // enough for every decision, and deliberately tolerant: if lost-phone is not
    // installed at all there is no file, no lost mode, and nothing changes.
    //
    // Format, fixed by contract: first line "1" or "0".
    static bool lostPhoneActive()
    {
        const QString path =
            QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
            + QStringLiteral("/lost-phone/modo-perdido");
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        return QString::fromUtf8(f.readLine()).trimmed() == QLatin1String("1");
    }

    void applyTrust(bool trusted)
    {
        // First run: assert the config to match, but never lock on our own
        // startup -- booting should not throw the user to a lock screen.
        if (!m_applied.has_value()) {
            setAutolock(!trusted);
            m_applied = trusted;
            return;
        }
        if (*m_applied == trusted) {
            return;
        }

        setAutolock(!trusted);
        m_applied = trusted;

        if (!trusted) {
            lockIfScreenOff();
        }
    }

    // "When the screen turns off (or now if already off)": re-arming Autolock covers the
    // next blank on its own. This only handles the case where the screen is
    // ALREADY off when trust is lost -- there is no future blank to trigger on,
    // so we lock now. In-use (recent activity) is left alone. If the locker will
    // not tell us, we fail closed and lock.
    void lockIfScreenOff()
    {
        if (screensaverActive()) {
            return; // already locked
        }
        // Best signal first, where it exists at all.
        const qint64 idle = sessionIdleMs();
        if (idle >= 0) {
            if (idle >= IDLE_LOCK_MS) {
                lockNow();
            }
            return;
        }

        // It does not exist here, so ask the panel. Screen lit = phone in hand:
        // leave it, the next blank locks it. Screen dark = there is no next
        // blank to wait for, so lock now.
        const std::optional<bool> off = screenIsOff();
        if (off.has_value()) {
            if (*off) {
                lockNow();
            }
            return;
        }

        // Neither answered: fail closed, because the alternative is a phone
        // that silently stopped locking.
        lockNow();
    }

    void reload()
    {
        m_settings = SmartUnlockConfig::load();
        recompute();
    }

private Q_SLOTS:
    void onNetworkChanged()
    {
        recompute();
    }

    void onActiveChanged(bool active)
    {
        // Transition to unlocked = a successful PIN entry. Arm the grace window
        // if the user asked for one; it only bites when nothing else grants
        // trust, so it is harmless when on a trusted network.
        if (m_wasActive && !active && m_settings.graceAfterUnlock) {
            m_graceUntil = QDateTime::currentDateTime().addSecs(qint64(m_settings.graceMinutes) * 60);
        }
        m_wasActive = active;
        recompute();
    }

private:
    Settings m_settings;
    QFileSystemWatcher m_watcher;
    QTimer m_tick;
    QTimer m_graceTimer;
    QTimer m_gatewayTimer;

    std::optional<bool> m_applied; // last Autolock decision, unset before first
    bool m_wasActive = false;      // last known screensaver state
    bool m_started = false;        // false until the startup delay has passed
    QDateTime m_graceUntil;        // when the post-unlock grace ends
    QDateTime m_gatewayUnknownSince; // when the gateway MAC first read blank
    bool m_lastNet = false;        // last verdict the network actually gave
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("smart-unlockd"));

    // ExecStopPost path: force the lock back and exit, without a running engine.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--rearmar") == 0) {
            TrustEngine::rearm();
            return 0;
        }
    }

    TrustEngine engine;

    // On the way out for any reason, restore the normal lock. Runs on SIGTERM
    // (systemd stop) too; the ExecStopPost is the backstop if we are killed.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { TrustEngine::rearm(); });

    return app.exec();
}

#include "main.moc"
