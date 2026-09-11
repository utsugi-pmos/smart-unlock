// SPDX-License-Identifier: GPL-2.0-or-later
//
// Entry point of the standalone application. Deliberately thin: everything it
// does is in SmartUnlockBackend, the same object the Settings module uses. It
// exists because a KCM can stop loading after a Plasma update (kcm_lookandfeel
// on this very device is already in that state); this app needs only Qt and
// Kirigami, so the settings stay reachable.

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <KAboutData>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "backend.h"

int main(int argc, char *argv[])
{
    // QApplication, not QGuiApplication: Kirigami pulls in QtWidgets styles for
    // its dialogs, and with QGuiApplication they fall back to something that
    // does not match the rest of the phone.
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("smart-unlock"));

    KAboutData about(QStringLiteral("smart-unlock"),
                     i18n("Smart Unlock"),
                     QStringLiteral("1.0"),
                     i18n("Do not ask for the PIN on trusted networks or at trusted times"),
                     KAboutLicense::GPL_V2);
    KAboutData::setApplicationData(about);

    // Matches the .desktop file name, so the shell attaches the window to its
    // launcher icon instead of showing a generic one.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.smartunlock"));

    SmartUnlockBackend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("smartUnlockBackend"), &backend);

    engine.loadFromModule("SmartUnlock", "App");

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
