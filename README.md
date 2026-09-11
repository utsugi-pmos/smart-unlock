# Smart Unlock

> It works and it is installed. It did not before: the first version put the phone
> into a **reboot loop** and it had to be rescued over SSH between one reboot and
> the next. What was learned is in "What went wrong", at the end — it is worth
> reading before touching this daemon.

Google's Smart Lock, before they removed it, made at home for the surya.

While the phone **trusts the moment**, it does not ask for the PIN: the screen
turns off and when you turn it on it goes straight back to the desktop. As soon as
it stops trusting, the normal lock is back.

There are **three ways to trust, combined with OR** — one being met is enough:

1. **Trusted network.** You are connected to a wifi you marked. It is recognised
   by the access point's **BSSID** (its MAC) **plus the gateway's MAC**, not by the
   network name.
2. **Grace after unlock.** You just entered the PIN; for the next *X* minutes it
   does not ask again.
3. **Schedule.** The clock falls within a window you defined (by day of the week,
   and it can cross midnight).

It is configured from its **own app** (in the drawer) and from
**Preferences > Personalization**. The two are frames over the same backend, like
appsvivas.

## Installing on a freshly built phone

It appears in `surya-setup` under **extras**, unchecked. You check it and that is
it: the auto-build detects it (it has sources in `ajustes/smart-unlock.d` and a
recipe in `pmaports/temp/smart-unlock`), compiles it, takes it to the phone and
runs the setting, which installs it and enables the daemon — **off**.

`surya-setup` tries it first with the cross compiler and, if that fails, **retries
inside QEMU** (`--no-cross`). The retry is not decorative: as soon as the local
pmaports and the repository drift apart by one gcc version, crossdirect is left
without `liblto_plugin.so` and **no** in-house package compiles —verified, it also
took down appsvivas, which had been installed for months—. Without that second
attempt, a freshly installed phone is left without the in-house apps and all you
see is a red line scrolling past.

By hand, if needed:

```sh
surya/pmaports/sincronizar smart-unlock
pmbootstrap --no-cross build smart-unlock --arch aarch64
SMART_UNLOCK_APK=/ruta/a/smart-unlock-1.0-r0.apk surya-setup smart-unlock
```

## It never comes activated

It is installed **off**, always. With no configuration file —the state of a new
installation— `Enabled` is `false` and the phone locks exactly like before the
package existed. Turning it on is a conscious decision by the user, not something
that happens to them by installing something.

## How "don't ask for the PIN" is done

Not by faking an unlock: the kscreenlocker greeter goes through PAM and there is
no honest way to bypass it from outside. Instead, the lock is prevented from ever
engaging. And for that you have to disarm **three** locks, because on this phone
there are three distinct paths to the greeter:

    powerdevilrc     [<perfil>][Display] LockBeforeTurnOffDisplay
    kscreenlockerrc  [Daemon] Autolock
    kscreenlockerrc  [Daemon] LockOnResume

**The first is the one that actually locks you here**, and the one that cost the
most to find. PowerDevil locks the screen *before* turning it off, and on a phone
it does so by default. From its own code (Plasma/6.7,
`daemon/actions/bundled/dpms.cpp`):

```cpp
if (m_lockBeforeTurnOff && (type == u"TurnOff" || type == u"ToggleOnOff")) {
    lockScreen();
}
```

```cpp
bool ProfileDefaults::defaultLockBeforeTurnOffDisplay(bool isMobile)
{
    return isMobile;          // on a phone: true
}
```

Notice that the condition fires with `TurnOff` **and** with `ToggleOnOff`: that is
why **no** configuration of the button's action avoids the lock, and testing
values there is a waste of time. The key is `LockBeforeTurnOffDisplay`, in the
`Display` subgroup of each power profile.

The other two are still needed: `Autolock` is the idle lock and `LockOnResume` the
resume-from-suspend one —the surya suspends when it turns off the screen—, so a
well-behaved button would not save the phone from locking by either of those two
paths.

With the three disarmed the screen **still turns off** on idle or on pressing power
(the battery does not suffer), it simply does not raise the greeter, so on turning
it back on you land on the desktop.

On losing trust, `LockBeforeTurnOffDisplay` is **deleted** —returning PowerDevil to
its factory value, which is to lock— and the other two go back to `true`.

**PowerDevil has to be restarted, warning it is not enough.**
`reparseConfiguration()` accepts the call and keeps using the old value: measured,
with the key already written to disk the next press still locked. The daemon
restarts `plasma-powerdevil.service` via systemd, which takes less than a second
and only happens when trust changes.

The `smart-unlockd` daemon watches the network (NetworkManager, over the system
bus) and the lock state (`org.freedesktop.ScreenSaver`, over the session one),
recomputes trust on every change and on a slow tick, and writes those keys
accordingly. **If the daemon is not running, nothing happens and the phone locks
as always** — the safe failure is the absence of the function.

## Security, without frills

A "trusted network" is worth what a MAC address is worth, and a MAC can be cloned:
someone can set up an access point with the same SSID **and** the same BSSID. That
is why a second factor is stored, the gateway's MAC read from the ARP table while
you are really connected: faking both at once, on the same subnet, is already quite
a bit more than renaming a hotspot. Even so **it is a convenience, not a safe** —
Google's Smart Lock carried the same hole.

If when adding the network the ARP did not yet have the gateway's MAC, that entry
stays "BSSID only" (flagged in the interface) and trust is by BSSID alone.

### "I don't know it yet" is not "it doesn't match"

The gateway's MAC is read from `/proc/net/arp`, and there it **takes a few seconds
to appear** after each network change: the entry does not exist, or exists as
`00:00:00:00:00:00`. The first version read that gap as a different MAC, i.e. as
"this is not your network", and stopped trusting. Seconds later the ARP populated
and it trusted again.

Measured over a whole day on the phone: **15 of the 33 trust changes** were not
changes of anything, but that gap.

And they did not come for free. Each trust change rewrites `powerdevilrc` and
**restarts PowerDevil** (see `setLockBeforeScreenOff`), and a freshly started
PowerDevil re-registers its idle timers — i.e. the countdown to turn off the screen
starts from zero. That day there were **37 restarts**, and in 24 of the 36 gaps
between one and the next not even the 10 minutes the screen needs to turn off went
by. The symptom you saw was a phone that stayed with the screen on, which looks
nothing like its cause.

So the check now has **three answers** instead of two, and an unresolved gateway is
`Unknown`: the previous verdict is kept and it is looked at again after 2 s. The
wait is **capped at 30 s** on purpose — this is the second factor against a cloned
BSSID, and whoever can answer the beacon can also refrain from answering the ARP—.
Past that deadline, "I don't know" becomes "no", which is the direction that locks
the phone.

The 30 s are measured. A normal wifi reconnection does not even come through here
(the gateway is still in the neighbour cache), and the worst case —the table
emptied by hand— took **15.3 s** on this phone:

    network: gateway not resolved yet -- holding trust = true for up to 30 s
    network: gateway resolved after 15311 ms

The same test before the fix gave two trust changes and **two PowerDevil
restarts**; now it gives zero.

## How it knows if the screen was already off

The re-lock has two halves. Putting `Autolock=true` back covers the **next**
screen-off by itself. The other half —"…or right now if it is off"— is the case
where the screen was **already** off when trust is lost: there is no future
screen-off to hook into, so it has to lock immediately.

The natural thing would be to ask the locker how long the session has been idle.
**On this phone that does not exist**, measured on the device:

    $ qdbus6 org.freedesktop.ScreenSaver /ScreenSaver \
          org.freedesktop.ScreenSaver.GetSessionIdleTime
    Error: org.freedesktop.DBus.Error.NotSupported
    GetSessionIdleTime is not supported on this platform

A daemon relying on that call would **always** fall into its safe-failure branch
and lock the screen in your face, mid-press. So the question is asked of the panel,
which does answer:

    /sys/class/backlight/<panel>/bl_power           0 = on, 4 = off
    /sys/class/backlight/<panel>/actual_brightness  0 = illuminates nothing

Both are read because there are panels that never move `bl_power` and just drop the
brightness to zero; if either of the two says "dark", it is off. The idle call is
still tried **first**, so that a platform that does implement it uses the better
signal.

If neither of the two answers, the daemon **fails closed**: it locks. The
alternative would be a phone that silently stopped locking.

### Side effect, stated plainly

On re-arming, the daemon writes `Autolock=true` and `LockOnResume=true`, which are
KDE's default values — not whatever was there before. If someone had deliberately
disabled either of the two by hand, this function will re-enable it the first time
it loses trust. Saving and restoring the original state would be possible, but it
means remembering across boots a preference almost no one changes; the predictable
behaviour is preferred: when it does not trust, the lock is KDE's normal one.

Detail observed in passing: when the screen turns off, the surya enters suspend and
drops even the SSH session. While it sleeps the daemon does not run either, so it
recomputes on waking.

## The PIN once per session: this is how it should work

On boot, Plasma Mobile leaves the screen locked — `LockOnStart=true` in
`/etc/xdg/kscreenlockerrc`. You enter the PIN once and, while it trusts, it does
not ask again.

That **is not a limitation to work around, it is the design**. If the phone booted
unlocked, rebooting it would be the way to skip the PIN: just remove the battery or
force a power-off to get in without knowing anything. Trust says "this moment is
safe", not "this device is yours"; the latter is proven by the PIN, and it has to
be proven at least once per session. Google's Smart Lock worked the same way, and
for the same reason: after a reboot it always asked for the pattern.

So `LockOnStart` is not touched. The daemon also does not try to dismiss a lock
that is already up, and it could not even if it wanted to — verified on the device,
the locker refuses to be deactivated without authenticating:

    $ qdbus6 org.freedesktop.ScreenSaver /ScreenSaver \
          org.freedesktop.ScreenSaver.SetActive false
    false          # and GetActive keeps returning true

The same goes for coming back home with the phone already locked in your pocket:
the first time it asks for the PIN, and after that it does not. In continuous use
inside the trusted network it never gets to lock, so it never asks.

### A symptom that misleads

If the phone has rebooted —because of the battery, say— and no one has entered the
PIN since, turning on the screen shows **the boot greeter**, not a new lock. The
function looks like it is doing nothing when in fact it has not yet gotten to
intervene. It is told apart like this:

    journalctl --user -u smart-unlockd -f     # what the daemon decides
    test/remoto/movil estado                  # lock, screen and trust

## Files

    src/config.{h,cpp}    on-disk schema (~/.config/smart-unlockrc) + schedule
    src/netinfo.{h,cpp}   BSSID and gateway MAC from NetworkManager
    backend.{h,cpp}       editable model, shared by KCM and app
    kcm.cpp               the Preferences module frame
    main.cpp + App.qml    the standalone app
    ui/main.qml           the KCM frame
    ui/ConfigView.qml     the settings screen, shared by the two
    daemon/main.cpp       smart-unlockd: watches, decides, arms/disarms the lock
    smart-unlockd.service   the user unit

## Compiling and testing on the surya itself

Same as app-usage: the phone already ships cmake, g++ and the needed Qt6, it is
aarch64, and neither pmbootstrap nor a Qt on the laptop is needed.

    ssh surya
    cd .../desbloqueo.d
    cmake -S . -B build -G Ninja && cmake --build build
    ./build/desbloqueo      # the settings screen, without installing
    ./build/smart-unlockd     # the daemon, in the foreground, to watch the journal

Packaging (from the repository, on the laptop):

    surya/pmaports/sincronizar smart-unlock
    pmbootstrap build smart-unlock --arch aarch64
    SMART_UNLOCK_APK=.../smart-unlock-1.0-r0.apk surya-setup smart-unlock

Or simpler: select it in `surya-setup`, which compiles it on its own.

## Format of ~/.config/smart-unlockrc

    [General]
    Enabled=true

    [Grace]
    AfterUnlock=true
    Minutes=5

    [Schedule]
    Enabled=true
    Windows=12345|23:00|07:00,67|10:00|23:59   # days|from|to, days 1=Mon..7=Sun

    [Networks]
    Order=AA:BB:CC:DD:EE:FF                    # order and presence

    [Network AA:BB:CC:DD:EE:FF]
    Ssid=MyHouse
    Gateway=11:22:33:44:55:66                  # empty => BSSID only
    Active=true

Until 1.0 these names were Spanish (`Activado`, `Gracia`, `Horario`,
`Ventanas`, `Redes`, `Red <BSSID>`). A file written by 1.0 is still read, and
the first save rewrites it with the names above.

## Removing it

    systemctl --user disable --now smart-unlockd.service
    sudo apk del smart-unlock

On stopping the service `Autolock=true` is set back (the daemon itself does it on
exit, and the unit's `ExecStopPost` as a backup), so removing it never leaves the
phone without a lock.

## What went wrong

The first installed version left the phone booting and rebooting every 13-28
seconds. From the journal of a failed boot:

```
19:43:08  Started Smart Unlock: el motor que decide si pedir el PIN
19:43:12  (end of that boot)
```

Four seconds. Three defects that added up, and none was visible without testing on
the device:

1. **`configure` on the locker on every recompute.** `setAutolock()` asked
   kscreenlocker to reload its configuration even when the value had not changed —
   i.e. always, because the normal case is "function stopped, `Autolock` already
   `true`". Doing it while the session boots takes it down, and the kernel patch
   `0055-el-watchdog-vigila-tambien-el-arranque` turns a boot that does not finish
   into a reboot. Now a value that does not change touches nothing.

2. **`QDBusInterface` blocks.** Constructing one introspects the remote object
   synchronously, with D-Bus's default timeout: **25 seconds**, of the same order as
   the measured reboot cycle. Now it is `QDBusMessage` without introspection, with
   2 s to the locker and 3 s to NetworkManager.

3. **All of that, in the constructor.** The daemon is launched by
   `graphical-session.target`, i.e. **while** the session boots. Now the constructor
   only reads a file and arms timers; the first decision waits a minute.

And once it no longer took the phone down, it still did not work, because of two
more:

4. **`QFile::atEnd()` lies on `/proc`.** The `/proc` files say they measure 0, and
   `atEnd()` answers from the size: the loop that read `/proc/net/arp` ran **zero
   times**, the gateway MAC always came out empty, and a trusted network could never
   match its second factor. It is read with `readAll()`.

5. **The settings-file watcher watched nothing.** `addPath()` fails on a file that
   does not yet exist, which is exactly the state of a new installation. And the tick
   recomputed without rereading the settings, so a missed notification left the
   daemon with old configuration forever. Now the directory is watched too and the
   tick rereads.

### The lesson that holds for the rest of the surya

A daemon that decides silently is undebuggable: the symptom of "the logic said no"
and that of "the daemon never started" are identical from outside. The five faults
were found in minutes **as soon as the daemon said what it was deciding and why**.
That is why it now talks, and why its log category is at `QtInfoMsg` and not the
`warning` Qt sets by default.

And before installing something here that touches the graphical session: **the USB
cable plugged in and an SSH rescue loop armed**. If it takes down the session, the
watchdog reboots and there is no screen left to fix it from; the only way is to
slip in over SSH in the ~25 s window between reboots. The `boot` partition carries
U-Boot, not the kernel, so via fastboot it cannot be fixed without reinstalling and
losing the data.
