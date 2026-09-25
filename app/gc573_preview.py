#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""A local GC573 video/audio preview window for desktop screen sharing."""
import argparse
from pathlib import Path
import sys
import threading
import time

from gc573_control import discover, parse_status


class LatestFrame:
    """One copied frame, never an unbounded queue of captured console frames."""
    def __init__(self):
        self.lock = threading.Lock()
        self.frame = None
        self.count = 0

    def put(self, frame):
        with self.lock:
            self.frame = frame
            self.count += 1

    def take(self):
        with self.lock:
            frame, self.frame = self.frame, None
            return frame


def capture_problem(values):
    if values.get('passthrough_only'):
        return 'Passthrough-only mode. Switch to capture mode to preview or share your PS5.'
    if values.get('hdmi_error'):
        return f"HDMI setup stopped (error {values['hdmi_error']}). Check GC573 Control."
    if not values.get('hdmi_ready', 1) or not values.get('input_present'):
        return 'Waiting for your console. Use 1080p SDR with HDCP disabled.'
    if (values.get('input_width'), values.get('input_height')) not in ((1920, 1080), (1280, 720)):
        return 'Unsupported input. Set your console to 1080p SDR for capture.'
    return None


def audio_device(status, root=Path('/sys/class/sound')):
    for card in sorted(root.glob('card[0-9]*')):
        if (card / 'device').resolve() == status.parent.resolve():
            return f'hw:{card.name[4:]},0'
    return None


def run_gui(args):
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Gdk', '4.0')
    gi.require_version('Gst', '1.0')
    gi.require_version('GstApp', '1.0')
    gi.require_version('GstVideo', '1.0')
    from gi.repository import Gtk, Gdk, GLib, Gst, GstApp, GstVideo
    Gst.init(None)
    missing = [name for name in ('v4l2src', 'appsink', 'alsasrc', 'pulsesink',
                                 'audioconvert', 'audioresample', 'volume')
               if not Gst.ElementFactory.find(name)]
    if missing:
        raise RuntimeError('Missing GStreamer plugins: ' + ', '.join(missing) + '. See README preview dependencies.')

    class Preview(Gtk.Application):
        def __init__(self):
            super().__init__(application_id='io.github.gc573.Preview')
            self.video = self.audio = None
            self.frames = LatestFrame()
            self.rendered = 0
            self.last_frame = 0
            self.want_running = True
            self.audio_enabled = not args.mute
            self.audio_failed = False
            self.audio_message = ''
            self.active_device = None
            self.active_size = None
            self.sources = []

        def do_activate(self):
            if self.get_active_window():
                self.get_active_window().present()
                return
            self.win = Gtk.ApplicationWindow(application=self, title='GC573 Preview')
            self.win.set_default_size(1100, 700)
            self.win.connect('close-request', self.close)
            self.header = Gtk.HeaderBar()
            self.header.set_title_widget(Gtk.Label(label='GC573 Preview'))
            self.pause = Gtk.Button(label='Pause')
            self.pause.connect('clicked', self.toggle_play)
            self.header.pack_start(self.pause)
            self.sound = Gtk.ToggleButton(label='Sound', active=self.audio_enabled)
            self.sound.connect('toggled', self.toggle_audio)
            self.header.pack_start(self.sound)
            self.volume = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
            self.volume.set_size_request(100, -1)
            self.volume.set_value(70)
            self.volume.set_tooltip_text('Console audio volume')
            self.volume.connect('value-changed', self.set_volume)
            self.header.pack_start(self.volume)
            fullscreen = Gtk.Button(label='Fullscreen')
            fullscreen.connect('clicked', lambda *_: self.fullscreen())
            self.header.pack_end(fullscreen)
            help_button = Gtk.MenuButton(label='Share in Discord')
            popover = Gtk.Popover()
            help_text = Gtk.Label(label='In Discord, join your call and choose Share Your Screen.\n'
                                 'Select the GC573 Preview window in the screen picker.\n'
                                 'If offered, enable sound and select GC573 Preview audio.\n\n'
                                 'Keep this app running. Close OBS while using this preview.\n'
                                 'Sound sharing depends on your Discord client.\n'
                                 'The app does not join calls or start sharing for you.')
            help_text.set_margin_top(18)
            help_text.set_margin_bottom(18)
            help_text.set_margin_start(18)
            help_text.set_margin_end(18)
            popover.set_child(help_text)
            help_button.set_popover(popover)
            self.header.pack_end(help_button)
            self.win.set_titlebar(self.header)
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
            self.picture = Gtk.Picture()
            self.picture.set_content_fit(Gtk.ContentFit.CONTAIN)
            self.picture.set_hexpand(True)
            self.picture.set_vexpand(True)
            overlay = Gtk.Overlay()
            overlay.set_child(self.picture)
            self.message = Gtk.Label(label='Connecting to your console…', wrap=True)
            self.message.set_justify(Gtk.Justification.CENTER)
            overlay.add_overlay(self.message)
            box.append(overlay)
            self.status = Gtk.Label(label='Starting preview', xalign=0)
            self.status.set_margin_start(14)
            self.status.set_margin_end(14)
            self.status.set_margin_top(8)
            self.status.set_margin_bottom(8)
            box.append(self.status)
            self.win.set_child(box)
            css = Gtk.CssProvider()
            css.load_from_data(b'window { background: #101318; color: #eef1f6; } '
                               b'picture { background: black; } headerbar { background: #1c222c; }')
            Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), css,
                                                      Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            keys = Gtk.EventControllerKey()
            keys.connect('key-pressed', self.key)
            self.win.add_controller(keys)
            self.sources = [GLib.timeout_add(16, self.draw), GLib.timeout_add(500, self.poll)]
            self.win.present()
            self.poll()
            if args.smoke_seconds:
                self.sources.append(GLib.timeout_add_seconds(args.smoke_seconds, self.smoke_done))

        def fullscreen(self):
            full = not self.win.is_fullscreen()
            self.header.set_visible(not full)
            self.status.set_visible(not full)
            self.win.fullscreen() if full else self.win.unfullscreen()

        def key(self, _controller, key, _code, _state):
            if key == Gdk.KEY_F11 or (key == Gdk.KEY_Escape and self.win.is_fullscreen()):
                self.fullscreen()
                return True
            return False

        def toggle_play(self, _button):
            self.want_running = not self.want_running
            self.pause.set_label('Pause' if self.want_running else 'Resume')
            if not self.want_running:
                self.stop()
            self.poll()

        def toggle_audio(self, button):
            self.audio_enabled = button.get_active()
            self.audio_failed = False
            self.audio_message = ''
            if not self.audio_enabled:
                self.stop_pipeline('audio')
            self.poll()

        def set_volume(self, scale):
            if self.audio:
                self.audio.get_by_name('volume').set_property('volume', scale.get_value() / 100)

        def watch(self, pipeline, kind):
            bus = pipeline.get_bus()
            bus.add_signal_watch()
            bus.connect('message', self.bus_message, kind)

        def bus_message(self, _bus, message, kind):
            if message.type == Gst.MessageType.ERROR:
                error, _debug = message.parse_error()
                print(f'{kind}: {error.message}', file=sys.stderr, flush=True)
                if kind == 'audio':
                    self.stop_pipeline('audio')
                    self.audio_failed = True
                    self.audio_message = 'Audio unavailable — close other audio capture apps; toggle Sound to retry.'
                else:
                    self.stop()
                    self.want_running = False
                    self.pause.set_label('Retry')
                    self.show_message('Could not capture video. Close OBS or other capture apps, then press Retry.')
                    self.status.set_text(error.message)
            elif message.type == Gst.MessageType.LATENCY:
                pipeline = self.audio if kind == 'audio' else self.video
                if pipeline:
                    pipeline.recalculate_latency()

        def sample(self, sink):
            sample = sink.emit('pull-sample')
            if sample is None:
                return Gst.FlowReturn.EOS
            info = GstVideo.VideoInfo.new_from_caps(sample.get_caps())
            buffer = sample.get_buffer()
            ok, mapped = buffer.map(Gst.MapFlags.READ)
            if not ok:
                return Gst.FlowReturn.ERROR
            try:
                # Copy before the driver reuses its mmap buffer. GTK texture creation
                # stays on the main thread; replacing this slot drops stale frames.
                data = GLib.Bytes.new(mapped.data)
                self.frames.put((info.width, info.height, info.stride[0], data))
            finally:
                buffer.unmap(mapped)
            return Gst.FlowReturn.OK

        def draw(self):
            frame = self.frames.take()
            if frame is not None and self.video:
                width, height, stride, data = frame
                texture = Gdk.MemoryTexture.new(width, height, Gdk.MemoryFormat.B8G8R8, data, stride)
                self.picture.set_paintable(texture)
                self.message.set_visible(False)
                self.last_frame = time.monotonic()
                self.rendered += 1
            return True

        def show_message(self, text):
            self.picture.set_paintable(None)
            self.message.set_text(text)
            self.message.set_visible(True)

        def stop_pipeline(self, kind):
            pipeline = getattr(self, kind)
            setattr(self, kind, None)
            if pipeline:
                pipeline.get_bus().remove_signal_watch()
                pipeline.set_state(Gst.State.NULL)

        def stop(self):
            self.stop_pipeline('audio')
            self.stop_pipeline('video')
            self.frames.take()
            self.active_device = self.active_size = None
            self.last_frame = 0
            self.audio_failed = False
            self.audio_message = ''

        def start_video(self, device, width, height):
            self.video = Gst.parse_launch(
                f'v4l2src name=camera io-mode=mmap do-timestamp=true ! '
                f'video/x-raw,format=BGR,width={width},height={height} ! '
                'appsink name=frames emit-signals=true max-buffers=1 drop=true '
                'sync=false enable-last-sample=false')
            self.video.get_by_name('camera').set_property('device', str(device))
            self.video.get_by_name('frames').connect('new-sample', self.sample)
            self.watch(self.video, 'video')
            self.active_device, self.active_size = device, (width, height)
            self.last_frame = time.monotonic()
            if self.video.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
                self.stop()
                raise RuntimeError('Could not start capture. Close OBS and press Retry.')

        def start_audio(self, device):
            self.audio = Gst.parse_launch(
                'alsasrc name=console buffer-time=40000 latency-time=10000 ! '
                'audio/x-raw,format=S16LE,rate=48000,channels=2 ! '
                'audioconvert ! audioresample ! volume name=volume ! '
                'pulsesink client-name="GC573 Preview" buffer-time=40000 latency-time=10000')
            self.audio.get_by_name('console').set_property('device', device)
            self.set_volume(self.volume)
            self.watch(self.audio, 'audio')
            if self.audio.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
                self.stop_pipeline('audio')
                self.audio_failed = True
                self.audio_message = 'Audio could not start. Toggle Sound to retry.'

        def poll(self):
            try:
                if not self.want_running:
                    if self.pause.get_label() != 'Retry':
                        self.show_message('Preview paused')
                        self.status.set_text('Resume to view your console')
                    return True
                devices = discover()
                if len(devices) != 1:
                    self.stop()
                    self.show_message('Connect one GC573 with the native driver loaded.')
                    self.status.set_text('Waiting for capture card')
                    return True
                device, status = devices[0]
                values = parse_status(status.read_text())
                problem = capture_problem(values)
                if problem:
                    self.stop()
                    self.show_message(problem)
                    self.status.set_text('No supported capture signal')
                    return True
                size = values['input_width'], values['input_height']
                if self.video and (self.active_device != device or self.active_size != size):
                    self.stop()
                if not self.video:
                    self.start_video(device, *size)
                if self.audio_enabled and not self.audio and not self.audio_failed:
                    source = audio_device(status)
                    if source:
                        self.start_audio(source)
                    else:
                        self.audio_failed = True
                        self.audio_message = 'No GC573 audio device found'
                if time.monotonic() - self.last_frame > 3:
                    self.show_message('Waiting for video frames…')
                sound = self.audio_message or ('Sound on' if self.audio else 'Sound off')
                self.status.set_text(f'{size[0]} × {size[1]} · {values.get("input_fps_milli", 0)/1000:.2f} fps · {sound}')
            except (OSError, RuntimeError, GLib.Error) as exc:
                self.stop()
                self.want_running = False
                self.pause.set_label('Retry')
                self.show_message(str(exc))
            return True

        def close(self, *_args):
            for source in self.sources:
                GLib.source_remove(source)
            self.sources = []
            self.stop()
            return False

        def smoke_done(self):
            print(f'preview_received={self.frames.count} preview_rendered={self.rendered} '
                  f'audio_running={int(self.audio is not None)}', flush=True)
            self.close()
            self.quit()
            return False

    app = Preview()
    result = app.run([])
    if args.smoke_seconds and not app.rendered:
        return 1
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mute', action='store_true', help='Start without local console audio')
    parser.add_argument('--smoke-seconds', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        sys.exit(run_gui(args))
    except (ImportError, ValueError, RuntimeError) as exc:
        sys.exit(f'GC573 Preview could not start: {exc}')
