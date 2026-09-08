# Drives the phone's input devices, so the lock/unlock cycle can be exercised
# without a finger on the glass. Runs ON the phone, as root.
#
# Why this exists: the only honest test of "does it still ask for the PIN" is
# pressing the power button, waking the screen and looking at it. Doing that by
# hand means asking a person for every iteration, and this feature took dozens.
#
#   inyectar.py power                 press the power key (blank / wake)
#   inyectar.py swipe [x y0 y1]       swipe up: opens the mobile lock screen
#   inyectar.py type <text>           type digits through a virtual keyboard
#   inyectar.py clear                 backspace a few times, to empty a field
#
# NO PASSWORD LIVES HERE. 'type' takes whatever it is given, and the caller
# decides where that came from.
import fcntl
import struct
import sys
import time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045567
UI_DEV_CREATE, UI_DEV_DESTROY = 0x5501, 0x5502
EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
BTN_TOUCH = 0x14A
ABS_X, ABS_Y = 0x00, 0x01
ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x2F, 0x35, 0x36, 0x39
KEY_POWER, KEY_ENTER, KEY_BACKSPACE = 116, 28, 14
DIGITS = {"0": 11, "1": 2, "2": 3, "3": 4, "4": 5, "5": 6, "6": 7, "7": 8, "8": 9, "9": 10}
WIDTH, HEIGHT = 1080, 2400


def emit(fd, etype, code, value):
    # 'i' and not 'I': tracking ids are released with -1, which is not unsigned.
    fd.write(struct.pack("llHHi", 0, 0, etype, code, value))


def press_power():
    # The real power key, straight into its own device: this is the gesture the
    # user makes, so it is the one worth reproducing.
    with open("/dev/input/event0", "wb") as fd:
        for value in (1, 0):
            emit(fd, EV_KEY, KEY_POWER, value)
            emit(fd, EV_SYN, 0, 0)
            fd.flush()
            time.sleep(0.12)


def new_uinput(name, keys=(), absaxes=False):
    fd = open("/dev/uinput", "wb")
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    for key in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, key)
    absmax = [0] * 64
    if absaxes:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
        for axis in (ABS_X, ABS_Y, ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID):
            fcntl.ioctl(fd, UI_SET_ABSBIT, axis)
        for axis, top in ((ABS_X, WIDTH), (ABS_MT_POSITION_X, WIDTH), (ABS_Y, HEIGHT),
                          (ABS_MT_POSITION_Y, HEIGHT), (ABS_MT_SLOT, 9), (ABS_MT_TRACKING_ID, 65535)):
            absmax[axis] = top
    dev = struct.pack("80sHHHHi", name, 3, 0x2222, 0x3333, 1, 0)
    dev += struct.pack("64i", *absmax) + b"\0" * (4 * 64 * 3)
    fd.write(dev)
    fd.flush()
    fcntl.ioctl(fd, UI_DEV_CREATE)
    # The compositor needs a moment to notice a device that just appeared;
    # without this the first events are delivered to nobody.
    time.sleep(1.2)
    return fd


def swipe(x, y0, y1):
    # A virtual touchscreen, NOT the real panel: events written straight to the
    # hardware device are ignored by the compositor, while a uinput device it
    # opened itself counts as a finger.
    fd = new_uinput(b"smart-unlock-touch", keys=(BTN_TOUCH,), absaxes=True)
    emit(fd, EV_ABS, ABS_MT_SLOT, 0)
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 1)
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x)
    emit(fd, EV_ABS, ABS_MT_POSITION_Y, y0)
    emit(fd, EV_ABS, ABS_X, x)
    emit(fd, EV_ABS, ABS_Y, y0)
    emit(fd, EV_KEY, BTN_TOUCH, 1)
    emit(fd, EV_SYN, 0, 0)
    fd.flush()
    steps = 20
    for i in range(1, steps + 1):
        y = int(y0 + (y1 - y0) * i / steps)
        emit(fd, EV_ABS, ABS_MT_SLOT, 0)
        emit(fd, EV_ABS, ABS_MT_POSITION_Y, y)
        emit(fd, EV_ABS, ABS_Y, y)
        emit(fd, EV_SYN, 0, 0)
        fd.flush()
        time.sleep(0.015)
    emit(fd, EV_ABS, ABS_MT_SLOT, 0)
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1)
    emit(fd, EV_KEY, BTN_TOUCH, 0)
    emit(fd, EV_SYN, 0, 0)
    fd.flush()
    time.sleep(0.8)
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    fd.close()


def tap(fd, code):
    for value in (1, 0):
        emit(fd, EV_KEY, code, value)
        emit(fd, EV_SYN, 0, 0)
        fd.flush()
        time.sleep(0.05)


def type_text(text, clear_first):
    fd = new_uinput(b"smart-unlock-keyboard", keys=list(DIGITS.values()) + [KEY_ENTER, KEY_BACKSPACE])
    if clear_first:
        # A field with leftovers from a previous attempt turns a correct PIN
        # into "Wrong PIN", which looks exactly like a broken test.
        for _ in range(12):
            tap(fd, KEY_BACKSPACE)
        time.sleep(0.3)
    for char in text:
        if char not in DIGITS:
            raise SystemExit(f"digits only: '{char}' is not valid")
        tap(fd, DIGITS[char])
        time.sleep(0.15)
    if text:
        time.sleep(0.3)
        tap(fd, KEY_ENTER)
    time.sleep(0.8)
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    fd.close()


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__ or "usage: inyectar.py power|swipe|type|clear")
    action = sys.argv[1]
    if action == "power":
        press_power()
    elif action == "swipe":
        args = [int(a) for a in sys.argv[2:5]] if len(sys.argv) >= 5 else [540, 2100, 700]
        swipe(*args)
    elif action == "type":
        type_text(sys.argv[2] if len(sys.argv) > 2 else "", clear_first=True)
    elif action == "clear":
        type_text("", clear_first=True)
    else:
        raise SystemExit(f"don't know what '{action}' is")
    print("ok")


main()
