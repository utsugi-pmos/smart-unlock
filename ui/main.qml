// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Settings module front-end. The content is ConfigView.qml, shared with the
// standalone app; this only wraps it in the KCM frame, whose Apply button is
// driven by the backend's dirty flag from kcm.cpp.

import QtQuick

import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    // SimpleKCM already scrolls its content, so ConfigView is a plain column.
    ConfigView {
        backend: kcm.backend
    }
}
