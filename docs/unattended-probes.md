# Unattended development and login startup

From a normal account in the project checkout:

```sh
tools/build.sh
sudo tools/enable-unattended-probes.sh
tools/install-startup.sh
tools/install-app.sh
tools/run-probe.sh --start
```

The root installer discovers exactly one GC573 and embeds its PCI address and
this checkout's absolute path in a root-owned helper. It grants the invoking
`SUDO_USER` password-free access to `/usr/local/sbin/gc573-codex-probe` through
`/etc/sudoers.d/gc573-codex`. Installation validates sudoers before activation.
If several cards are present, automatic installation refuses to select one.
Moving the checkout requires reinstalling the helper and desktop launchers.

The helper accepts one diagnostic mode, serializes calls with a root-owned
lock, clears the child environment, and invokes the checked project loader.
The loader checks the module kernel version, exact PCI identity and existing
binding. It refuses to unbind another driver. `--check` changes no hardware.
Future builds and modes do not require reinstalling the helper.

**Privilege scope:** this explicitly trusts the user-writable project loader
and compiled module with root/kernel execution. It is a development convenience,
not a privilege boundary against modifications to that checkout. The installer
does not grant general password-free sudo access. Do not enable it for an
untrusted checkout or on a multiuser system whose users can modify the project.

The optional user service builds as the desktop user at login, invokes `--start`,
and restores that user's saved lighting settings. Startup preserves an already
working capture device. An active supported HDMI source is required. Full
power-cycle startup and suspend/resume have not yet been verified.

Each invocation saves combined stdout/stderr in `reports/auto-*.log`. Check the
reported hardware error fields as well as the exit code. The root helper's lock
serializes helper calls only; do not concurrently run the manual root loader.
Close OBS and other capture applications before diagnostic module reloads.
Desktop audio services may also keep the ALSA control device open.

Removal:

```sh
tools/install-startup.sh --remove
sudo tools/enable-unattended-probes.sh --remove
rm -f ~/.local/bin/gc573-control ~/.local/share/applications/gc573-control.desktop
```

Removal preserves the checkout, recordings, saved lighting preferences and any
currently loaded module. It does not reset the card or unload unrelated drivers.
