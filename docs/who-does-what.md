# Who does what

Every source file and its responsibility. See [architecture.md](architecture.md) for how the parts fit together and [requirements.md](requirements.md) for the `R-…` identifiers.

## Build layout

| File | Role |
|---|---|
| `sfos-casio-sync.pro` | Top-level qmake project: `core`, `service`, `app`, `data`, `tests`. |
| `common.pri` | Shared compiler settings and include paths. `CONFIG+=hostbuild` builds for a desktop without Sailfish libraries. |
| `core/core.pro`, `service/service.pro`, `app/app.pro`, `app/bridge/bridge.pro`, `app/program/program.pro`, `data/data.pro`, `tests/tests.pro` | Sub-projects. |
| `rpm/sfos-casio-sync.spec` | RPM spec (version 1.0.0). |

## `core/` — logic without UI or process wiring

| Files | Responsibility | Requirements |
|---|---|---|
| `watchproto.{h,cpp}` | Pure frame logic: builds the time frame (`0x09`) and notification frames (for characteristic `26eb0030`, inverted bytes), validates fields, truncates UTF-8 on character boundaries, normalises text, encodes the day of week per model. No I/O. | R-TIME-6, R-TIME-7, R-NOTIF-5, R-NOTIF-6 |
| `modeltable.{h,cpp}` | Reads `models.json`: name patterns, support status (supported / untested), per-model parameters, default silhouette. | R-GEN-3 |
| `notiffilter.{h,cpp}` | Pure rules for a phone notification: master switch, own-app drop, blacklist, enabled apps, private/empty drop, sender shortening, category lookup (empty table). | R-NOTIF-1, R-NOTIF-4, R-NOTIF-10 |
| `notifmonitor.{h,cpp}` | Listens to `Notify` calls on the session bus with libdbus `BecomeMonitor` on its own silent connection; parses the message; reconnects with delays from 1 s to 1 min. | R-NOTIF-2, R-NOTIF-3 |
| `settings.{h,cpp}` | `Settings` (INI), `State` (JSON, atomic writes) and loading of the model table. Watches, intervals, enabled apps, blacklist, per-watch silhouette. | R-GEN-5, R-TIME-9, R-NOTIF-1 |
| `scheduler.{h,cpp}` | Pure scheduling: which watches are due, back-off after failures, next wake-up time, a replaceable clock. | R-TIME-8, R-TIME-9 |
| `wakescheduler.{h,cpp}` | Wrapper around `BackgroundActivity` (libkeepalive): schedule one wake-up, `hold()`/`release()` to keep the phone awake. Host build uses a stub. | |
| `watchlink.{h,cpp}` | Owns the BLE session: finds the paired watch, connects, writes the time frame, the link-status notification and queued alerts with pacing, keeps the circular alert queue and retry intervals. All BlueZ calls go through `BluezAdapter`. | R-TIME-10, R-TIME-11, R-NOTIF-9, R-NOTIF-11 |
| `bluezadapter.{h,cpp}` | `BluezDBusAdapter`: the real `BluezAdapter` over `org.bluez` with QtDBus. Not covered by host tests. | R-PLAT-2, R-PLAT-3 |

## `service/` — the background process (`sfos-casio-sync --service`)

| Files | Responsibility |
|---|---|
| `serviceiface.h` | The seams: `SessionRunner`, `NotifSource`, `StatusPublisher` and the data types passed between them. |
| `servicecore.{h,cpp}` | Wires scheduler, wake-ups, watch sessions, notification source, filter and status publisher; handles the D-Bus operations (status, sync now, test notification, list watches). |
| `servicebus.{h,cpp}` | The D-Bus API object on the session bus. |
| `workerrunner.{h,cpp}` | Runs the blocking BLE sessions on a dedicated worker thread. |
| `monitorsource.{h,cpp}` | Adapts `NotifMonitor` to the `NotifSource` seam. |
| `nemopublisher.{h,cpp}` | Shows the resident "last sync" notification through `nemonotifications` and handles its tap action. |
| `servicelog.{h,cpp}` | Optional debug log (off by default, metadata only). |
| `servicemain.{h,cpp}` | Entry point of the service mode. |

## `app/` — user interface

| Files | Responsibility |
|---|---|
| `program/main.cpp` | One binary, two modes: `--service` starts the service, otherwise the UI. |
| `bridge/uibridge.{h,cpp}` | The object exposed to QML as `cws`; connects the models and the service client. |
| `bridge/serviceclient.{h,cpp}` | D-Bus client of the service; starts it by activation when needed. |
| `bridge/settingsbridge.{h,cpp}` | Reads and writes settings for the UI and keeps UI-private settings. |
| `bridge/watchesmodel.{h,cpp}` | List model of the watches. |
| `bridge/appsmodel.{h,cpp}` | List model of the applications and their on/off switch. |
| `bridge/silhouettecatalog.{h,cpp}` | Catalogue of the watch pictures and the rule for which one is shown. |
| `bridge/formatter.{h,cpp}` | Text for times and intervals. |
| `bridge/appinfo.{h,cpp}` | Version, project address, link to the system Bluetooth settings. |
| `bridge/instanceguard.{h,cpp}` | Single-instance UI through its own bus name and `Activate()`. |
| `qml/sfos-casio-sync.qml` | Application window. |
| `qml/pages/` | `StatusPage`, `WatchesPage`, `WatchDetailPage`, `SilhouettePage`, `NotificationsPage`, `BlacklistPage`, `SettingsPage`, `AboutPage`, `NoticeDialog`, `RiskDialog`. |
| `qml/components/` | `MasterSwitch`, `WatchActions`, `WatchBlock`, `WatchSilhouette`, `texts.js` (user-visible texts). |
| `qml/cover/CoverPage.qml` | Application cover. |
| `images/silhouettes/` | Five generic watch drawings and their index. |

## `data/` and `rpm/` — installed files

| File | Role |
|---|---|
| `data/models.json` | Model table installed to `/usr/share/sfos-casio-sync/`. |
| `data/sfos-casio-sync.desktop` | Launcher; `Sandboxing=Disabled`. |
| `data/dbus/it.michelebrami.sfos_casio_sync.service` | D-Bus activation file for the service. |
| `data/systemd/sfos-casio-sync-ping.{service,timer}` | User timer that pings the service so it is started after a reboot or a crash. |
| `data/icons/` | Application icon: SVG and PNG at 86, 108, 128 and 172 px. |

## `tests/`

| Directory | What it tests |
|---|---|
| `watchproto`, `notiffilter`, `scheduler`, `settings` | Pure logic against golden vectors and rules. |
| `watchlink` | Sessions, queue, retries and watch matching with a fake `BluezAdapter`. |
| `notifmonitor` | Parsing and a private `dbus-daemon` for the monitor. |
| `service` | `ServiceCore` wiring with fakes for the worker, the notification source, the clock and wake-ups. |
| `uibridge` | Models, formatter, silhouette rule, single instance, QML text rules. |
| `watchlink-cli`, `notifmonitor-cli`, `wakeprobe` | Command-line tools for manual checks on a phone; not part of `make check`. |
