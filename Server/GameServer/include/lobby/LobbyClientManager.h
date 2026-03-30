#pragma once
#include "common/IClientManager.h"
#include <unordered_map>
#include <string>
#include <uwebsockets/App.h>

using WebSocketType = uWS::WebSocket<false, true, int>;

struct LobbyClientInfo {
    std::string roomCode;
    std::string playerName;
    std::string voiceSession;
};

class LobbyClientManager : public IClientManager<WebSocketType*, LobbyClientInfo> {
private:
    std::unordered_map<WebSocketType*, LobbyClientInfo> clients;

public:
    void addClient(WebSocketType* const& key, const LobbyClientInfo& data) override;
    void removeClient(WebSocketType* const& key) override;
    bool hasClient(WebSocketType* const& key) const override;
    size_t clientCount() const override;
    void cleanupStaleClients() override;
    LobbyClientInfo* getClientInfo(WebSocketType* ws);
};
