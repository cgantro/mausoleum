#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/Platform.h"
#include "common/logging/Logger.h"
#include "voice/VoiceConstants.h"
#include "voice/VoicePacketType.h"

#pragma pack(push, 1)
struct VoicePacketHeader {
    VoicePacketType packetType; // 0: Join, 1: Voice Data, 2: Leave
    char roomCode[Voice::Protocol::ROOM_CODE_SIZE];        // 룸 코드 (가변 가능하지만 고정 크기 16바이트로 제한)
    char speakerName[Voice::Protocol::SPEAKER_NAME_SIZE];  // 발화자 이름 고정 (최대 32바이트)
    char sessionToken[65]; // 256-bit lobby-issued bearer token, hex + NUL
    uint32_t payloadSize;     // 뒤에 따라오는 음성 데이터 
};

#pragma pack(pop)

struct ParsedPacket {
    const VoicePacketHeader* header;
    const char* rawBuffer;
    int rawSize;
    std::shared_ptr<const std::vector<char>> sharedPayload;
    sockaddr_in senderAddr;
    uint64_t senderKey;
    std::string roomCode;
    std::string speakerName;
};

class VoicePacketRouter {
public:
    using Handler = std::function<void(ParsedPacket&)>;

    void registerHandler(VoicePacketType packetType, Handler handler);
    void dispatch(ParsedPacket& packet);
private:
    std::unordered_map<VoicePacketType, Handler> handlers;
};
