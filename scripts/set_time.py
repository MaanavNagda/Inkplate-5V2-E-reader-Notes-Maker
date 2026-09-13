#!/usr/bin/env python3
"""Set the Inkplate RTC to the host's local time via the SETTIME serial command.

Usage: python3 scripts/set_time.py [/dev/ttyUSB0]

The firmware's SETTIME command calls rtc.setEpoch(). The ESP32's localtime()
runs in UTC, so we send (true_epoch + local_utc_offset) so the RTC stores the
host's local civil time.
"""

import os
import select
import sys
import termios
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
BAUD = termios.B460800


def local_utc_offset_seconds() -> int:
    """Seconds east of UTC for the host's current local time (handles DST)."""
    lt = time.localtime()
    off = getattr(lt, "tm_gmtoff", None)
    if off is not None:
        return int(off)
    # Fallback: difference between localtime and gmtime.
    return int(time.mktime(lt) - time.mktime(time.gmtime()))


def main() -> int:
    fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag
        attrs[1] = 0  # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
        attrs[3] = 0  # lflag
        attrs[4] = BAUD  # ispeed
        attrs[5] = BAUD  # ospeed
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

        # Opening the port may reset the board; give it time to boot.
        time.sleep(3.0)

        epoch = int(time.time() + local_utc_offset_seconds())
        os.write(fd, ("SETTIME %d\n" % epoch).encode())

        out = b""
        end = time.time() + 3.0
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                chunk = os.read(fd, 256)
                if not chunk:
                    break
                out += chunk
        sys.stdout.write(out.decode(errors="replace"))
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
