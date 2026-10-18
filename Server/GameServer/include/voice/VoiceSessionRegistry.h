#pragma once
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

// Shared by the lobby and voice servers in the same process. Tokens never appear
// in broadcasts/logs; they are revoked when the owning lobby connection leaves.
class VoiceSessionRegistry {
    struct Session { std::string room, speaker; uint64_t endpoint = 0; };
    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
public:
    static VoiceSessionRegistry& instance() { static VoiceSessionRegistry registry; return registry; }
    std::string issue(const std::string& room, const std::string& speaker) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::random_device random;
        constexpr char hex[] = "0123456789abcdef";
        std::string token;
        do {
            token.clear();
            for (int i = 0; i < 8; ++i) {
                const auto word = static_cast<uint32_t>(random());
                for (int shift = 28; shift >= 0; shift -= 4) token += hex[(word >> shift) & 15];
            }
        } while (sessions_.count(token));
        sessions_.emplace(token, Session{room, speaker});
        return token;
    }
    void revoke(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_); sessions_.erase(token);
    }
    bool authorize(const std::string& token, const std::string& room,
                   const std::string& speaker, uint64_t endpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end() || it->second.room != room || it->second.speaker != speaker ||
            (it->second.endpoint && it->second.endpoint != endpoint)) return false;
        it->second.endpoint = endpoint;
        return true;
    }
    bool isEndpointAuthorized(const std::string& room, uint64_t endpoint) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [token, session] : sessions_)
            if (session.room == room && session.endpoint == endpoint) return true;
        return false;
    }
};
