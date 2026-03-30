#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include "common/ConcurrentQueue.h"
#include "common/IServer.h"
#include "common/logging/Logger.h"
#include "network/NetworkService.h"
#include "voice/VoiceClientManager.h"
#include "voice/VoiceConstants.h"
#include "voice/VoicePacketRouter.h"
#include "voice/VoicePacketType.h"

class VoiceServer : public IServer{
private:
    struct VoiceInboundTask {
        NetPacket packet;
        std::chrono::steady_clock::time_point queuedAt;
    };

    static constexpr size_t kVoiceWorkerCount = 2;

    VoiceClientManager clientManager;
    VoicePacketRouter packetRouter;
    NetworkService network{Voice::Config::INBOUND_QUEUE_CAPACITY, Voice::Config::OUTBOUND_QUEUE_CAPACITY};
    std::chrono::steady_clock::time_point lastCleanupTime;
    std::vector<std::unique_ptr<ConcurrentQueue<VoiceInboundTask>>> voiceInboundQueues_;
    std::vector<std::thread> voiceWorkers_;
    std::atomic<bool> voiceWorkersRunning_{false};

public:
    VoiceServer();
    void run(int port) override;
    void shutdown() override;
protected:
    const char* tag() const override { return "VoiceServer"; }
    bool initSocket(int port) override { return true; }
    void runLoop() override {}
private:
    void onUdpPacket(NetPacket&& packet);
    bool buildPacket(const NetPacket& packet, ParsedPacket& outPacket);
    bool extractRoomCode(const NetPacket& packet, std::string& outRoomCode) const;
    size_t shardIndexForRoom(const std::string& roomCode) const;
    void startVoiceWorkers();
    void stopVoiceWorkers();
    void voiceWorkerLoop(size_t workerIndex);
    void handleJoin(ParsedPacket& packet);
    void handleVoiceData(ParsedPacket& packet);
    void handleLeave(ParsedPacket& packet);
};
