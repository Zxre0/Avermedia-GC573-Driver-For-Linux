#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""GC573 Control: local V4L2 RGB controls and live FPGA input status."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import struct
import sys

CID_MODE = 0x00982900
CID_COLOR = CID_MODE + 1
CID_BRIGHTNESS = CID_MODE + 2
VIDIOC_S_CTRL = 0xC008561C
MODES = ('Rainbow', 'Solid', 'Off')
SETTINGS = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'gc573-control/settings.json'


def parse_status(text):
    values = {}
    for line in text.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            try:
                values[key] = int(value, 16 if value.startswith('0x') else 10)
            except ValueError:
                pass
    return values


def discover(root=Path('/sys/class/video4linux')):
    result = []
    for node in sorted(root.glob('video*')):
        try:
            pci = (node / 'device').resolve()
            if (pci / 'vendor').read_text().strip() != '0x1461' or (pci / 'device').read_text().strip() != '0x0054':
                continue
            if (pci / 'subsystem_device').read_text().strip() != '0x5730':
                continue
            if (pci / 'driver').resolve().name != 'gc573_native':
                continue
            result.append((Path('/dev') / node.name, pci / 'bringup_status'))
        except OSError:
            continue
    return result


def validate(mode, color, brightness):
    if type(mode) is not int or mode not in range(3):
        raise ValueError('Select Rainbow, Solid or Off.')
    if type(color) is not int or not 0 <= color <= 0xffffff:
        raise ValueError('Color must be a six-digit RGB color.')
    if type(brightness) is not int or not 0 <= brightness <= 100:
        raise ValueError('Brightness must be between 0 and 100.')


def apply(device, mode, color, brightness):
    validate(mode, color, brightness)
    fd = os.open(device, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        for control, value in ((CID_COLOR, color), (CID_BRIGHTNESS, brightness), (CID_MODE, mode)):
            fcntl.ioctl(fd, VIDIOC_S_CTRL, struct.pack('Ii', control, value))
    finally:
        os.close(fd)


def save(mode, color, brightness):
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    temp = SETTINGS.with_suffix('.tmp')
    temp.write_text(json.dumps(dict(mode=mode, color=color, brightness=brightness)) + '\n')
    temp.replace(SETTINGS)


def restore():
    if not SETTINGS.exists():
        return
    data = json.loads(SETTINGS.read_text())
    values = [data[k] for k in ('mode', 'color', 'brightness')]
    validate(*values)
    devices = discover()
    if devices:
        apply(devices[0][0], *values)


def run_gui():
    import gi
    gi.require_version('Gtk', '4.0')
    from gi.repository import Gtk, Gdk, GLib

    class Control(Gtk.Application):
        def __init__(self):
            super().__init__(application_id='io.github.gc573.Control')
            self.device = None
            self.initialized = False

        def do_activate(self):
            if self.get_active_window():
                self.get_active_window().present()
                return
            win = Gtk.ApplicationWindow(application=self, title='GC573 Control')
            win.set_default_size(590, 510)
            css = Gtk.CssProvider()
            css.load_from_data(b"""
                window { background: #151922; color: #edf2fa; }
                .title { font-size: 28px; font-weight: 700; }
                .signal { font-size: 23px; font-weight: 600; }
                .muted { color: #a6b5ca; }
                .panel { background: #202938; border-radius: 16px; padding: 20px; }
                button { padding: 9px 16px; border-radius: 9px; }
                .apply { background: #587fe9; color: white; }
            """)
            Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            for side in ('top', 'bottom', 'start', 'end'):
                getattr(outer, 'set_margin_' + side)(26)
            win.set_child(outer)
            def label(text, style):
                widget = Gtk.Label(label=text, xalign=0)
                if style:
                    widget.add_css_class(style)
                return widget
            outer.append(label('GC573 Control', 'title'))
            outer.append(label('HDMI input and lighting', 'muted'))
            panel = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            panel.add_css_class('panel')
            panel.set_margin_top(14)
            self.signal = label('Looking for your capture card…', 'signal')
            self.details = label('', 'muted')
            self.audio = label('', 'muted')
            panel.append(self.signal)
            panel.append(self.details)
            panel.append(self.audio)
            outer.append(panel)
            grid = Gtk.Grid(column_spacing=24, row_spacing=15)
            grid.set_margin_top(18)
            self.mode = Gtk.DropDown.new_from_strings(MODES)
            self.color = Gtk.ColorButton()
            rgba = Gdk.RGBA(); rgba.parse('#ffffff'); self.color.set_rgba(rgba)
            self.color.connect('color-set', lambda _: self.mode.set_selected(1))
            self.brightness = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
            self.brightness.set_value(100)
            self.brightness.set_digits(0)
            for row, (name, widget) in enumerate((('Lighting',self.mode),('Solid color',self.color),('Brightness',self.brightness))):
                grid.attach(label(name, ''), 0, row, 1, 1)
                widget.set_hexpand(True)
                grid.attach(widget, 1, row, 1, 1)
            outer.append(grid)
            self.apply_button = Gtk.Button(label='Apply lighting')
            self.apply_button.add_css_class('apply')
            self.apply_button.set_halign(Gtk.Align.END)
            self.apply_button.set_margin_top(10)
            self.apply_button.connect('clicked', self.commit)
            outer.append(self.apply_button)
            self.message = label('Changes apply without interrupting OBS.', 'muted')
            self.message.set_wrap(True)
            outer.append(self.message)
            self.update()
            GLib.timeout_add(1000, self.update)
            win.present()

        def commit(self, _):
            if not self.device:
                return
            rgba = self.color.get_rgba()
            color = (round(rgba.red * 255) << 16) | (round(rgba.green * 255) << 8) | round(rgba.blue * 255)
            values = (self.mode.get_selected(), color, round(self.brightness.get_value()))
            try:
                apply(self.device, *values)
                save(*values)
                self.message.set_text('Lighting applied and saved.')
            except PermissionError:
                self.message.set_text('Access denied. Your session needs access to the video device.')
            except (OSError, ValueError) as exc:
                self.message.set_text(f'Lighting was not applied: {exc}')

        def update(self):
            devices = discover()
            self.device = devices[0][0] if devices else None
            self.apply_button.set_sensitive(bool(devices))
            try:
                if not devices:
                    self.signal.set_text('Card not available')
                    self.details.set_text('Waiting for the native GC573 driver.')
                    self.audio.set_text('')
                    self.initialized = False
                else:
                    values = parse_status(devices[0][1].read_text())
                    if values.get('audio_prepare_error'):
                        self.audio.set_text('HDMI audio needs a 48 kHz stereo PCM signal')
                    elif values.get('audio_registered'):
                        self.audio.set_text('HDMI audio · 48 kHz stereo · ' + ('Capturing' if values.get('audio_running') else 'Ready'))
                    else:
                        self.audio.set_text('HDMI audio unavailable · requires 48 kHz stereo PCM')
                    if values.get('input_present'):
                        fps = values.get('input_fps_milli', 0) / 1000
                        self.signal.set_text(f"{values.get('input_width',0)} × {values.get('input_height',0)}  ·  {fps:.2f} fps")
                        active = values.get('capture_streaming', 0) == 1
                        self.details.set_text('HDMI signal detected  •  ' + ('Capturing' if active else 'Ready'))
                    else:
                        self.signal.set_text('No HDMI signal')
                        self.details.set_text('Connect an active HDMI source.')
                    if not self.initialized:
                        self.mode.set_selected(min(values.get('rgb_mode',0),2))
                        rgba = Gdk.RGBA(); rgba.parse(f"#{values.get('rgb_color',0xffffff):06x}")
                        self.color.set_rgba(rgba)
                        self.brightness.set_value(values.get('rgb_brightness',100))
                        self.initialized = True
                    if values.get('capture_error') == -67:
                        self.details.set_text('Waiting for supported HDMI input to return…')
                    elif values.get('capture_error',0):
                        self.details.set_text('Capture needs recovery; input status is shown above.')
                    if values.get('hdmi_error', 0):
                        self.details.set_text(f"HDMI setup stopped at phase {values.get('hdmi_phase', 0)} (error {values['hdmi_error']}).")
                    elif values.get('hdmi_deferred') and not values.get('hdmi_ready'):
                        self.details.set_text('Waiting for stable HDMI timing…' if values.get('hdmi_phase') == 16
                                              else 'Driver loaded · waiting for supported HDMI input…')
                        self.audio.set_text('HDMI audio · waiting for input')
                    if values.get('led_error',0):
                        self.message.set_text('The driver reported a lighting error.')
            except OSError:
                self.signal.set_text('Card disconnected')
                self.details.set_text('Waiting for the native driver…')
                self.audio.set_text('')
                self.initialized = False
            return GLib.SOURCE_CONTINUE

    Control().run([sys.argv[0]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--status', action='store_true', help='print live status as JSON')
    parser.add_argument('--restore', action='store_true', help='restore saved RGB settings, then exit')
    parser.add_argument('--mode', choices=['rainbow', 'solid', 'off'])
    parser.add_argument('--color', default='ffffff')
    parser.add_argument('--brightness', type=int, default=100)
    args = parser.parse_args()
    try:
        if args.status:
            devices = discover()
            print(json.dumps(parse_status(devices[0][1].read_text()) if devices else {'available': False}, indent=2))
        elif args.restore:
            restore()
        elif args.mode:
            devices = discover()
            if not devices:
                raise ValueError('No native GC573 video device is available.')
            apply(devices[0][0], ['rainbow', 'solid', 'off'].index(args.mode), int(args.color.lstrip('#'), 16), args.brightness)
        else:
            run_gui()
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f'GC573: {exc}\n')


if __name__ == '__main__':
    main()
