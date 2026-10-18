#include "lobby/LobbyClientManager.h"
#include "voice/VoiceSessionRegistry.h"

void LobbyClientManager::addClient(WebSocketType* const& key, const LobbyClientInfo& data) {
    removeClient(key);
    auto info = data;
    info.voiceSession = VoiceSessionRegistry::instance().issue(data.roomCode, data.playerName);
    clients[key] = std::move(info);
}

void LobbyClientManager::removeClient(WebSocketType* const& key) {
    auto it = clients.find(key);
    if (it != clients.end()) VoiceSessionRegistry::instance().revoke(it->second.voiceSession);
    clients.erase(key);
}

bool LobbyClientManager::hasClient(WebSocketType* const& key) const {
    return clients.count(key) > 0;
}

size_t LobbyClientManager::clientCount() const {
    return clients.size();
}

void LobbyClientManager::cleanupStaleClients() {
    // WebSocket은 onDisconnect 이벤트로 관리되므로 스케줄링 기반 Stale 제거 불필요
}

LobbyClientInfo* LobbyClientManager::getClientInfo(WebSocketType* ws) {
    auto it = clients.find(ws);
    if (it != clients.end()) return &it->second;
    return nullptr;
}
