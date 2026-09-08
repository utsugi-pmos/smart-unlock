// SPDX-License-Identifier: GPL-2.0-or-later
//
// The standalone application. Same content as the Settings module, in a plain
// Kirigami window that depends only on Qt and Kirigami, so it keeps working if
// a Plasma update ever leaves the KCM unable to load. The one thing it adds is
// an Apply button, because there is no KCM frame providing one.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: i18n("Smart Unlock")

    width: Kirigami.Units.gridUnit * 26
    height: Kirigami.Units.gridUnit * 46

    pageStack.initialPage: Kirigami.ScrollablePage {
        id: page

        title: i18n("Smart Unlock")

        // Kirigami.ScrollablePage takes a single flickable child and scrolls it.
        ConfigView {
            backend: smartUnlockBackend
        }

        // Only shown once something changed: a permanently visible button eats
        // a row of a phone screen for nothing.
        footer: QQC2.ToolBar {
            visible: smartUnlockBackend.dirty
            height: visible ? implicitHeight : 0

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("There are unapplied changes")
                    elide: Text.ElideRight
                }

                QQC2.Button {
                    text: i18n("Discard")
                    icon.name: "dialog-cancel"
                    onClicked: smartUnlockBackend.load()
                }

                QQC2.Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: smartUnlockBackend.save()
                }
            }
        }
    }
}
