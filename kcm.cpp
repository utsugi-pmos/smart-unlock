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
        // The Apply button belongs to the KCM frame, so the backend's dirty
        // flag drives it.
        connect(m_backend, &SmartUnlockBackend::dirtyChanged, this, [this] {
            setNeedsSave(m_backend->dirty());
        });
        setNeedsSave(m_backend->dirty());
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
