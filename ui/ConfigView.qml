// SPDX-License-Identifier: GPL-2.0-or-later
//
// The whole settings screen, used by both front-ends: the Settings module and
// the standalone app. Everything arrives through `backend`, so this file knows
// nothing about which of the two is showing it. It is a plain ColumnLayout --
// both frames (SimpleKCM and ScrollablePage) provide the scrolling.
//
// Three ways to be trusted, OR'd together, each its own section: a Wi-Fi you
// are on, a while after you last unlocked, and a time of day. The master switch
// at the top gates all of them.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property QtObject backend

    spacing: Kirigami.Units.smallSpacing

    function twoDigits(n) {
        return (n < 10 ? "0" : "") + n
    }

    // What the feature is, and the one caveat that matters, said up front rather
    // than buried: a "trusted network" is only as trustworthy as a MAC address,
    // which can be cloned. It is a convenience, not a vault.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: true
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Information
        text: i18n("While it trusts the moment, the phone will not ask for the PIN: the screen turns off and comes straight back on. As soon as it stops trusting, it locks again. Careful: a trusted network is recognised by the router's MAC, which someone could clone; it is a convenience, not a safe.")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Enable trusted unlock")
            wrapMode: Text.WordWrap
        }

        QQC2.Switch {
            checked: root.backend.enabled
            onToggled: root.backend.setEnabled(checked)
        }
    }

    // Everything below the master switch is dimmed and inert when it is off, so
    // the toggles cannot be mistaken for active while the feature is disabled.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        enabled: root.backend.enabled
        opacity: enabled ? 1 : 0.5

        // ---------------------------------------------------------------- redes
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("Trusted networks")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: i18n("Do not ask for the PIN while you are connected to one of these networks. They are recognised by the router's BSSID and the gateway's MAC, not by the name.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.8
        }

        // The network you are on right now, offered by its real name so you
        // never have to type a MAC.
        Kirigami.AbstractCard {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Icon {
                        source: "network-wireless-symbolic"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: root.backend.currentConnected
                                ? i18n("You are on: %1", root.backend.currentSsid || i18n("(no name)"))
                                : i18n("You are not connected to any wifi")
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: root.backend.currentConnected
                            elide: Text.ElideRight
                            text: root.backend.currentBssid
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }

                        // Without this, the screen says "you are on HomeWiFi"
                        // above a trusted network with the same name, and it looks
                        // like everything is fine while the phone refuses to trust.
                        // The notice names the reason and what to do, which is to
                        // press the button down here.
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: root.backend.currentSsidSavedWithOtherBssid
                            wrapMode: Text.WordWrap
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.neutralTextColor
                            text: i18n("You have a saved network with this name, but with a different access point. Dual-band routers give one per band (2.4 and 5 GHz) and switch on their own, so you have to add this one too.")
                        }
                    }

                    QQC2.ToolButton {
                        icon.name: "view-refresh"
                        onClicked: root.backend.refreshCurrent()
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.backend.currentConnected && !root.backend.currentHasGateway
                    text: i18n("The gateway's MAC could not be read; this network would be trusted by its BSSID alone.")
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.neutralTextColor
                }

                QQC2.Button {
                    Layout.fillWidth: true
                    icon.name: "list-add"
                    visible: root.backend.currentConnected
                    enabled: !root.backend.currentAlreadyTrusted
                    text: root.backend.currentAlreadyTrusted
                        ? i18n("This network is already trusted")
                        : i18n("Trust this network")
                    onClicked: root.backend.trustCurrentNetwork()
                }
            }
        }

        Repeater {
            model: root.backend.networks

            delegate: QQC2.ItemDelegate {
                id: netRow

                required property var modelData

                Layout.fillWidth: true
                highlighted: false

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: netRow.modelData.ssid || i18n("(no name)")
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: netRow.modelData.bssid + " · " + (netRow.modelData.hasGateway
                                ? i18n("BSSID + gateway")
                                : i18n("BSSID only"))
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }
                    }

                    QQC2.Switch {
                        checked: netRow.modelData.active
                        onToggled: root.backend.setNetworkActive(netRow.modelData.bssid, checked)
                    }

                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        onClicked: root.backend.removeNetwork(netRow.modelData.bssid)
                    }
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            visible: root.backend.networks.length === 0
            text: i18n("No trusted network yet.")
            opacity: 0.6
        }

        // -------------------------------------------------------------- gracia
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("After unlocking with the PIN")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Do not ask for the PIN again for a while after unlocking")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.graceEnabled
                onToggled: root.backend.setGraceEnabled(checked)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing
            visible: root.backend.graceEnabled

            QQC2.Label {
                text: i18n("For")
            }

            QQC2.SpinBox {
                from: 1
                to: 24 * 60
                value: root.backend.graceMinutes
                onValueModified: root.backend.setGraceMinutes(value)
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("minutes")
            }
        }

        // ------------------------------------------------------------- horario
        Kirigami.ListSectionHeader {
            Layout.fillWidth: true
            text: i18n("Trusted schedule")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Do not ask for the PIN within certain time windows")
                wrapMode: Text.WordWrap
            }

            QQC2.Switch {
                checked: root.backend.scheduleEnabled
                onToggled: root.backend.setScheduleEnabled(checked)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: root.backend.scheduleEnabled

            Repeater {
                model: root.backend.windows

                delegate: QQC2.ItemDelegate {
                    id: winRow

                    required property var modelData

                    Layout.fillWidth: true
                    highlighted: false

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            source: "appointment-recurring"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: winRow.modelData.start + " – " + winRow.modelData.end
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: winRow.modelData.daysLabel
                                font: Kirigami.Theme.smallFont
                                opacity: 0.6
                            }
                        }

                        QQC2.ToolButton {
                            icon.name: "edit-delete"
                            onClicked: root.backend.removeWindow(winRow.modelData.index)
                        }
                    }
                }
            }

            QQC2.Button {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                icon.name: "list-add"
                text: i18n("Add window")
                onClicked: addDialog.open_it()
            }
        }
    }

    // The add-window form. A Dialog (popup), so it does not take a slot in the
    // ColumnLayout above.
    Kirigami.PromptDialog {
        id: addDialog

        title: i18n("New trusted window")
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel

        // A window with no days is not a window, and the backend drops it. If
        // OK stayed enabled, pressing it would close the dialog and silently do
        // nothing -- so it is greyed out until at least one day is picked, and
        // the reason is written underneath.
        readonly property bool anyDay: {
            for (var i = 1; i <= 7; i++) {
                if (days[i]) {
                    return true
                }
            }
            return false
        }

        // standardButton() is a method, so its result cannot be the target of a
        // binding -- the button has to be poked when the value changes, and
        // again on open, because the dialog rebuilds its buttons each time.
        function refreshOk() {
            const button = standardButton(QQC2.Dialog.Ok)
            if (button) {
                button.enabled = anyDay
            }
        }
        onAnyDayChanged: refreshOk()
        onOpened: refreshOk()

        // index 1..7 = Mon..Sun; index 0 is an unused filler so the digit is the
        // position.
        property var days: [false, false, false, false, false, false, false, false]

        function open_it() {
            days = [false, false, false, false, false, false, false, false]
            startHour.value = 23
            startMinute.value = 0
            endHour.value = 7
            endMinute.value = 0
            open()
        }

        onAccepted: {
            var d = ""
            for (var i = 1; i <= 7; i++) {
                if (days[i]) {
                    d += i
                }
            }
            root.backend.addWindow(d,
                root.twoDigits(startHour.value) + ":" + root.twoDigits(startMinute.value),
                root.twoDigits(endHour.value) + ":" + root.twoDigits(endMinute.value))
        }

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: i18n("Days")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: 7

                    delegate: QQC2.Button {
                        required property int index

                        Layout.fillWidth: true
                        checkable: true
                        checked: addDialog.days[index + 1]
                        text: ["M", "T", "W", "T", "F", "S", "S"][index]
                        onToggled: {
                            // Reassign the whole array so the binding above sees
                            // the change: mutating an element in place does not
                            // notify.
                            var a = addDialog.days.slice()
                            a[index + 1] = checked
                            addDialog.days = a
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                QQC2.Label { text: i18n("From") }
                QQC2.SpinBox { id: startHour; from: 0; to: 23 }
                QQC2.Label { text: ":" }
                QQC2.SpinBox { id: startMinute; from: 0; to: 59 }

                QQC2.Label { text: i18n("Until") }
                QQC2.SpinBox { id: endHour; from: 0; to: 23 }
                QQC2.Label { text: ":" }
                QQC2.SpinBox { id: endMinute; from: 0; to: 59 }
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: !addDialog.anyDay
                text: i18n("Pick at least one day.")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.neutralTextColor
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("If the end time is earlier than the start, the window crosses midnight (e.g. 23:00 – 07:00).")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
        }
    }
}
