# Room voice isolation and overload policy

문서 수정일(사용자 지정): 2026-03-30. 이 날짜는 Git 기록용이며 아래 검증의 실행 시점을 뜻하지 않습니다.

The lobby and voice servers run in the same process (`src/main.cpp`). Successful room creation/join now issues a private 256-bit hex `voiceSession` to that WebSocket connection. The shared registry binds it to the room, speaker, and first UDP endpoint. Every incoming packet is validated before admission to its room shard and again before dispatch. Lobby leave/disconnect/replacement revokes the session. Relay recipients must still have an active session in that room. Sender tokens are zeroed before relay, so other participants cannot obtain them from voice packets. Room leave uses the owning WebSocket membership rather than caller-provided room/name fields.

Both server and Unreal client must be deployed together: the packed voice header adds `char sessionToken[65]` before `payloadSize`. Old clients fail closed. Endpoint changes require a fresh lobby membership/session. Tokens must never be logged. These bearer credentials do not provide UDP encryption or replay resistance; use trusted networking and TLS for lobby transport before public deployment. A separate voice process would need an authenticated shared authorization service; the current singleton is intentionally process-local.

The voice NetworkService bounds its ingress to 128 packets and egress to 256. Each of two room-hashed worker shards retains at most 128 tasks. Full queues discard the oldest queued item, retaining FIFO order among survivors. A closed queue rejects pushes. Worker tasks older than 100 ms are discarded before processing. This bounds backlog storage; it does not promise 100 ms end-to-end audio latency or isolate a hot room sharing a shard. A queued outbound datagram may already be in flight when a lobby session is revoked.

## Reproduce native checks

```sh
cmake -S Server/GameServer/tests -B /tmp/mausoleum-tests
cmake --build /tmp/mausoleum-tests -j2
ctest --test-dir /tmp/mausoleum-tests --output-on-failure
```

The native test checks missing sessions, wrong rooms/speakers/endpoints, revocation, queue FIFO/drop-oldest/closure, age boundaries at 100 ms, and runtime collection boundaries (120 seconds, occupied rooms, missing/negative population, clock rollback, minimum 10-second timeout). A deterministic overload case runs four producers with 10,000 pushes each and no consumer: 128 remain, 39,872 are discarded. This is a synthetic capacity check, not a measured voice-delay improvement. ASan/UBSan is also supported by adding `-fsanitize=address,undefined` compiler/linker flags.

Room runtime collection now requires an exactly zero population. A negative resolver result is treated as unknown and no longer collects a room. `A302.RoomRuntime.IndependentLifecycle` is an Unreal automation test covering room key normalization, empty keys, independent stop/recreate, repeated stop, and the existing offset-only fallback. `A302.RoomRuntime.IndependentPhaseProgress` additionally covers two independent phase timelines, per-room reward counts, and stopping one room without stopping the other. These Unreal tests have not run in this environment because Unreal Engine is absent. Real level streaming, dedicated-server concurrent phase progression, and audio playback remain engine-dependent checks.

## Live server regression

The full native server was compiled and linked against uWebSockets v20.61.0 (commit `3d93f604be479c7a98e4736a277257567590b3bc`, pinned uSockets `833497e8e0988f7fd8d33cd4f6f36056c68d225d`) and nlohmann/json v3.12.0, obtained from their official GitHub repositories over verified HTTPS. The cloud copies live outside the checkout at `/workspace/.onboarding/mausoleum-deps`. The tested native binary is `/workspace/.onboarding/mausoleum-server`. To reproduce with those dependencies:

```sh
python3 Server/GameServer/tests/build_native_server.py \
  --uwebsockets-root /workspace/.onboarding/mausoleum-deps/uWebSockets \
  --json-include-root /workspace/.onboarding/mausoleum-deps \
  --output /workspace/.onboarding/mausoleum-server
```

The normal CMake/vcpkg server target remains available. The standalone helper makes the checked production source build reproducible without modifying its dependency declarations.

After building the native Server target, run:

```sh
python3 Server/GameServer/tests/voice_udp_smoke.py /absolute/path/to/Server
```

This optional integration test requires Python `websockets`, starts a temporary server on localhost ports 8001/48100, and checks real lobby room creation/join, unauthorized and cross-room UDP rejection, endpoint binding, authorized relay, removal of the bearer token from relayed packets, and session revocation after leave/disconnect. Native CTest, ASan/UBSan, the full production link, and this live integration test passed in the cloud environment. It does not start Unreal or claim a game/audio roundtrip.

## Spatial audio evidence and remaining checks

`VoiceAudioReceiver.cpp` configures HRTF, logarithmic attenuation, an inner radius of 450 Unreal units, and falloff distance of 4,200. `DistanceVoiceChatStrategy.cpp` also rejects out-of-range or different-room voice. These are source observations, not listening-test results. Validate the active spatialization plugin, left/right/front/back playback, distance transitions, and lobby/nonspatial playback in Unreal. Room offsets use a hash modulo 128 slots; different room codes are not guaranteed different physical slots, so offset spacing alone must not be presented as proven collision-free room isolation.

A slot collision cannot be corrected only inside `PrepareRoom`: `AA302GameMode::StartRoomGameplay` starts the phase timeline before invoking it and ignores its return value, while client level loading and server item/player spawning independently derive the shared hash. A safe allocator change requires a coordinated room admission error path, authoritative slot replication, and replacing every static lookup; it remains a separate engine integration task.
