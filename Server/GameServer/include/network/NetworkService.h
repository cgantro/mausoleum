#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>
#include "common/ConcurrentQueue.h"
#include "network/ConnectionRegistry.h"
#include "network/NetworkIO.h"
#include "network/NetworkTypes.h"

class NetworkService {
public:
    using Handler = std::function<void(NetPacket&&, NetworkService&)>;

    explicit NetworkService(size_t inboundCapacity = 0, size_t outboundCapacity = 0);
    ~NetworkService();

    bool start();
    void stop();

    // 네트워크 엔드포인트 등록
    bool addUdpEndpoint(int port, int maxPacketSize = 2048);
    bool addTcpListener(int port);

    // 프로토콜별 핸들러 등록
    void registerHandler(NetProtocol protocol, Handler handler);

    // 응답 전송 요청 (큐에 적재)
    void sendUdp(const sockaddr_in& addr, const char* data, size_t size);
    void sendUdp(const sockaddr_in& addr, const std::vector<char>& data);
    void sendUdp(const sockaddr_in& addr, std::shared_ptr<const std::vector<char>> sharedData);
    void sendTcp(ConnectionId id, const char* data, size_t size);
    void sendTcp(ConnectionId id, const std::vector<char>& data);

private:
    ConcurrentQueue<NetPacket> inboundQueue_;
    ConcurrentQueue<NetPacket> outboundQueue_;
    std::unordered_map<NetProtocol, Handler> handlers_;
    std::vector<NetworkUdpEndpointConfig> udpEndpointConfigs_;
    std::vector<NetworkTcpListenerConfig> tcpListenerConfigs_;

    ConnectionRegistry connections_;
    NetworkIO io_;

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    void workerLoop();
};
