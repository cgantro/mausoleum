#include "Voice/Network/VoiceNetworkClient.h"
#include "Network/GameNetworkSubsystem.h"
#include "Voice/Profiling/VoiceProfiler.h"

#pragma pack(push, 1)
struct FVoicePacketHeader {
    uint8_t packetType;       // 0: Join, 1: Voice Data, 2: Leave
    char roomCode[16];        // 룸 코드
    char speakerName[32];     // 발화자 이름
    char sessionToken[65];    // lobby-issued token; stripped from relayed packets
    uint32_t payloadSize;     // 뒤에 따라오는 음성 데이터 
};
#pragma pack(pop)

namespace
{
    constexpr uint32 MaxVoicePayloadBytes = 2048;
}

void UVoiceNetworkClient::Initialize(UObject* OuterObj)
{
    if (!OuterObj) return;

    UGameInstance* GameInstance = OuterObj->GetWorld() ? OuterObj->GetWorld()->GetGameInstance() : nullptr;
    if (GameInstance)
    {
        GameNetworkSubsystem = Cast<UGameNetworkSubsystem>(GameInstance->GetSubsystemBase(UGameNetworkSubsystem::StaticClass()));
        if (GameNetworkSubsystem)
        {
            // UDP 바이너리 수신 리스너
            GameNetworkSubsystem->OnUdpBinaryPacketReceived.AddUObject(this, &UVoiceNetworkClient::HandleBinaryMessage);
        }
    }
}

void UVoiceNetworkClient::Connect(const FString& URL)
{
    if (GameNetworkSubsystem && !URL.IsEmpty())
    {
        if (!GameNetworkSubsystem->IsConnected(EProtocolType::UDP))
        {
            GameNetworkSubsystem->Connect(EProtocolType::UDP, URL);
        }
    }
}

void UVoiceNetworkClient::Disconnect(const FString& RoomCode, const FString& SpeakerName)
{
    SendLeavePacket(RoomCode, SpeakerName);
}

bool UVoiceNetworkClient::IsConnected() const
{
    return GameNetworkSubsystem && GameNetworkSubsystem->IsConnected(EProtocolType::UDP);
}

void UVoiceNetworkClient::SendVoiceData(const TArray<uint8>& Payload, const FString& RoomCode, const FString& Mode, const FString& SpeakerName)
{
    VOICE_PROFILE_NET_SEND();
    if (!GameNetworkSubsystem || !IsConnected()) return;

    // 헤더 값 설정
    FVoicePacketHeader Header;
    FMemory::Memzero(&Header, sizeof(FVoicePacketHeader));
    const FString Session = GameNetworkSubsystem->GetVoiceSession();
    if (Session.Len() != 64) return;
    FTCHARToUTF8 Utf8Session(*Session);
    FMemory::Memcpy(Header.sessionToken, Utf8Session.Get(), 64);

    Header.packetType = (Mode == TEXT("join_voice")) ? 0 : 1;
    
    FTCHARToUTF8 Utf8RoomCode(*RoomCode);
    FMemory::Memcpy(Header.roomCode, Utf8RoomCode.Get(), FMath::Min(Utf8RoomCode.Length(), 15));
    
    FTCHARToUTF8 Utf8SpeakerName(*SpeakerName);
    FMemory::Memcpy(Header.speakerName, Utf8SpeakerName.Get(), FMath::Min(Utf8SpeakerName.Length(), 31));

    if (Header.packetType == 1 && Payload.Num() > static_cast<int32>(MaxVoicePayloadBytes))
    {
        UE_LOG(
            LogVoiceChat,
            Warning,
            TEXT("[Voice] Oversized payload dropped. room=%s speaker=%s payload=%dB max=%uB"),
            *RoomCode,
            *SpeakerName,
            Payload.Num(),
            MaxVoicePayloadBytes
        );
        return;
    }

    Header.payloadSize = static_cast<uint32>(Payload.Num());

    // 헤더 + 페이로드를 하나의 연속된 바이트 배열로 합치기
    TArray<uint8> BinaryData;
    BinaryData.SetNumUninitialized(sizeof(FVoicePacketHeader) + Header.payloadSize);

    FMemory::Memcpy(BinaryData.GetData(), &Header, sizeof(FVoicePacketHeader));

    if (Header.payloadSize > 0)
    {
        FMemory::Memcpy(BinaryData.GetData() + sizeof(FVoicePacketHeader), Payload.GetData(), Header.payloadSize);
    }

    if (Header.payloadSize > 2 || Header.packetType == 1)
    {
        UE_LOG(LogVoiceChat, Verbose, TEXT("Send: room=%s speaker=%s total=%dB payload=%dB"), *RoomCode, *SpeakerName, BinaryData.Num(), Header.payloadSize);
    }
    GameNetworkSubsystem->SendBinaryPacket(EProtocolType::UDP, BinaryData);
}

void UVoiceNetworkClient::SendLeavePacket(const FString& RoomCode, const FString& SpeakerName)
{
    if (!GameNetworkSubsystem || !IsConnected()) return;

    FVoicePacketHeader Header;
    FMemory::Memzero(&Header, sizeof(FVoicePacketHeader));
    const FString Session = GameNetworkSubsystem->GetVoiceSession();
    if (Session.Len() != 64) return;
    FTCHARToUTF8 Utf8Session(*Session);
    FMemory::Memcpy(Header.sessionToken, Utf8Session.Get(), 64);
    Header.packetType = 2; // Leave
    Header.payloadSize = 0;

    FTCHARToUTF8 Utf8RoomCode(*RoomCode);
    FMemory::Memcpy(Header.roomCode, Utf8RoomCode.Get(), FMath::Min(Utf8RoomCode.Length(), 15));

    FTCHARToUTF8 Utf8SpeakerName(*SpeakerName);
    FMemory::Memcpy(Header.speakerName, Utf8SpeakerName.Get(), FMath::Min(Utf8SpeakerName.Length(), 31));

    TArray<uint8> BinaryData;
    BinaryData.SetNumUninitialized(sizeof(FVoicePacketHeader));
    FMemory::Memcpy(BinaryData.GetData(), &Header, sizeof(FVoicePacketHeader));

    UE_LOG(LogVoiceChat, Log, TEXT("[Voice] Leave 패킷 전송 (Room: %s, User: %s)"), *RoomCode, *SpeakerName);
    GameNetworkSubsystem->SendBinaryPacket(EProtocolType::UDP, BinaryData);
}

void UVoiceNetworkClient::HandleBinaryMessage(const TArray<uint8>& BinaryData)
{
    VOICE_PROFILE_NET_RECV();
    if (BinaryData.Num() < sizeof(FVoicePacketHeader)) return;

    const FVoicePacketHeader* Header = reinterpret_cast<const FVoicePacketHeader*>(BinaryData.GetData());
    
    // Voice Data 패킷(1)만 처리
    if (Header->packetType != 1) return;

    if (Header->payloadSize == 0 || Header->payloadSize > MaxVoicePayloadBytes) return;

    const int32 ExpectedPacketSize = static_cast<int32>(sizeof(FVoicePacketHeader) + Header->payloadSize);
    if (BinaryData.Num() != ExpectedPacketSize) return;

    char RoomCodeAnsi[17] = {};
    FMemory::Memcpy(RoomCodeAnsi, Header->roomCode, 16);
    const FString RoomCode = UTF8_TO_TCHAR(RoomCodeAnsi);

    char SpeakerAnsi[33] = {};
    FMemory::Memcpy(SpeakerAnsi, Header->speakerName, 32);
    const FString Speaker = UTF8_TO_TCHAR(SpeakerAnsi);

    if (RoomCode.IsEmpty() || Speaker.IsEmpty()) return;

    const uint8_t* PayloadStart = BinaryData.GetData() + sizeof(FVoicePacketHeader);

    TArray<uint8> PayloadArray;
    PayloadArray.Append(PayloadStart, static_cast<int32>(Header->payloadSize));

    OnBinaryPacketReceived.ExecuteIfBound(RoomCode, Speaker, PayloadArray);
}
