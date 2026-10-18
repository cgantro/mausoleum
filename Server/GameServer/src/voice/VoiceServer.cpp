#include "voice/VoiceServer.h"
#include <algorithm>
#include <memory>
#include "voice/VoiceSessionRegistry.h"
#include <thread>

namespace {
std::string fixedString(const char* buffer, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && buffer[len] != '\0') {
        ++len;
    }
    return std::string(buffer, len);
}
} // namespace

VoiceServer::VoiceServer() : lastCleanupTime(std::chrono::steady_clock::now()) {
    // 패킷 핸들러 등록
    packetRouter.registerHandler(VoicePacketType::Join, [this](ParsedPacket& packet){
        handleJoin(packet);
    });
    packetRouter.registerHandler(VoicePacketType::VoiceData, [this](ParsedPacket& packet){
        handleVoiceData(packet);
    });
    packetRouter.registerHandler(VoicePacketType::Leave, [this](ParsedPacket& packet){
        handleLeave(packet);
    });

    // 네트워크 수신 핸들러 등록
    network.registerHandler(NetProtocol::Udp, [this](NetPacket&& packet, NetworkService&){
        onUdpPacket(std::move(packet));
    });
}

void VoiceServer::run(int port) {
    running = true;
    startVoiceWorkers();

    if (!network.addUdpEndpoint(port, Voice::Config::UDP_MAX_PACKET_SIZE)) {
        LOG_ERROR(tag(), "UDP 엔드포인트 등록 실패: " << port);
        running = false;
        stopVoiceWorkers();
        return;
    }
    if (!network.start()) {
        LOG_ERROR(tag(), "NetworkService 시작 실패");
        running = false;
        stopVoiceWorkers();
        return;
    }

    while (running) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(
                now - lastCleanupTime).count() > Voice::Config::CLEANUP_INTERVAL_SECONDS) {
            clientManager.cleanupStaleClients();
            lastCleanupTime = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    network.stop();
    stopVoiceWorkers();
}

void VoiceServer::shutdown() {
    running = false;
    network.stop();
    stopVoiceWorkers();
}

void VoiceServer::onUdpPacket(NetPacket&& packet) {
    if (packet.protocol != NetProtocol::Udp) {
        LOG_WARN(tag(), "UDP 핸들러에 TCP 패킷이 전달됨");
        return;
    }
    if (!voiceWorkersRunning_.load(std::memory_order_acquire)) {
        return;
    }

    if (!packet.sharedData && !packet.data.empty()) {
        auto payload = std::make_shared<std::vector<char>>(std::move(packet.data));
        packet.sharedData = std::static_pointer_cast<const std::vector<char>>(payload);
    }

    ParsedPacket admission{};
    if (!buildPacket(packet, admission) ||
        !VoiceSessionRegistry::instance().authorize(
            fixedString(admission.header->sessionToken, sizeof(admission.header->sessionToken)),
            admission.roomCode, admission.speakerName, admission.senderKey)) return;

    std::string roomCode;
    if (!extractRoomCode(packet, roomCode)) {
        return;
    }

    const size_t shard = shardIndexForRoom(roomCode);
    if (shard >= voiceInboundQueues_.size()) {
        return;
    }

    VoiceInboundTask task;
    task.packet = std::move(packet);
    task.queuedAt = std::chrono::steady_clock::now();
    voiceInboundQueues_[shard]->push(std::move(task));
}

bool VoiceServer::extractRoomCode(const NetPacket& packet, std::string& outRoomCode) const {
    const char* payload = packet.payloadData();
    const size_t payloadSize = packet.payloadSize();
    if (payload == nullptr || payloadSize < sizeof(VoicePacketHeader)) {
        return false;
    }

    auto* header = reinterpret_cast<const VoicePacketHeader*>(payload);
    const auto typeValue = static_cast<uint8_t>(header->packetType);
    if (typeValue > static_cast<uint8_t>(VoicePacketType::Leave)) {
        return false;
    }

    outRoomCode = fixedString(header->roomCode, sizeof(header->roomCode));
    return true;
}

bool VoiceServer::buildPacket(const NetPacket& packet, ParsedPacket& outPacket) {
    const char* payload = packet.payloadData();
    const size_t payloadSize = packet.payloadSize();
    if (payload == nullptr || payloadSize < sizeof(VoicePacketHeader)) {
        LOG_WARN(tag(), "패킷 크기 부족: " << payloadSize);
        return false;
    }

    auto* header = reinterpret_cast<const VoicePacketHeader*>(payload);
    const auto typeValue = static_cast<uint8_t>(header->packetType);
    if (typeValue > static_cast<uint8_t>(VoicePacketType::Leave)) {
        LOG_WARN(tag(), "알 수 없는 패킷 타입: " << static_cast<int>(typeValue));
        return false;
    }
    const size_t payloadBytes = payloadSize - sizeof(VoicePacketHeader);
    if (header->packetType == VoicePacketType::VoiceData) {
        if (header->payloadSize == 0 || static_cast<size_t>(header->payloadSize) != payloadBytes) {
            LOG_WARN(tag(), "payloadSize 불일치: header=" << header->payloadSize << ", actual=" << payloadBytes);
            return false;
        }
        if (header->payloadSize > Voice::Config::MAX_VOICE_PAYLOAD_SIZE) {
            LOG_WARN(tag(), "voice payload 초과: " << header->payloadSize << " > " << Voice::Config::MAX_VOICE_PAYLOAD_SIZE);
            return false;
        }
    } else {
        if (header->payloadSize != 0 || payloadBytes != 0) {
            LOG_WARN(tag(), "제어 패킷 payload 오류: type=" << static_cast<int>(typeValue)
                     << " header=" << header->payloadSize << " actual=" << payloadBytes);
            return false;
        }
    }

    outPacket = ParsedPacket{
        header,
        payload,
        static_cast<int>(payloadSize),
        packet.sharedData,
        packet.addr,
        makeClientKey(packet.addr),
        fixedString(header->roomCode, sizeof(header->roomCode)),
        fixedString(header->speakerName, sizeof(header->speakerName))
    };
    return true;
}

size_t VoiceServer::shardIndexForRoom(const std::string& roomCode) const {
    return std::hash<std::string>{}(roomCode) % kVoiceWorkerCount;
}

void VoiceServer::startVoiceWorkers() {
    if (voiceWorkersRunning_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    voiceWorkers_.clear();
    voiceInboundQueues_.clear();

    voiceWorkers_.reserve(kVoiceWorkerCount);
    voiceInboundQueues_.reserve(kVoiceWorkerCount);
    for (size_t i = 0; i < kVoiceWorkerCount; ++i) {
        voiceInboundQueues_.push_back(std::make_unique<ConcurrentQueue<VoiceInboundTask>>(Voice::Config::INBOUND_QUEUE_CAPACITY));
    }

    for (size_t i = 0; i < kVoiceWorkerCount; ++i) {
        voiceWorkers_.emplace_back([this, i]() { voiceWorkerLoop(i); });
    }
}

void VoiceServer::stopVoiceWorkers() {
    if (!voiceWorkersRunning_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    for (auto& queue : voiceInboundQueues_) {
        queue->close();
    }
    for (auto& worker : voiceWorkers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    voiceWorkers_.clear();
    voiceInboundQueues_.clear();
}

void VoiceServer::voiceWorkerLoop(size_t workerIndex) {
    if (workerIndex >= voiceInboundQueues_.size()) {
        return;
    }

    VoiceInboundTask task;
    while (voiceInboundQueues_[workerIndex]->pop(task)) {
        const auto age = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - task.queuedAt).count();
        if (Voice::Config::IsQueuedVoiceExpired(age)) continue;
        ParsedPacket parsed{};
        if (!buildPacket(task.packet, parsed)) {
            continue;
        }
        if (!VoiceSessionRegistry::instance().authorize(
                fixedString(parsed.header->sessionToken, sizeof(parsed.header->sessionToken)),
                parsed.roomCode, parsed.speakerName, parsed.senderKey)) continue;
        packetRouter.dispatch(parsed);
    }
}

void VoiceServer::handleJoin(ParsedPacket& packet){
    auto now = std::chrono::steady_clock::now();

    ClientInfo clientInfo{packet.senderAddr, now};
    clientManager.addClient(packet.senderKey, clientInfo);
    clientManager.joinRoom(packet.roomCode, packet.senderKey, clientInfo);

    LOG_INFO(tag(), "클라이언트 Join - 방: " << packet.roomCode << " / 화자: " << packet.speakerName);
}

void VoiceServer::handleVoiceData(ParsedPacket& packet){
    auto now = std::chrono::steady_clock::now();

    // 클라이언트 갱신 (lastSeen 업데이트)
    ClientInfo clientInfo{packet.senderAddr, now};
    clientManager.addClient(packet.senderKey, clientInfo);
    clientManager.joinRoom(packet.roomCode, packet.senderKey, clientInfo);

    thread_local std::vector<std::pair<CLIENT_KEY, ClientInfo>> clientsSnapshot;
    clientManager.getRoomClientsSnapshot(packet.roomCode, clientsSnapshot);

    // Never disclose a sender bearer token to other room participants.
    auto sanitized = std::make_shared<std::vector<char>>(packet.rawBuffer, packet.rawBuffer + packet.rawSize);
    auto* relayHeader = reinterpret_cast<VoicePacketHeader*>(sanitized->data());
    std::fill(std::begin(relayHeader->sessionToken), std::end(relayHeader->sessionToken), 0);
    std::shared_ptr<const std::vector<char>> sharedPayload = sanitized;
    for (const auto& [otherKey, otherClient] : clientsSnapshot) {
        if(otherKey != packet.senderKey &&
           VoiceSessionRegistry::instance().isEndpointAuthorized(packet.roomCode, otherKey)){
            network.sendUdp(otherClient.addr, sharedPayload);
        }
    }

}

void VoiceServer::handleLeave(ParsedPacket& packet){
    if(!clientManager.hasClient(packet.senderKey)){
        LOG_WARN(tag(), "인증되지 않은 클라이언트의 Leave 패킷 수신 - 방: " << packet.roomCode << " / 화자: " << packet.speakerName);
        return;
    }
    clientManager.leaveRoom(packet.senderKey);
    clientManager.removeClient(packet.senderKey);

    LOG_INFO(tag(), "클라이언트 Leave - 방: " << packet.roomCode << " / 화자: " << packet.speakerName);
}
