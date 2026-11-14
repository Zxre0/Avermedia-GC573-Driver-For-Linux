#!/usr/bin/env python3
"""Discord status over a fake local IPC socket; never contacts Discord."""
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'app'))
import gc573_discord as discord  # noqa: E402


def read(connection):
    opcode, length = struct.unpack('<II', discord.receive(connection, 8))
    return opcode, json.loads(discord.receive(connection, length))


class FakeDiscord:
    def __init__(self, path):
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(str(path))
        self.server.listen()
        self.frames = []
        self.closed = threading.Event()
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        connection, _ = self.server.accept()
        with connection:
            self.frames.append(read(connection))
            connection.sendall(discord.encode(discord.FRAME, {'cmd': 'DISPATCH', 'evt': 'READY'}))
            frame = read(connection)
            self.frames.append(frame)
            connection.sendall(discord.encode(discord.FRAME, {'cmd': 'SET_ACTIVITY', 'nonce': frame[1]['nonce']}))
            try:
                while connection.recv(1024):
                    pass
            except OSError:
                pass
        self.closed.set()


class DiscordPresenceTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def wait_for(self, condition):
        deadline = time.monotonic() + 5
        while not condition():
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.01)

    def test_status_is_set_while_running_and_released_on_stop(self):
        path = self.root / 'discord-ipc-0'
        server = FakeDiscord(path)
        presence = discord.DiscordPresence(paths=[self.root / 'missing', path]).start()
        self.wait_for(lambda: len(server.frames) == 2)
        self.assertEqual(server.frames[0], (discord.HANDSHAKE, {'v': 1, 'client_id': '1571006065657516082'}))
        opcode, command = server.frames[1]
        self.assertEqual(opcode, discord.FRAME)
        self.assertEqual(command['cmd'], 'SET_ACTIVITY')
        self.assertEqual(command['args']['activity'], {'timestamps': {'start': presence.started}})
        self.assertFalse(server.closed.is_set())
        presence.stop()
        # Closing the socket is what makes Discord clear the status.
        self.assertTrue(server.closed.wait(5))
        self.assertFalse(presence.thread.is_alive())

    def test_missing_discord_is_silent_and_stops_promptly(self):
        presence = discord.DiscordPresence(paths=[self.root / 'discord-ipc-0']).start()
        time.sleep(0.05)
        started = time.monotonic()
        presence.stop()
        self.assertLess(time.monotonic() - started, 1)
        self.assertFalse(presence.thread.is_alive())

    def test_socket_search_covers_native_flatpak_and_snap_clients(self):
        paths = set(discord.socket_paths({'XDG_RUNTIME_DIR': '/run/user/1000'}))
        for folder in ('', 'app/com.discordapp.Discord/', 'snap.discord/',
                       '.flatpak/com.discordapp.Discord/xdg-run/'):
            self.assertIn(Path(f'/run/user/1000/{folder}discord-ipc-0'), paths)
        self.assertIn(Path('/tmp/discord-ipc-9'), paths)


if __name__ == '__main__':
    unittest.main()
