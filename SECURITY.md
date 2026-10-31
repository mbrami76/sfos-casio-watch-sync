# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private reporting instead: open the **Security** tab of the repository and choose **Report a vulnerability**. Include the app version, the Sailfish OS version, the watch model and the steps to reproduce.

This is a volunteer project: reports are handled on a best-effort basis, and a fix is published as a new signed release. Credit is given in the release notes if you want it.

## What the application can see and do

Knowing this helps to judge what counts as a vulnerability:

- The application is **not sandboxed** (`Sandboxing=Disabled`), because reading other applications' notifications needs a monitor on the session D-Bus. It runs with the permissions of the user.
- When notification forwarding is switched on, it reads **all notifications** posted on the phone. Only the notifications of applications the user enabled are sent, and only to the paired watch over Bluetooth LE. The text is not logged or stored beyond a short in-memory queue (10 items, dropped after 5 minutes).
- The application makes **no network connections**.
- Settings and state are plain files in the user's home directory (`~/.config/it.michelebrami/` and `~/.local/share/it.michelebrami/`). The state file contains the Bluetooth addresses of the paired watches.
- The background service exposes a D-Bus API on the **session bus** (`it.michelebrami.sfos_casio_sync`). Any process of the same user can call it, which is the normal trust boundary of the session bus.

## In scope

- Notification text or watch addresses leaving the phone, or being written to disk or logs, contrary to the above.
- Ways for another application or user to read forwarded notification text through the application.
- Memory-safety or parsing errors in the notification and frame handling that a malicious notification could trigger.
- Problems in the packaging (file permissions, scriptlets) or in the release signatures.

## Out of scope

- Weaknesses of the Casio watch firmware or of the Bluetooth LE protocol itself.
- Anything that needs an attacker who already controls the user's session or has root access to the phone.

## Verifying releases

Releases are signed with GPG. The fingerprint and the verification steps are in the [README](README.md#releases).
