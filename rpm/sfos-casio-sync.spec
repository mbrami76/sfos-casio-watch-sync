# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

Name:       sfos-casio-sync
Summary:    Casio Watch Sync
Version:    1.0.0
Release:    1
# The Sailfish OS package lint only accepts this spelling (GPL-3.0-or-later in the source files).
License:    GPLv3+
# Passed to qmake as CWS_URL for the About page.
URL:        https://github.com/mbrami76/sfos-casio-watch-sync
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(nemonotifications-qt5)
BuildRequires:  pkgconfig(keepalive)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5DBus)

%description
Sets the time of Casio Bluetooth watches (target: DW-H5600) from the phone clock
and forwards selected phone notifications to the watch. Runs unsandboxed, uses
no network, collects no data.

# Chum metadata, see Metadata.md in sailfishos-chum/main
%if 0%{?_chum}
Title: Casio Watch Sync
Type: desktop-application
DeveloperName: Michele Brami
Categories:
 - Utility
PackageIcon: https://raw.githubusercontent.com/mbrami76/sfos-casio-watch-sync/main/data/icons/sfos-casio-sync.svg
Screenshots:
 - https://raw.githubusercontent.com/mbrami76/sfos-casio-watch-sync/main/docs/screenshots/main.png
 - https://raw.githubusercontent.com/mbrami76/sfos-casio-watch-sync/main/docs/screenshots/watch.png
 - https://raw.githubusercontent.com/mbrami76/sfos-casio-watch-sync/main/docs/screenshots/notifications.png
AIRating: V
AINote: Almost entirely written with generative tools; specified, reviewed and tested by the author on a real phone and watch.
Custom:
  Repo: https://github.com/mbrami76/sfos-casio-watch-sync
Links:
  Bugtracker: https://github.com/mbrami76/sfos-casio-watch-sync/issues
  Donation: https://liberapay.com/mbrami76
%endif

%prep
%setup -q -n %{name}-%{version}

%build
# CWS_VERSION and CWS_URL reach the About page as defines (app/bridge/bridge.pri).
%qmake5 CWS_VERSION=%{version} CWS_URL=%{url}
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

# User units cannot be enabled by hand on the phone: the enabling symlink is part of the package.
install -d %{buildroot}/usr/lib/systemd/user/user-session.target.wants
ln -s ../sfos-casio-sync-ping.timer %{buildroot}/usr/lib/systemd/user/user-session.target.wants/sfos-casio-sync-ping.timer

%pre
# First install or upgrade: stop a running instance (UI and service) so the new binary is
# the one that runs; the timer started in %%post brings the service back by D-Bus activation.
systemctl-user stop sfos-casio-sync-ping.timer >/dev/null 2>&1 || :
pkill -TERM -x sfos-casio-sync >/dev/null 2>&1 || :
for i in 1 2 3 4 5; do
    pgrep -x sfos-casio-sync >/dev/null 2>&1 || break
    sleep 1
done
exit 0

%post
# The enabling symlink takes effect at the next login of the user session; start the timer
# now as well. Without a user session (build root, ssh without a session) this is skipped.
systemctl-user daemon-reload >/dev/null 2>&1 || :
systemctl-user start sfos-casio-sync-ping.timer >/dev/null 2>&1 || :
exit 0

%preun
# Removal only ($1 = 0); on upgrade %%pre of the new package has done the work and the
# service already started again must not be stopped here.
if [ "$1" -eq 0 ]; then
    systemctl-user stop sfos-casio-sync-ping.timer >/dev/null 2>&1 || :
    pkill -TERM -x sfos-casio-sync >/dev/null 2>&1 || :
    for i in 1 2 3 4 5; do
        pgrep -x sfos-casio-sync >/dev/null 2>&1 || break
        sleep 1
    done
fi
exit 0

%postun
if [ "$1" -eq 0 ]; then
    systemctl-user daemon-reload >/dev/null 2>&1 || :
fi
exit 0

%files
%defattr(-,root,root,-)
%license /usr/share/licenses/%{name}/LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}/
%{_datadir}/applications/%{name}.desktop
%{_datadir}/dbus-1/services/it.michelebrami.sfos_casio_sync.service
/usr/lib/systemd/user/sfos-casio-sync-ping.service
/usr/lib/systemd/user/sfos-casio-sync-ping.timer
/usr/lib/systemd/user/user-session.target.wants/sfos-casio-sync-ping.timer
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Sat Oct 31 2026 mbrami76 <mbpub4com+gh@posteo.it> - 1.0.0-1
- First release.
