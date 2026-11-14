#!/usr/bin/env python3
"""Discord status while GC573 Preview runs, over Discord's local IPC socket."""
import json
import os
from pathlib import Path
import socket
import struct
import threading
import time
import uuid

# The project's Discord application: its name and icon, set in the Discord
# Developer Portal, are what the status shows. Not a secret.
CLIENT_ID = '1571006065657516082'
REPOSITORY = 'https://github.com/Zxre0/Open573-Linux'
HANDSHAKE, FRAME, CLOSE = 0, 1, 2
RETRY_SECONDS = 15


def socket_paths(environ=os.environ):
    """Candidate IPC sockets for native, Flatpak and Snap Discord clients."""
    bases = [environ.get(name) for name in ('XDG_RUNTIME_DIR', 'TMPDIR', 'TMP', 'TEMP')]
    for base in dict.fromkeys(filter(None, bases + ['/tmp'])):
        for folder in ('', 'app/com.discordapp.Discord', 'snap.discord', '.flatpak/com.discordapp.Discord/xdg-run'):
            for index in range(10):
                yield Path(base, folder, f'discord-ipc-{index}')


def encode(opcode, payload):
    data = json.dumps(payload, separators=(',', ':')).encode()
    return struct.pack('<II', opcode, len(data)) + data


def read_frame(connection):
    header = receive(connection, 8)
    opcode, length = struct.unpack('<II', header)
    return opcode, json.loads(receive(connection, length) or b'{}')


def receive(connection, size):
    data = b''
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError('Discord closed the connection')
        data += chunk
    return data


def connect(paths=None):
    for path in paths if paths is not None else socket_paths():
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.connect(str(path))
            return connection
        except OSError:
            connection.close()
    return None


def distribution(paths=(Path('/etc/os-release'), Path('/usr/lib/os-release'))):
    """Distribution name and version from os-release, e.g. 'Fedora Linux 45'."""
    for path in paths:
        try:
            text = path.read_text()
        except OSError:
            continue
        values = {}
        for line in text.splitlines():
            key, separator, value = line.partition('=')
            if separator:
                values[key.strip()] = value.strip().strip('"\'')
        name = ' '.join(filter(None, (values.get('NAME'), values.get('VERSION_ID'))))
        name = name or values.get('PRETTY_NAME', '')
        if len(name) >= 2:
            return name[:128]  # Discord's limit for status text
    return 'Linux'


def activity_frame(started, pid=None, system=None):
    return encode(FRAME, {
        'cmd': 'SET_ACTIVITY', 'nonce': str(uuid.uuid4()),
        'args': {'pid': pid or os.getpid(), 'activity': {
            'details': 'AVerMedia GC573 running on Linux',
            'state': system or distribution(),
            'timestamps': {'start': started},
            # Other people see the button; Discord hides it on your own status.
            'buttons': [{'label': 'Open573-Linux on GitHub', 'url': REPOSITORY}]}}})


class DiscordPresence:
    """Shows the status while running; Discord clears it when the socket closes.

    Everything happens on a background thread, so a missing or slow Discord
    client never delays the preview.
    """

    def __init__(self, paths=None, retry=RETRY_SECONDS):
        self.paths = paths
        self.retry = retry
        self.started = int(time.time())
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.connection = None
        self.thread = threading.Thread(target=self.run, name='discord-presence', daemon=True)

    def start(self):
        self.thread.start()
        return self

    def run(self):
        while not self.stopping.is_set():
            connection = connect(self.paths)
            if connection:
                with self.lock:
                    if self.stopping.is_set():
                        connection.close()
                        return
                    self.connection = connection
                try:
                    connection.sendall(encode(HANDSHAKE, {'v': 1, 'client_id': CLIENT_ID}))
                    opcode, _ready = read_frame(connection)
                    if opcode == FRAME:
                        connection.sendall(activity_frame(self.started))
                        # Consume replies until Discord or stop() closes the socket.
                        while read_frame(connection)[0] != CLOSE:
                            pass
                except (OSError, ValueError, struct.error):
                    pass
                finally:
                    with self.lock:
                        self.connection = None
                    connection.close()
            self.stopping.wait(self.retry)

    def stop(self):
        with self.lock:
            self.stopping.set()
            if self.connection:
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        if self.thread.is_alive():
            self.thread.join(timeout=2)
