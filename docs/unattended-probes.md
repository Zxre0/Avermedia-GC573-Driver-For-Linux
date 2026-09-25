# Unattended development and boot startup

`./install.sh` installs dependencies, the app, helper and boot service together.
`./uninstall.sh` disables startup, unloads the driver and removes the project
installation, including saved lighting settings (use `--keep-settings` to keep
them). Shared packages, source and personal recordings are preserved. See the
[easy setup guide](../README.md#easy-install-and-uninstall). The individual
commands below remain available for manual setup.

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

Since 0.43.0, startup completes the static receiver/splitter preparation, then
registers video/audio devices and RGB before waiting for HDMI input. The module
continues the signal-dependent phases in delayed work. Waits poll every 500 ms;
successful phases advance once, and writes that fail stop the worker. Video DMA
and audio receiver accesses are gated until the worker finishes, so an application
can open the video node while HDMI is absent without racing initialization.

The userspace prefix is checkpointed under `/run/gc573-codex/`, bound to the PCI
address and Linux boot ID. The handoff stays marked pending while the worker owns
startup. A service restart preserves the loaded card; if that module is unloaded
mid-initialization, the checkpoint blocks blind replay of hardware writes.
Checkpoints do not persist across reboot. This is not a general reset or recovery
procedure for arbitrary partially initialized hardware. Exit 75 remains available
for temporary startup-lock contention, with a 10-second service retry.

Check status and logs:

```sh
systemctl status gc573-native-boot.service --no-pager
journalctl -u gc573-native-boot.service -b --no-pager
```

`active (exited)` means the devices are registered, including when HDMI is absent.
Check `hdmi_ready`, `hdmi_waiting`, `hdmi_phase` and `hdmi_error` in the app's
`--status` output for the background HDMI setup. A supported source is needed for
frames, and valid PCM audio is needed to start audio capture. Matching kernel
headers are required to build after a kernel update. Hardware/identity/build
failures remain visible in the journal; deferred protocol failures leave the
devices registered with `hdmi_error` set. The boot service has been started on the
target system, and the user has confirmed successful automatic loading after
reboot with the corrected sequence. A full power-off/power-on test remains separate.

Each invocation saves combined stdout/stderr in `reports/auto-*.log`. Check the
reported hardware error fields as well as the exit code. The root helper's lock
serializes helper calls only; do not concurrently run the manual root loader.
Close OBS and other capture applications before diagnostic module reloads.
Desktop audio services may also keep the ALSA control device open.

Manual removal (keeps the loaded module and preferences):

```sh
tools/install-startup.sh --remove-boot
tools/install-startup.sh --remove
sudo tools/enable-unattended-probes.sh --remove
rm -f ~/.local/bin/gc573-control ~/.local/share/applications/gc573-control.desktop
```

Removal preserves the checkout, recordings, saved lighting preferences and any
currently loaded module. It does not reset the card or unload unrelated drivers.
