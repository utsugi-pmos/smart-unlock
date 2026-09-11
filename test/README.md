# Tests

Three suites, none needs the phone. To run them all:

```sh
surya/setup/settings/smart-unlock.d/test/run
```

Returns 0 if everything passes. Below is each one separately, in case you want to
compile only the one you are touching.

## Schedule (`test_schedule.cpp`)

`windowActiveAt()` is the only part of the trust logic that is pure —a
`ScheduleWindow` and a date go in, a `bool` comes out— and also the one with the
edges easiest to get wrong: which day "owns" the small hours of a window that
crosses midnight, and what happens when that previous day is Sunday.

It is tested **without the phone**, inside pmbootstrap's aarch64 chroot, which is
where the good Qt6 lives:

```sh
C=~/.local/var/pmbootstrap/chroot_buildroot_aarch64
sudo mkdir -p "$C/tmp/t"
sudo cp ../src/config.cpp ../src/config.h test_schedule.cpp "$C/tmp/t/"

pmbootstrap chroot -b aarch64 --add qt6-qtbase-dev,build-base,kconfig-dev -- sh -c '
  cd /tmp/t && g++ -std=c++20 -fPIC -I. \
    -I/usr/include/qt6 -I/usr/include/qt6/QtCore \
    -I/usr/include/KF6/KConfigCore -I/usr/include/KF6/KConfig \
    test_schedule.cpp config.cpp -lQt6Core -lKF6ConfigCore -o test && ./test'
```

Returns 0 if everything passes. Last time: **26 checks, 0 failures**.

What it covers, beyond the obvious:

- The start edge is in and the end edge is not (`[start, end)`).
- Window that crosses midnight: Friday 23:00-07:00 includes **Saturday** at
  03:00, because those small hours belong to Friday, which is the day you marked.
- And for that same reason Friday at 06:00 is **not** in: those small hours belong
  to Thursday, which is not marked.
- The week crossing Sunday → Monday, where the "previous day" falls outside the
  1..7 range and has to be wrapped.
- `start == end` is read as the whole day.

## ARP table (`test_arp.cpp`)

`arpLookup()` resolves the gateway's MAC, which is the **second factor** of a
trusted network. Throughout its whole first life it always returned an empty
string, because it read `/proc/net/arp` with a `readLine()`/`atEnd()` loop and
`/proc` files report a size of 0: `atEnd()` answers from the size, so the loop did
not run even once. No error, no warning, and the only symptom being that the
trusted network never matched.

The path to the table is a parameter (default `/proc/net/arp`) only so the parsing
can be tested with sample tables. It is compiled the same way as the other test,
swapping the sources:

```sh
sudo cp ../src/netinfo.cpp ../src/netinfo.h test_arp.cpp "$C/tmp/t/"
pmbootstrap chroot -b aarch64 -- sh -c '
  cd /tmp/t && g++ -std=c++20 -fPIC -I. \
    -I/usr/include/qt6 -I/usr/include/qt6/QtCore -I/usr/include/qt6/QtDBus \
    test_arp.cpp netinfo.cpp -lQt6Core -lQt6DBus -o testarp && ./testarp'
```

Last time: **8 checks, 0 failures**. It covers the parsing (uppercase, incomplete
entry `00:00:00:00:00:00`, missing IP, nonexistent file, the header line) and,
above all, **the regression**: it reads the real `/proc/net/arp` and demands
finding an entry that is there. If someone puts an `atEnd()` loop back in, that
check fails.

## Settings on disk (`test_config.cpp`)

That saving and reloading loses nothing, and above all **that a new installation
comes turned off**. That first check is not a courtesy: it is the requirement that
this function never activate on its own, written as a test instead of as a
promise. Without a settings file —the state of a freshly installed phone—
`Enabled` has to be `false`.

```sh
sudo cp ../src/config.cpp ../src/config.h test_config.cpp "$C/tmp/t/"
pmbootstrap chroot -b aarch64 -- sh -c '
  cd /tmp/t && g++ -std=c++20 -fPIC -I. \
    -I/usr/include/qt6 -I/usr/include/qt6/QtCore \
    -I/usr/include/KF6/KConfigCore -I/usr/include/KF6/KConfig \
    test_config.cpp config.cpp -lQt6Core -lKF6ConfigCore -o testcfg && ./testcfg'
```

Last time: **21 checks, 0 failures**. Besides the off-by-default, it covers the
full round trip (windows, including one that crosses midnight; networks with their
order, their gateway MAC and their active/inactive), that removing a network
deletes its group instead of leaving it orphaned, and that a corrupt window in the
file is discarded instead of guessed — a half-read window interpreted as "all day"
would be a lock nobody asked to open.
