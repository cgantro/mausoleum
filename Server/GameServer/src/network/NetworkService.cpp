#include "network/NetworkService.h"
#include <algorithm>
#include "common/logging/Logger.h"

NetworkService::NetworkService(size_t inboundCapacity, size_t outboundCapacity)
    : inboundQueue_(inboundCapacity), outboundQueue_(outboundCapacity), io_(connections_, inboundQueue_, outboundQueue_) {}

NetworkService::~NetworkService() {
    stop();
}

bool NetworkService::start() {
    if (running_.exchange(true)) {
        return true;
    }

    if (!io_.configureEndpoints(udpEndpointConfigs_, tcpListenerConfigs_)) {
        running_ = false;
        return false;
    }

    if (!io_.start()) {
        running_ = false;
        return false;
    }

    constexpr size_t workerCount = 2;
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
    return true;
}

void NetworkService::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    inboundQueue_.close();
    outboundQueue_.close();

    io_.stop();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

bool NetworkService::addUdpEndpoint(int port, int maxPacketSize) {
    if (running_) {
        LOG_ERROR("NetworkService", "실행 중에는 UDP 엔드포인트를 등록할 수 없습니다.");
        return false;
    }
    NetworkUdpEndpointConfig cfg;
    cfg.port = port;
    cfg.maxPacketSize = maxPacketSize;
    udpEndpointConfigs_.push_back(cfg);
    return true;
}

bool NetworkService::addTcpListener(int port) {
    if (running_) {
        LOG_ERROR("NetworkService", "실행 중에는 TCP 리스너를 등록할 수 없습니다.");
        return false;
    }
    NetworkTcpListenerConfig cfg;
    cfg.port = port;
    tcpListenerConfigs_.push_back(cfg);
    return true;
}

void NetworkService::registerHandler(NetProtocol protocol, Handler handler) {
    handlers_[protocol] = std::move(handler);
}

void NetworkService::sendUdp(const sockaddr_in& addr, const char* data, size_t size) {
    NetPacket packet;
    packet.protocol = NetProtocol::Udp;
    packet.addr = addr;
    packet.data.assign(data, data + size);
    outboundQueue_.push(std::move(packet));
}

void NetworkService::sendUdp(const sockaddr_in& addr, const std::vector<char>& data) {
    sendUdp(addr, data.data(), data.size());
}

void NetworkService::sendUdp(const sockaddr_in& addr, std::shared_ptr<const std::vector<char>> sharedData) {
    if (!sharedData) {
        return;
    }
    NetPacket packet;
    packet.protocol = NetProtocol::Udp;
    packet.addr = addr;
    packet.sharedData = std::move(sharedData);
    outboundQueue_.push(std::move(packet));
}

void NetworkService::sendTcp(ConnectionId id, const char* data, size_t size) {
    NetPacket packet;
    packet.protocol = NetProtocol::Tcp;
    packet.connectionId = id;
    packet.data.assign(data, data + size);
    outboundQueue_.push(std::move(packet));
}

void NetworkService::sendTcp(ConnectionId id, const std::vector<char>& data) {
    sendTcp(id, data.data(), data.size());
}

void NetworkService::workerLoop() {
    NetPacket packet;
    while (inboundQueue_.pop(packet)) {
        auto it = handlers_.find(packet.protocol);
        if (it != handlers_.end()) {
            it->second(std::move(packet), *this);
        } else {
            LOG_WARN("NetworkService", "핸들러 없음. protocol=" << static_cast<int>(packet.protocol));
        }
    }
}
