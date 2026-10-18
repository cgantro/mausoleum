#include "Room/A302RoomRuntimeSubsystem.h"

#include "Room/RoomWorldOffset.h"
#include "Room/RoomRuntimePolicy.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UA302RoomRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    EnsureTickTimer();
}

void UA302RoomRuntimeSubsystem::Deinitialize()
{
    if (UWorld* World = ResolveWorld())
    {
        World->GetTimerManager().ClearTimer(RuntimeTickTimerHandle);
    }

    for (TPair<FString, FA302RoomRuntimeState>& Pair : RoomRuntimeStates)
    {
        if (ULevelStreamingDynamic* Streaming = Pair.Value.StreamingLevel.Get())
        {
            Streaming->SetIsRequestingUnloadAndRemoval(true);
        }
    }

    RoomRuntimeStates.Reset();
    RoomPopulationResolver = nullptr;
    Super::Deinitialize();
}

bool UA302RoomRuntimeSubsystem::PrepareRoom(const FString& RoomCode)
{
    const FString NormalizedRoomCode = A302RoomWorldOffset::NormalizeRoomCode(RoomCode);
    if (NormalizedRoomCode.IsEmpty())
    {
        return false;
    }

    FA302RoomRuntimeState& RoomState = RoomRuntimeStates.FindOrAdd(NormalizedRoomCode);
    RoomState.RoomCode = NormalizedRoomCode;
    RoomState.RoomOffset = ResolveRoomOffset(NormalizedRoomCode);
    RoomState.LastTouchedServerTime = FPlatformTime::Seconds();

    if (RoomState.bLevelReady)
    {
        return true;
    }

    if (!RoomState.bLevelInstanceRequested)
    {
        const bool bRequested = TryRequestLevelInstance(RoomState);
        if (!bRequested)
        {
            // If level instance creation failed, fallback to offset-only mode.
            RoomState.bLevelReady = true;
            RoomLevelReadyDelegate.Broadcast(NormalizedRoomCode);
            return true;
        }
    }

    EnsureTickTimer();
    return RoomState.bLevelReady;
}

bool UA302RoomRuntimeSubsystem::IsRoomLevelReady(const FString& RoomCode) const
{
    const FA302RoomRuntimeState* State = RoomRuntimeStates.Find(A302RoomWorldOffset::NormalizeRoomCode(RoomCode));
    return State ? State->bLevelReady : false;
}

FVector UA302RoomRuntimeSubsystem::ResolveRoomOffset(const FString& RoomCode) const
{
    return A302RoomWorldOffset::ResolveRoomOffset(A302RoomWorldOffset::NormalizeRoomCode(RoomCode));
}

void UA302RoomRuntimeSubsystem::TouchRoom(const FString& RoomCode)
{
    const FString NormalizedRoomCode = A302RoomWorldOffset::NormalizeRoomCode(RoomCode);
    if (FA302RoomRuntimeState* RoomState = RoomRuntimeStates.Find(NormalizedRoomCode))
    {
        RoomState->LastTouchedServerTime = FPlatformTime::Seconds();
    }
}

bool UA302RoomRuntimeSubsystem::StopRoomRuntime(const FString& RoomCode)
{
    const FString NormalizedRoomCode = A302RoomWorldOffset::NormalizeRoomCode(RoomCode);
    if (NormalizedRoomCode.IsEmpty())
    {
        return false;
    }

    FA302RoomRuntimeState RoomState;
    if (!RoomRuntimeStates.RemoveAndCopyValue(NormalizedRoomCode, RoomState))
    {
        return false;
    }

    if (ULevelStreamingDynamic* Streaming = RoomState.StreamingLevel.Get())
    {
        Streaming->SetIsRequestingUnloadAndRemoval(true);
    }

    UE_LOG(LogTemp, Log, TEXT("[RoomRuntimeSubsystem] Stopped room runtime. room=%s"), *NormalizedRoomCode);
    return true;
}

void UA302RoomRuntimeSubsystem::SetRoomPopulationResolver(TFunction<int32(const FString&)>&& InResolver)
{
    RoomPopulationResolver = MoveTemp(InResolver);
}

void UA302RoomRuntimeSubsystem::TickRoomRuntimes()
{
    if (RoomRuntimeStates.Num() == 0)
    {
        if (UWorld* World = ResolveWorld())
        {
            World->GetTimerManager().ClearTimer(RuntimeTickTimerHandle);
        }
        return;
    }

    const double CurrentServerTime = FPlatformTime::Seconds();
    TArray<FString> ZombieRooms;

    for (TPair<FString, FA302RoomRuntimeState>& Pair : RoomRuntimeStates)
    {
        FA302RoomRuntimeState& RoomState = Pair.Value;

        if (!RoomState.bLevelReady)
        {
            if (ULevelStreamingDynamic* Streaming = RoomState.StreamingLevel.Get())
            {
                if (Streaming->HasLoadedLevel() && Streaming->IsLevelVisible())
                {
                    RoomState.bLevelReady = true;
                    RoomState.LastTouchedServerTime = CurrentServerTime;
                    RoomLevelReadyDelegate.Broadcast(RoomState.RoomCode);
                    UE_LOG(LogTemp, Log, TEXT("[RoomRuntimeSubsystem] Room level is ready. room=%s"), *RoomState.RoomCode);
                }
            }
            else if (RoomState.bLevelInstanceRequested)
            {
                // Requested but invalid streaming ptr -> fallback ready.
                RoomState.bLevelReady = true;
                RoomState.LastTouchedServerTime = CurrentServerTime;
                RoomLevelReadyDelegate.Broadcast(RoomState.RoomCode);
            }
        }

        if (ShouldCollectZombieRoom(RoomState, CurrentServerTime))
        {
            ZombieRooms.Add(RoomState.RoomCode);
        }
    }

    for (const FString& ZombieRoom : ZombieRooms)
    {
        StopRoomRuntime(ZombieRoom);
    }
}

void UA302RoomRuntimeSubsystem::EnsureTickTimer()
{
    UWorld* World = ResolveWorld();
    if (!World)
    {
        return;
    }

    const float SafePollInterval = FMath::Max(0.1f, RuntimePollInterval);
    if (!World->GetTimerManager().IsTimerActive(RuntimeTickTimerHandle))
    {
        World->GetTimerManager().SetTimer(
            RuntimeTickTimerHandle,
            this,
            &UA302RoomRuntimeSubsystem::TickRoomRuntimes,
            SafePollInterval,
            true
        );
    }
}

UWorld* UA302RoomRuntimeSubsystem::ResolveWorld() const
{
    const UGameInstance* GameInstance = GetGameInstance();
    return GameInstance ? GameInstance->GetWorld() : nullptr;
}

bool UA302RoomRuntimeSubsystem::TryRequestLevelInstance(FA302RoomRuntimeState& RoomState)
{
    UWorld* World = ResolveWorld();
    if (!World)
    {
        return false;
    }

    if (TemplateLevelPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[RoomRuntimeSubsystem] TemplateLevelPath is empty. Use offset-only runtime. room=%s"), *RoomState.RoomCode);
        return false;
    }

    const FString LevelInstanceNameOverride = A302RoomWorldOffset::BuildLevelInstanceNameOverride(RoomState.RoomCode);
    bool bLoadRequested = false;
    ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstance(
        World,
        TemplateLevelPath,
        RoomState.RoomOffset,
        FRotator::ZeroRotator,
        bLoadRequested,
        LevelInstanceNameOverride
    );

    if (!bLoadRequested || !StreamingLevel)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[RoomRuntimeSubsystem] Failed to request level instance. room=%s map=%s instance=%s offset=(%.0f, %.0f, %.0f)"),
            *RoomState.RoomCode,
            *TemplateLevelPath,
            *LevelInstanceNameOverride,
            RoomState.RoomOffset.X,
            RoomState.RoomOffset.Y,
            RoomState.RoomOffset.Z
        );
        return false;
    }

    RoomState.StreamingLevel = StreamingLevel;
    RoomState.bLevelInstanceRequested = true;
    RoomState.bLevelReady = StreamingLevel->HasLoadedLevel() && StreamingLevel->IsLevelVisible();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[RoomRuntimeSubsystem] Level instance requested. room=%s map=%s instance=%s offset=(%.0f, %.0f, %.0f)"),
        *RoomState.RoomCode,
        *TemplateLevelPath,
        *LevelInstanceNameOverride,
        RoomState.RoomOffset.X,
        RoomState.RoomOffset.Y,
        RoomState.RoomOffset.Z
    );

    if (RoomState.bLevelReady)
    {
        RoomLevelReadyDelegate.Broadcast(RoomState.RoomCode);
    }

    return true;
}

bool UA302RoomRuntimeSubsystem::ShouldCollectZombieRoom(const FA302RoomRuntimeState& RoomState, double CurrentServerTime) const
{
    if (!RoomPopulationResolver ||
        CurrentServerTime - RoomState.LastTouchedServerTime < FMath::Max(10.0f, ZombieRoomTimeoutSeconds)) return false;
    return A302RoomRuntimePolicy::ShouldCollect(
        RoomState.LastTouchedServerTime, CurrentServerTime, ZombieRoomTimeoutSeconds,
        true, RoomPopulationResolver(RoomState.RoomCode));
}
