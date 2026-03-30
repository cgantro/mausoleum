#include "Network/GameNetworkSubsystem.h"
#include "Network/UDPHandler.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UGameNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[UGameNetworkSubsystem] Initialized. Lobby=%s Voice=%s GameServer=%s"),
		*GetLobbyURL(),
		*GetVoiceURL(),
		*GetGameServerAddress()
	);

	WebSocketClient = NewObject<UA302WebSocketClient>(this);
	
	if (WebSocketClient)
	{
		WebSocketClient->OnMessageReceived.AddDynamic(this, &UGameNetworkSubsystem::HandleWebSocketMessage);
		WebSocketClient->OnBinaryMessageReceived.AddUObject(this, &UGameNetworkSubsystem::HandleWebSocketBinaryMessage);
	}
	

	UDPHandler = NewObject<UUDPHandler>(this);

	if (UDPHandler)
	{
		UDPHandler->OnMessageReceived.AddDynamic(this, &UGameNetworkSubsystem::HandleUDPMessage);
		UDPHandler->OnBinaryMessageReceived.AddUObject(this, &UGameNetworkSubsystem::HandleUDPBinaryMessage);
	}
}

void UGameNetworkSubsystem::Deinitialize()
{
	if (WebSocketClient)
	{
		VoiceSession.Empty();
		WebSocketClient->Disconnect();
		WebSocketClient = nullptr;
	}

	if (UDPHandler)
	{
		UDPHandler->Disconnect();
		UDPHandler = nullptr;
	}

	Super::Deinitialize();
}

void UGameNetworkSubsystem::Connect(EProtocolType Protocol, const FString& URL)
{
	// 외부(블루프린트, GameInstance 등)에서 넘긴 URL을 무시하고,
	// 공용 코드 상수(FA302NetworkEndpointConfig)로 계산한 URL만 사용합니다.
	if (Protocol == EProtocolType::WebSocket && WebSocketClient)
	{
		FString FinalURL = GetURL(Protocol);
		UE_LOG(LogTemp, Log, TEXT("[UGameNetworkSubsystem] Connecting WebSocket to: %s"), *FinalURL);
		WebSocketClient->Connect(FinalURL, TEXT("[UGameNetworkSubsystem]"));
	}
	else if (Protocol == EProtocolType::UDP && UDPHandler)
	{
		FString FinalURL = GetURL(Protocol);
		UE_LOG(LogTemp, Log, TEXT("[UGameNetworkSubsystem] Connecting UDP to: %s"), *FinalURL);
		UDPHandler->Connect(FinalURL);
	}
}

void UGameNetworkSubsystem::Disconnect(EProtocolType Protocol)
{
	if (Protocol == EProtocolType::WebSocket && WebSocketClient)
	{
		VoiceSession.Empty();
		WebSocketClient->Disconnect();
	}
	else if (Protocol == EProtocolType::UDP && UDPHandler)
	{
		UDPHandler->Disconnect();
	}
}

bool UGameNetworkSubsystem::IsConnected(EProtocolType Protocol) const
{
	if (Protocol == EProtocolType::WebSocket && WebSocketClient)
	{
		return WebSocketClient->IsConnected();
	}
	else if (Protocol == EProtocolType::UDP && UDPHandler)
	{
		return UDPHandler->IsConnected();
	}
	return false;
}

void UGameNetworkSubsystem::SendPacket(EProtocolType Protocol, const FString& Payload)
{
	if (Protocol == EProtocolType::WebSocket)
	{
        if (WebSocketClient && WebSocketClient->IsConnected())
        {
		    WebSocketClient->SendMessage(Payload);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[UGameNetworkSubsystem] Cannot SendPacket. WebSocket not connected."));
        }
	}
	else if (Protocol == EProtocolType::UDP)
	{
        if (UDPHandler && UDPHandler->IsConnected())
        {
		    UDPHandler->SendMessage(Payload);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[UGameNetworkSubsystem] Cannot SendPacket. UDP not connected."));
        }
	}
}

void UGameNetworkSubsystem::SendBinaryPacket(EProtocolType Protocol, const TArray<uint8>& Payload)
{
	if (Protocol == EProtocolType::WebSocket)
	{
        if (WebSocketClient && WebSocketClient->IsConnected())
        {
		    WebSocketClient->SendBinaryMessage(Payload);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[UGameNetworkSubsystem] Cannot SendBinaryPacket. WebSocket not connected."));
        }
	}
	else if (Protocol == EProtocolType::UDP)
	{
        if (UDPHandler && UDPHandler->IsConnected())
        {
		    UDPHandler->SendBinaryMessage(Payload);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[UGameNetworkSubsystem] Cannot SendBinaryPacket. UDP not connected."));
        }
	}
}

void UGameNetworkSubsystem::HandleWebSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> Packet;
    auto Reader = TJsonReaderFactory<>::Create(Message);
    if (FJsonSerializer::Deserialize(Reader, Packet) && Packet.IsValid())
    {
        FString Type;
        const TSharedPtr<FJsonObject>* Data = nullptr;
        if (Packet->TryGetStringField(TEXT("type"), Type) &&
            (Type == TEXT("room_created") || Type == TEXT("room_joined")) &&
            Packet->TryGetObjectField(TEXT("data"), Data))
        {
            VoiceSession.Empty();
            (*Data)->TryGetStringField(TEXT("voiceSession"), VoiceSession);
        }
    }
	OnPacketReceived.Broadcast(Message);
}

void UGameNetworkSubsystem::HandleWebSocketBinaryMessage(const TArray<uint8>& MessageData)
{
	OnWebSocketBinaryPacketReceived.Broadcast(MessageData);
}

void UGameNetworkSubsystem::HandleUDPMessage(const FString& Message)
{
	OnPacketReceived.Broadcast(Message);
}

void UGameNetworkSubsystem::HandleUDPBinaryMessage(const TArray<uint8>& MessageData)
{
	OnUdpBinaryPacketReceived.Broadcast(MessageData);
	OnBinaryPacketReceived.Broadcast(MessageData);
}
