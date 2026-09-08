# Driving the phone from the laptop

Testing this function means turning off the screen, turning it on and **checking
whether it asks for the PIN**. Done by hand, that is one message to a person per
attempt, and tuning this took dozens. With these two pieces the whole cycle runs
from here.

```sh
./movil estado                   # lock, screen, daemon, trust
./movil captura foto.png         # and you bring it over to look at it
./movil pulsar     --pin NNNN    # the power button
./movil deslizar   --pin NNNN    # opens the lock screen
./movil desbloquear --pin NNNN   # slide + type
./movil ciclo      --pin NNNN    # unlocks, powers off, powers on and says whether it asked for the PIN
```

## The PIN

**It is not written anywhere, and it must not be.** It arrives as `--pin` or in
`SMART_UNLOCK_PIN`, is used and forgotten. An argument is visible in `ps` while
the command runs, so on a shared machine the variable is better:

```sh
SMART_UNLOCK_PIN=NNNN ./movil ciclo
```

## Check first whether someone has the phone in hand

These are **real** presses and slides. Firing them while someone is reading
scrolls the page for them and turns off the screen under their finger — it
happened. `movil estado` touches nothing and says whether the screen is on; on
and unlocked usually means it is in use.

## Two traps that are already solved here

**`pgrep -f kscreenlocker_greet` finds itself.** That text travels in the command
line of the very ssh that runs it, so pgrep always hits and the phone reports
"locked" even when it is not — which makes `ciclo` say *ASKS FOR THE PIN* always,
no matter what. That is why the checks use `[k]`:
`pgrep -f "[k]screenlocker_greet"`.

**The screen turns off by itself between one command and the next.** A capture
taken right after turning it on can come out blank simply because the idle
timeout expired while `spectacle` was starting.

## How it works

- **The button** is pressed by writing to `/dev/input/event0`, the real
  `pm8941_pwrkey`: the same gesture a person makes.
- **The finger is a new device.** Writing to the real touch panel does not work:
  the compositor ignores those events. You have to create a touchscreen with
  `uinput`, which Wayland does treat as a finger.
- **The keyboard, likewise**, because the mobile lock screen opens with a slide
  and only then accepts digits.
- **The captures** come from `spectacle`, borrowing from `plasmashell` itself its
  `WAYLAND_DISPLAY` and its bus: without that environment it cannot even find a
  graphics platform.

A warning that saves some time: if the capture comes out **blank and weighs
10 KB**, the screen is off. It is not that the app is not drawing.
