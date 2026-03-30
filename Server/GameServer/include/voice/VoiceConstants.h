#pragma once
#include <cstddef>
#include <cstdint>

namespace Voice::Protocol {
    constexpr size_t ROOM_CODE_SIZE = 16;
    constexpr size_t SPEAKER_NAME_SIZE = 32;
}

namespace Voice::Config {
    constexpr size_t INBOUND_QUEUE_CAPACITY = 128;
    constexpr size_t OUTBOUND_QUEUE_CAPACITY = 256;
    constexpr int MAX_QUEUE_AGE_MS = 100;
    constexpr bool IsQueuedVoiceExpired(double ageMilliseconds) { return ageMilliseconds > MAX_QUEUE_AGE_MS; }
    constexpr int CLEANUP_INTERVAL_SECONDS = 60;
    constexpr int CLIENT_TIMEOUT_SECONDS = 300;
    constexpr int UDP_MAX_PACKET_SIZE = 4096;
    constexpr uint32_t MAX_VOICE_PAYLOAD_SIZE = 2048;
    constexpr uint32_t LOG_PAYLOAD_MIN_SIZE = 2;
}
