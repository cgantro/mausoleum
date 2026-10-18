#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Network/A302NetworkEndpointConfig.h"
#include "Network/A302WebSocketClient.h"
#include "GameNetworkSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProtocolType : uint8
{
    WebSocket UMETA(DisplayName = "WebSocket (TCP)"),
    UDP UMETA(DisplayName = "UDP/RUDP")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPacketReceived, const FString&, Message);

// C++ 전용 델리게이트 (바이너리 패킷/Protobuf 등 수신용)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBinaryPacketReceived, const TArray<uint8>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnUdpBinaryPacketReceived, const TArray<uint8>&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWebSocketBinaryPacketReceived, const TArray<uint8>&);

/**
 * 전역 네트워크 상태를 관리하는 서브시스템 (GameInstance 종속)
 */
UCLASS()
class A302CLIENT_API UGameNetworkSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Network")
	FString GetLobbyURL() const { return GetURL(EProtocolType::WebSocket); }

	UFUNCTION(BlueprintPure, Category = "Network")
	FString GetVoiceURL() const { return GetURL(EProtocolType::UDP); }

	UFUNCTION(BlueprintPure, Category = "Network|GameServer")
	FString GetGameServerIP() const { return FA302NetworkEndpointConfig::GetGameServerHost(); }

	UFUNCTION(BlueprintPure, Category = "Network|GameServer")
	FString GetGameServerAddress() const
	{
		return FA302NetworkEndpointConfig::GetGameServerAddress();
	}

	UFUNCTION(BlueprintPure, Category = "Network")
	FString GetURL(EProtocolType Protocol) const
	{
		if (Protocol == EProtocolType::WebSocket)
		{
			return FA302NetworkEndpointConfig::GetLobbyWebSocketURL();
		}
		return FA302NetworkEndpointConfig::GetVoiceAddress();
	}

	// 연결 관리
	UFUNCTION(BlueprintCallable, Category = "Network")
	void Connect(EProtocolType Protocol, const FString& URL);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Disconnect(EProtocolType Protocol);

	UFUNCTION(BlueprintCallable, Category = "Network")
	bool IsConnected(EProtocolType Protocol) const;

	// 패킷 통신
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendPacket(EProtocolType Protocol, const FString& Payload);

	void SendBinaryPacket(EProtocolType Protocol, const TArray<uint8>& Payload);

	FString GetVoiceSession() const { return VoiceSession; }

	// 이벤트
	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnPacketReceived OnPacketReceived;

	FOnBinaryPacketReceived OnBinaryPacketReceived;
	FOnUdpBinaryPacketReceived OnUdpBinaryPacketReceived;
	FOnWebSocketBinaryPacketReceived OnWebSocketBinaryPacketReceived;

private:
	FString VoiceSession;
	UPROPERTY()
	TObjectPtr<UA302WebSocketClient> WebSocketClient;

	UPROPERTY()
	class UUDPHandler* UDPHandler;

	UFUNCTION()
	void HandleWebSocketMessage(const FString& Message);

	void HandleWebSocketBinaryMessage(const TArray<uint8>& MessageData);

	UFUNCTION()
	void HandleUDPMessage(const FString& Message);

	void HandleUDPBinaryMessage(const TArray<uint8>& MessageData);
};
