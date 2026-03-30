"""Live lobby/UDP regression. Requires Python websockets and a built Server binary.
Run: python3 voice_udp_smoke.py /absolute/path/to/Server
Tokens are deliberately never printed.
"""
import asyncio
import json
import socket
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import websockets

HEADER = struct.Struct("<B16s32s65sI")
DEST = ("127.0.0.1", 48100)

def packet(kind, room, name, token, payload=b""):
    return HEADER.pack(kind, room.encode(), name.encode(), token.encode(), len(payload)) + payload

async def request(ws, kind, **data):
    await ws.send(json.dumps({"domain": "lobby", "type": kind, "data": data}))

async def response(ws, kind):
    while True:
        data = json.loads(await asyncio.wait_for(ws.recv(), 3))
        if data["type"] == kind:
            return data["data"]
        assert data["type"] != "error", data

async def receive(sock, expected=True):
    try:
        value = await asyncio.wait_for(asyncio.get_running_loop().sock_recv(sock, 4096), .25)
    except asyncio.TimeoutError:
        assert not expected, "Expected authorized relay"
        return None
    assert expected, "Unauthorized voice reached receiver"
    return value

async def main(binary):
    with tempfile.TemporaryDirectory(prefix="mausoleum-smoke-") as temporary:
        with open(Path(temporary) / "stdout.log", "w") as output:
            process = subprocess.Popen([binary], cwd=temporary, stdout=output, stderr=output)
            sockets, connections = [], []
            try:
                for attempt in range(50):
                    try:
                        alice = await websockets.connect("ws://127.0.0.1:8001")
                        connections.append(alice)
                        break
                    except OSError:
                        assert process.poll() is None, "Server exited during startup"
                        await asyncio.sleep(.1)
                else:
                    raise AssertionError("Lobby did not start")
                bob = await websockets.connect("ws://127.0.0.1:8001")
                carol = await websockets.connect("ws://127.0.0.1:8001")
                connections += [bob, carol]
                await request(alice, "create_room", playerName="alice")
                a = await response(alice, "room_created")
                await request(bob, "join_room", roomCode=a["roomCode"], playerName="bob")
                b = await response(bob, "room_joined")
                await request(carol, "create_room", playerName="carol")
                c = await response(carol, "room_created")
                for _ in range(3):
                    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    sock.bind(("127.0.0.1", 0)); sock.setblocking(False)
                    sockets.append(sock)
                sa, sb, sc = sockets
                for sock, data, name in [(sa, a, "alice"), (sb, b, "bob"), (sc, c, "carol")]:
                    sock.sendto(packet(0, data["roomCode"], name, data["voiceSession"]), DEST)
                await asyncio.sleep(.1)
                # Missing token and caller-selected foreign room must not register or relay.
                sa.sendto(packet(1, a["roomCode"], "alice", "", b"bad"), DEST)
                await receive(sb, False)
                sa.sendto(packet(1, c["roomCode"], "alice", a["voiceSession"], b"bad"), DEST)
                await receive(sc, False)
                sa.sendto(packet(1, a["roomCode"], "bob", a["voiceSession"], b"bad"), DEST)
                await receive(sb, False)
                sc.sendto(packet(1, a["roomCode"], "alice", a["voiceSession"], b"stolen-endpoint"), DEST)
                await receive(sb, False)
                sa.sendto(packet(1, a["roomCode"], "alice", a["voiceSession"], b"valid"), DEST)
                relayed = await receive(sb)
                assert relayed[HEADER.size:] == b"valid"
                assert HEADER.unpack(relayed[:HEADER.size])[3] == bytes(65), "Token disclosed by relay"
                await receive(sc, False)
                # Leave request cannot impersonate Bob: connection membership wins.
                await request(alice, "leave_room", roomCode=c["roomCode"], playerName="carol")
                await response(bob, "player_left")
                sa.sendto(packet(1, a["roomCode"], "alice", a["voiceSession"], b"revoked"), DEST)
                await receive(sb, False)
                dave = await websockets.connect("ws://127.0.0.1:8001")
                connections.append(dave)
                await request(dave, "join_room", roomCode=a["roomCode"], playerName="dave")
                d = await response(dave, "room_joined")
                sd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sd.bind(("127.0.0.1", 0)); sd.setblocking(False); sockets.append(sd)
                sd.sendto(packet(0, d["roomCode"], "dave", d["voiceSession"]), DEST)
                await bob.close()
                await response(dave, "player_left")
                sd.sendto(packet(1, d["roomCode"], "dave", d["voiceSession"], b"after-disconnect"), DEST)
                await receive(sb, False)
                print("PASS live lobby-issued authorization, foreign-room/speaker/endpoint rejection, token stripping, leave and disconnect revocation")
            finally:
                for connection in connections:
                    await connection.close()
                for sock in sockets:
                    sock.close()
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()

if __name__ == "__main__":
    asyncio.run(main(str(Path(sys.argv[1]).resolve())))
