# How the watch protocol was investigated

The DW-H5600 has no public protocol documentation and is not supported by the open-source projects that exist for other Casio watches, to the author knowledge. This document describes the method, what was verified on real hardware, and the projects that made it possible. Thanks to their authors.

## 1. Credits

| Project | Author / host | Licence | What it contributed |
|---|---|---|---|
| Gadgetbridge | Gadgetbridge contributors, Codeberg | AGPL-3.0-or-later | Public knowledge of the Casio "2C/2D" GATT scheme used by other Casio models (feature requests on `26eb002c`, answers on `26eb002d`, notifications on `26eb0030`), the time feature and the notification frame layout. Source reviewed at commit `762c844c` (2026-10-24). |
| Rockpool | Andrew Branson (abranson) | see project | Reference for monitoring other applications' notifications on Sailfish OS with D-Bus `BecomeMonitor`, and for the need to run unsandboxed. |
| Amazfish | Adam Pigg (piggz) | see project | The same technique in a second Sailfish OS application. |
| libwatchfish | Adam Pigg (piggz) | see project | Shared Sailfish library for notification monitoring. |

CWS contains **no code copied from these projects**; they were used as documentation of behaviour and technique. Every protocol detail used by CWS was verified on the watch (section 2).

## 2. Method

1. **Survey of public knowledge.** The Casio support in Gadgetbridge was read to learn which GATT layout and feature identifiers the "2C/2D" family uses and how frames are built. DW-H5600 itself is not supported upstream, so every item was a hypothesis.
2. **GATT dump on the real watch.** With a Python script over the BlueZ D-Bus API on a Jolla phone (Sailfish OS 5.2, BlueZ 5.87), the watch's services and characteristics were listed and compared with the hypotheses. The watch exposes the `26eb000d` service with `26eb0023`, `0024`, `002c`, `002d` and `0030`, matching the "2C/2D" family; the older characteristics are absent.
3. **Feature queries.** Single-byte requests on `26eb002c` with answers on `26eb002d`: the name (`0x23`) and version (`0x20`) features answered as expected; the current-time feature (`0x09`) cannot be read (`ff 81 09`); an unknown feature (`0x7e`) answers `ff 81 7e`, so `ff 81 <id>` means "unknown or not readable".
4. **Time write.** An 11-byte frame on feature `0x09` was written with known values, including a deliberate +1 h test, and the result was read from the watch display. The watch takes the fields as its displayed local time; there is no time zone in the frame.
5. **Failure modes.** Deliberately malformed frames (short, long, month 13, day 32, hour 25, minute 60, day-of-week 9) showed that the watch silently discards invalid time frames and sends no error, so the application validates frames itself.
6. **Notification channel.** Frames on `26eb0030` (all bytes inverted) were sent and observed on the watch: pop-up, vibration and sound (unless disabled on the watch), a 600 ms spacing between writes, a frame limit of 214 bytes, and the fact that each new connection empties the watch's notification list.
7. **Sailfish OS side.** Notification monitoring through D-Bus and the behaviour of background wake-ups and the notification service were tested on the phone.

## 3. What is still open

- Only the DW-H5600 was tested. Other models of the same family are expected to behave similarly but are unverified.

## 4. Reproducing

`tests/watchlink-cli` and `tests/notifmonitor-cli` are command-line tools for repeating the session and notification checks on a phone.
