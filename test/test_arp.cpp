// Exercises NetInfo::arpLookup, the second factor of a trusted network -- and
// the function that silently returned nothing for its whole life because of how
// Qt reads files under /proc.
#include "netinfo.h"

#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
static int total = 0;

static void check(const char *name, const QString &got, const QString &expected)
{
    ++total;
    if (got != expected) {
        ++failures;
        std::printf("  FAIL   %-46s -> \"%s\" (expected \"%s\")\n",
                    name, qPrintable(got), qPrintable(expected));
    } else {
        std::printf("  ok     %-46s\n", name);
    }
}

int main()
{
    QTemporaryDir dir;
    const QString tabla = dir.filePath(QStringLiteral("arp"));
    {
        QFile f(tabla);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("IP address       HW type     Flags       HW address            Mask     Device\n"
                "192.168.2.1      0x1         0x2         94:83:c4:c3:c5:b9     *        wlan0\n"
                "172.16.42.2      0x1         0x2         fe:ba:7a:af:f2:4a     *        usb0\n"
                "192.168.2.77     0x1         0x0         00:00:00:00:00:00     *        wlan0\n");
    }

    std::printf("\n--- parsing the ARP table ---\n");
    check("the gateway, in uppercase",
              NetInfo::arpLookup(QStringLiteral("192.168.2.1"), tabla),
              QStringLiteral("94:83:C4:C3:C5:B9"));
    check("another entry from the same table",
              NetInfo::arpLookup(QStringLiteral("172.16.42.2"), tabla),
              QStringLiteral("FE:BA:7A:AF:F2:4A"));
    check("IP that is not there -> empty",
              NetInfo::arpLookup(QStringLiteral("10.0.0.1"), tabla), QString());
    check("incomplete entry (00:00:..) counts as unknown",
              NetInfo::arpLookup(QStringLiteral("192.168.2.77"), tabla), QString());
    check("empty IP -> empty", NetInfo::arpLookup(QString(), tabla), QString());
    check("file that does not exist -> empty",
              NetInfo::arpLookup(QStringLiteral("192.168.2.1"), dir.filePath(QStringLiteral("missing"))),
              QString());
    check("the header is not mistaken for an entry",
              NetInfo::arpLookup(QStringLiteral("IP"), tabla), QString());

    // The regression that matters. Files under /proc report a size of zero, and
    // QFile::atEnd() answers from the size -- a readLine()/atEnd() loop over
    // this file runs zero times and the function returns nothing, always. This
    // reads the real thing: if the table has any usable entry, we must find it.
    std::printf("\n--- against the real /proc/net/arp (the regression) ---\n");
    QFile real(QStringLiteral("/proc/net/arp"));
    if (real.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QList<QByteArray> lines = real.readAll().split('\n');
        QString ipConMac;
        for (const QByteArray &l : lines) {
            const QList<QByteArray> cols = l.simplified().split(' ');
            if (cols.size() >= 4 && cols.at(0).contains('.')
                && cols.at(3) != "00:00:00:00:00:00") {
                ipConMac = QString::fromUtf8(cols.at(0));
                break;
            }
        }
        if (ipConMac.isEmpty()) {
            std::printf("  (skipped: the system ARP table has no complete entries)\n");
        } else {
            ++total;
            const QString mac = NetInfo::arpLookup(ipConMac);
            if (mac.isEmpty()) {
                ++failures;
                std::printf("  FAIL   %s is in /proc/net/arp and arpLookup does not find it\n",
                            qPrintable(ipConMac));
            } else {
                std::printf("  ok     %s -> %s (read from /proc, size 0)\n",
                            qPrintable(ipConMac), qPrintable(mac));
            }
        }
    } else {
        std::printf("  (skipped: no /proc/net/arp)\n");
    }

    std::printf("\n%d checks, %d failures\n\n", total, failures);
    return failures == 0 ? 0 : 1;
}
