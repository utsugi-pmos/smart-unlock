// Exercises SmartUnlockConfig::windowActiveAt, the one piece of trust logic that
// is pure enough to test off the phone -- and the one whose edges (a window that
// crosses midnight, and which day the small hours belong to) are easy to get
// wrong and impossible to eyeball.
#include "config.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <cstdio>

static int failures = 0;
static int total = 0;

// 2026-09-07 is a Monday, so dow 1..7 maps onto the 7th..13th.
static QDateTime when(int dow, const char *hhmm)
{
    return QDateTime(QDate(2026, 9, 6 + dow), QTime::fromString(QLatin1String(hhmm), QStringLiteral("HH:mm")));
}

static void check(const char *name, const QString &days, const char *from, const char *to,
                      int dow, const char *hhmm, bool expected)
{
    ScheduleWindow w;
    w.days = days;
    w.start = QTime::fromString(QLatin1String(from), QStringLiteral("HH:mm"));
    w.end = QTime::fromString(QLatin1String(to), QStringLiteral("HH:mm"));

    const bool real = SmartUnlockConfig::windowActiveAt(w, when(dow, hhmm));
    ++total;
    if (real != expected) {
        ++failures;
        std::printf("  FAIL   %-52s days=%s %s-%s  dow=%d %s -> %s (expected %s)\n",
                    name, qPrintable(days), from, to, dow, hhmm,
                    real ? "true" : "false", expected ? "true" : "false");
    } else {
        std::printf("  ok     %-52s\n", name);
    }
}

int main()
{
    std::printf("\n--- normal window, same day (Mon-Fri 09:00-17:00) ---\n");
    const QString LV = QStringLiteral("12345");
    check("inside", LV, "09:00", "17:00", 3, "12:00", true);
    check("right at the start (inclusive)", LV, "09:00", "17:00", 3, "09:00", true);
    check("one minute before", LV, "09:00", "17:00", 3, "08:59", false);
    check("right at the end (exclusive)", LV, "09:00", "17:00", 3, "17:00", false);
    check("one minute before the end", LV, "09:00", "17:00", 3, "16:59", true);
    check("Saturday, day not marked", LV, "09:00", "17:00", 6, "12:00", false);

    std::printf("\n--- crosses midnight (Friday nights: Fri 23:00-07:00) ---\n");
    const QString V = QStringLiteral("5");
    check("Friday 23:30, already inside", V, "23:00", "07:00", 5, "23:30", true);
    check("Friday 22:59, not yet", V, "23:00", "07:00", 5, "22:59", false);
    check("SATURDAY 03:00 = Friday small hours", V, "23:00", "07:00", 6, "03:00", true);
    check("Saturday 07:00, already out (exclusive)", V, "23:00", "07:00", 6, "07:00", false);
    check("Saturday 23:30 NO (Saturday is not marked)", V, "23:00", "07:00", 6, "23:30", false);
    check("Friday 06:00 = THURSDAY small hours, not marked", V, "23:00", "07:00", 5, "06:00", false);

    std::printf("\n--- week crossing: Sunday -> Monday ---\n");
    const QString D = QStringLiteral("7");
    check("Sunday 23:30 inside", D, "23:00", "07:00", 7, "23:30", true);
    check("MONDAY 03:00 = Sunday small hours", D, "23:00", "07:00", 1, "03:00", true);
    const QString L = QStringLiteral("1");
    check("Monday marked: Sunday 23:30 NO", L, "23:00", "07:00", 7, "23:30", false);
    check("Monday marked: Monday 23:30 YES", L, "23:00", "07:00", 1, "23:30", true);

    std::printf("\n--- start == end: the whole day ---\n");
    check("Wednesday at noon", QStringLiteral("3"), "00:00", "00:00", 3, "12:00", true);
    check("Wednesday in the small hours", QStringLiteral("3"), "00:00", "00:00", 3, "03:00", true);
    check("Thursday, not marked", QStringLiteral("3"), "00:00", "00:00", 4, "12:00", false);

    std::printf("\n--- every day ---\n");
    const QString ALL = QStringLiteral("1234567");
    for (int d = 1; d <= 7; ++d) {
        check("any day within 22:00-23:00", ALL, "22:00", "23:00", d, "22:30", true);
    }

    std::printf("\n%d checks, %d failures\n\n", total, failures);
    return failures == 0 ? 0 : 1;
}
