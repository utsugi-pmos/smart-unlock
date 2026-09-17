// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Settings module. All the logic is in SmartUnlockBackend, shared with the
// standalone application; this only wires it to the KCM frame so the two
// front-ends cannot drift apart.

#include "backend.h"

#include <KPluginFactory>
#include <KQuickConfigModule>

class KCMSmartUnlock : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(SmartUnlockBackend *backend READ backend CONSTANT)

public:
    KCMSmartUnlock(QObject *parent, const KPluginMetaData &data)
        : KQuickConfigModule(parent, data)
        , m_backend(new SmartUnlockBackend(this))
    {
        // Every change is saved the moment it is made, with no Apply button.
        // On the phone the frame's Apply sits at the very bottom of the page:
        // a second owner turned the switch on, saw nothing happen and never
        // found it (reported 2026-09-17). The daemon watches the file, so a
        // saved change is also an applied one.
        setButtons(Help);
        connect(m_backend, &SmartUnlockBackend::dirtyChanged, this, [this] {
            if (m_backend->dirty()) {
                m_backend->save();
            }
        });
    }

    SmartUnlockBackend *backend() const
    {
        return m_backend;
    }

    void load() override
    {
        m_backend->load();
    }

    void save() override
    {
        m_backend->save();
    }

    void defaults() override
    {
        m_backend->restoreDefaults();
    }

private:
    SmartUnlockBackend *const m_backend;
};

K_PLUGIN_CLASS_WITH_JSON(KCMSmartUnlock, "kcm_smart_unlock.json")

#include "kcm.moc"
