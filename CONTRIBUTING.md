# Contributing

Thank you for your interest. Casio Watch Sync is a small volunteer project; issues and pull requests are welcome.

## Licence

The project is released under GPL-3.0-or-later. By submitting a contribution you agree that it is licensed under the same terms. Contributions must be your own work, or come from material under a GPL-3.0-or-later compatible licence with the origin stated in the pull request. Do not copy code from projects with an incompatible licence.

## Reporting problems

- **Bugs:** open an issue with the app version, the Sailfish OS version, the watch model and the steps to reproduce. Do not paste notification contents or full Bluetooth addresses.
- **Security problems:** see [SECURITY.md](SECURITY.md); do not use a public issue.
- **Other watch models:** open an issue with the exact model name the watch reports, and what worked or failed (time sync, notifications). A model is only marked "supported" after it has been tested on a real watch.

## Building and testing

See the [README](README.md) for the build. Before opening a pull request:

- build with `CONFIG+=hostbuild` and run `make check`; every test must pass;
- add or update a test for the behaviour you change;
- for anything that touches the watch protocol or the phone, say on which hardware you tried it.

## Guidelines

- **Language:** code, comments, identifiers, test data and documents are in English. User-visible strings go through `qsTr()` / `tr()`.
- **Comments:** self-explanatory. State the fact or the rule; do not point to other documents. The only cross-reference allowed is a requirement number (`R-GEN-5`, `R-NOTIF-10`, …) defined in [docs/requirements.md](docs/requirements.md).
- **Protocol facts:** frame layouts and feature behaviour are only added when verified on a real watch; describe how you verified them.
- **Privacy:** notification text is never logged or stored beyond the in-memory queue, and the application must not use the network.
- **Notification categories:** the application-name to icon-category table ships empty. Add an entry only for an application name that was really seen, and say which icon the watch showed.
- **Structure:** keep logic behind the existing interfaces (`BluezAdapter`, `SessionRunner`, `NotifSource`, `StatusPublisher`, `WakeScheduler`) so that it stays testable on a desktop. [docs/who-does-what.md](docs/who-does-what.md) describes the layout.
- **Pictures:** the watch silhouettes are generic drawings. Do not add drawings of real watch models or any manufacturer artwork.
- **Commits:** one logical change per commit, with a short message in the imperative.

## AI-generated code

Code written with the help of AI tools is accepted when all of the following hold:

- it is specific to an issue that was already discussed, or to a feature listed in the To do section of the [README](README.md);
- it is clearly commented and explained: what it does, why, and how it was verified;
- it is thoroughly tested, on the host and, where the change touches the watch or the phone, on real hardware.

## Decisions and forks

The author decides what is merged, and those decisions are final in this repository. You are free to fork the repository and do what you want with your copy, as long as you respect the licence.
