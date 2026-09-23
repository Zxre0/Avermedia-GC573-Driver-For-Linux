# Unattended development and boot startup

From a normal account in the project checkout:

```sh
tools/build.sh
sudo tools/enable-unattended-probes.sh
tools/install-startup.sh --boot
tools/install-app.sh
systemctl status gc573-native-boot.service --no-pager
```

The root installer discovers exactly one GC573 and embeds its PCI address and
this checkout's absolute path in a root-owned helper. It grants the invoking
`SUDO_USER` password-free access to `/usr/local/sbin/gc573-codex-probe` through
`/etc/sudoers.d/gc573-codex`. Installation validates sudoers before activation.
If several cards are present, automatic installation refuses to select one.
Moving the checkout requires reinstalling the helper and desktop launchers.

The helper accepts one diagnostic or boot-service installation/removal mode,
serializes hardware calls with a root-owned
lock, clears the child environment, and invokes the checked project loader.
The loader checks the module kernel version, exact PCI identity and existing
binding. It refuses to unbind another driver. `--check` changes no hardware.
Future builds and modes do not require reinstalling the helper.

**Privilege scope:** this explicitly trusts the user-writable project loader
and compiled module with root/kernel execution. It is a development convenience,
not a privilege boundary against modifications to that checkout. The installer
does not grant general password-free sudo access. Do not enable it for an
untrusted checkout or on a multiuser system whose users can modify the project.

The system unit `/etc/systemd/system/gc573-native-boot.service` runs at boot
as the checkout owner. It builds without root, invokes the authorized helper,
and restores that user's saved lighting. It uses its own runtime directory and
video/audio supplementary groups, so a desktop session is not required. Existing
working capture is preserved. The installer disables the older login unit to
avoid duplicate startup. A checkout/home directory unavailable before login
requires the login-only option documented in the README.

Startup waits for source power and HDMI lock, including after TX1 activation.
Completed phases are checkpointed under `/run/gc573-codex/`, bound to the PCI
address and Linux boot ID. A temporary signal wait exits with status 75; systemd
retries after 10 seconds. Checked phases are not replayed on that retry. A
failed or interrupted writing phase stays marked pending and stops automatic
replay. Checkpoints do not persist across reboot. This is not a general reset
or recovery procedure for arbitrary partially initialized hardware.

Check status and logs:

```sh
systemctl status gc573-native-boot.service --no-pager
journalctl -u gc573-native-boot.service -b --no-pager
```

`active (exited)` means initialization completed. A supported HDMI source is
required before initial capture registration. Matching kernel headers are
required to build after a kernel update. A missing source can leave the service
in `activating (auto-restart)`. Hardware/identity/build failures stop the service
and remain visible in the journal. The boot service has been started on the
target system, and the user has confirmed successful automatic loading after
reboot with the corrected sequence. A full power-off/power-on test remains separate.

Each invocation saves combined stdout/stderr in `reports/auto-*.log`. Check the
reported hardware error fields as well as the exit code. The root helper's lock
serializes helper calls only; do not concurrently run the manual root loader.
Close OBS and other capture applications before diagnostic module reloads.
Desktop audio services may also keep the ALSA control device open.

Removal:

```sh
tools/install-startup.sh --remove-boot
tools/install-startup.sh --remove
sudo tools/enable-unattended-probes.sh --remove
rm -f ~/.local/bin/gc573-control ~/.local/share/applications/gc573-control.desktop
```

Removal preserves the checkout, recordings, saved lighting preferences and any
currently loaded module. It does not reset the card or unload unrelated drivers.
