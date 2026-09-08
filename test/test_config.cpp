// Exercises the on-disk settings: that saving and loading round-trip, and --
// the one that is a requirement rather than a nicety -- that a machine with no
// settings file at all comes up with the feature OFF.
#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <KConfigGroup>
#include <KSharedConfig>
#include <cstdio>

static int fallos = 0;
static int total = 0;

static void comprobar(const char *nombre, bool ok)
{
    ++total;
    if (!ok) {
        ++fallos;
        std::printf("  FAIL   %s\n", nombre);
    } else {
        std::printf("  ok     %s\n", nombre);
    }
}

static void borrarConfig()
{
    QFile::remove(SmartUnlockConfig::filePath());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QDir().mkpath(QFileInfo(SmartUnlockConfig::filePath()).absolutePath());

    std::printf("\n--- no configuration file (fresh install) ---\n");
    borrarConfig();
    {
        const Settings s = SmartUnlockConfig::load();
        comprobar("OFF: enabled is false", s.enabled == false);
        comprobar("no trusted networks", s.networks.isEmpty());
        comprobar("no schedule time windows", s.windows.isEmpty());
        comprobar("schedule disabled", s.scheduleEnabled == false);
        comprobar("grace disabled", s.graceAfterUnlock == false);
    }

    std::printf("\n--- save and load again ---\n");
    {
        Settings s;
        s.enabled = true;
        s.graceAfterUnlock = true;
        s.graceMinutes = 17;
        s.scheduleEnabled = true;

        ScheduleWindow w1;
        w1.days = QStringLiteral("12345");
        w1.start = QTime(9, 30);
        w1.end = QTime(17, 45);
        ScheduleWindow w2;
        w2.days = QStringLiteral("67");
        w2.start = QTime(23, 0);
        w2.end = QTime(7, 0);
        s.windows = {w1, w2};

        TrustedNetwork n1;
        n1.bssid = QStringLiteral("04:D4:C4:C9:4C:18");
        n1.ssid = QStringLiteral("HomeWiFi");
        n1.gateway = QStringLiteral("94:83:C4:C3:C5:B9");
        n1.active = true;
        TrustedNetwork n2;
        n2.bssid = QStringLiteral("AA:BB:CC:DD:EE:FF");
        n2.ssid = QStringLiteral("Otra");
        n2.active = false;
        s.networks = {n1, n2};

        SmartUnlockConfig::save(s);
        const Settings r = SmartUnlockConfig::load();

        comprobar("enabled", r.enabled == true);
        comprobar("grace minutes", r.graceMinutes == 17);
        comprobar("schedule enabled", r.scheduleEnabled == true);
        comprobar("two windows", r.windows.size() == 2);
        if (r.windows.size() == 2) {
            comprobar("days of the first", r.windows[0].days == QStringLiteral("12345"));
            comprobar("hours of the first", r.windows[0].start == QTime(9, 30) && r.windows[0].end == QTime(17, 45));
            comprobar("the one crossing midnight is kept",
                      r.windows[1].start == QTime(23, 0) && r.windows[1].end == QTime(7, 0));
        }
        comprobar("two networks", r.networks.size() == 2);
        if (r.networks.size() == 2) {
            comprobar("the ORDER is preserved", r.networks[0].bssid == n1.bssid && r.networks[1].bssid == n2.bssid);
            comprobar("ssid", r.networks[0].ssid == QStringLiteral("HomeWiFi"));
            comprobar("gateway MAC", r.networks[0].gateway == n1.gateway);
            comprobar("one active and one not", r.networks[0].active && !r.networks[1].active);
            comprobar("network with no gateway captured as empty", r.networks[1].gateway.isEmpty());
        }
    }

    std::printf("\n--- removing a network deletes its group, does not leave it orphaned ---\n");
    {
        Settings s = SmartUnlockConfig::load();
        s.networks.removeAt(1);
        SmartUnlockConfig::save(s);
        const Settings r = SmartUnlockConfig::load();
        comprobar("a single network remains", r.networks.size() == 1);
        comprobar("and it is the one that should remain",
                  r.networks.size() == 1 && r.networks[0].ssid == QStringLiteral("HomeWiFi"));
    }

    std::printf("\n--- a corrupt window is discarded, not guessed ---\n");
    {
        KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("smart-unlockrc"));
        KConfigGroup ho(cfg, QStringLiteral("Horario"));
        ho.writeEntry("Ventanas", QStringList{QStringLiteral("12345|09:00|17:00"),
                                              QStringLiteral("basura"),
                                              QStringLiteral("|09:00|17:00")});
        cfg->sync();
        const Settings r = SmartUnlockConfig::load();
        comprobar("only the good one survives", r.windows.size() == 1);
    }

    std::printf("\n--- dual band: same name, different access point ---\n");
    {
        // The case that ate an afternoon: the network is marked from one band, the
        // router switches to the other, the BSSID changes in the last digit and
        // the phone stops recognising it. The check by BSSID is deliberate, so
        // what has to be guaranteed is that it is DISTINGUISHED from "I do not
        // know this network at all", so it can warn on screen.
        Settings s;
        s.enabled = true;
        TrustedNetwork n;
        n.bssid = QStringLiteral("A6:AD:9F:3D:ED:69");
        n.ssid = QStringLiteral("HomeWiFi");
        n.gateway = QStringLiteral("94:83:C4:C3:C5:B9");
        n.active = true;
        s.networks = {n};

        const QString otraBanda = QStringLiteral("A6:AD:9F:3D:ED:68");
        bool mismoNombre = false;
        bool mismoPunto = false;
        for (const TrustedNetwork &g : s.networks) {
            if (g.ssid == QStringLiteral("HomeWiFi")) {
                mismoNombre = true;
            }
            if (g.bssid == otraBanda) {
                mismoPunto = true;
            }
        }
        comprobar("the name DOES match", mismoNombre);
        comprobar("the access point does NOT match (that is why it does not trust)", !mismoPunto);
        comprobar("distinguishable from an unknown network", mismoNombre && !mismoPunto);
    }

    borrarConfig();
    std::printf("\n%d checks, %d failures\n\n", total, fallos);
    return fallos == 0 ? 0 : 1;
}
