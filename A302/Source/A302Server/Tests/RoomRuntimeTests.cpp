#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Room/A302RoomRuntimeSubsystem.h"
#include "Phase/A302ServerPhaseSubsystem.h"
#include "GameData/RewardTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FA302RoomRuntimeIsolationTest,
    "A302.RoomRuntime.IndependentLifecycle",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FA302RoomRuntimeIsolationTest::RunTest(const FString& Parameters)
{
    UGameInstance* Instance = NewObject<UGameInstance>();
    auto* Runtime = NewObject<UA302RoomRuntimeSubsystem>(Instance);
    TestFalse(TEXT("Empty room rejected"), Runtime->PrepareRoom(TEXT("  ")));
    // Without a world this uses the documented offset-only fallback.
    TestTrue(TEXT("A prepares"), Runtime->PrepareRoom(TEXT("room-a")));
    TestTrue(TEXT("B prepares"), Runtime->PrepareRoom(TEXT("room-b")));
    TestTrue(TEXT("Normalized key is ready"), Runtime->IsRoomLevelReady(TEXT(" ROOM-A ")));
    TestTrue(TEXT("A stops"), Runtime->StopRoomRuntime(TEXT("room-a")));
    TestFalse(TEXT("A no longer ready"), Runtime->IsRoomLevelReady(TEXT("room-a")));
    TestTrue(TEXT("B remains ready"), Runtime->IsRoomLevelReady(TEXT("room-b")));
    TestFalse(TEXT("Repeated stop is idempotent"), Runtime->StopRoomRuntime(TEXT("room-a")));
    TestTrue(TEXT("A can be prepared again"), Runtime->PrepareRoom(TEXT("room-a")));
    Runtime->StopRoomRuntime(TEXT("room-a"));
    Runtime->StopRoomRuntime(TEXT("room-b"));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FA302RoomPhaseIsolationTest,
    "A302.RoomRuntime.IndependentPhaseProgress",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FA302RoomPhaseIsolationTest::RunTest(const FString& Parameters)
{
    UGameInstance* Instance = NewObject<UGameInstance>();
    auto* Phases = NewObject<UA302ServerPhaseSubsystem>(Instance);
    TestFalse(TEXT("Empty phase room rejected"), Phases->StartRoomPhaseTimeline(TEXT(" ")));
    TestTrue(TEXT("Start A"), Phases->StartRoomPhaseTimeline(TEXT("room-a")));
    TestTrue(TEXT("Start B"), Phases->StartRoomPhaseTimeline(TEXT("room-b")));
    Phases->NotifyRoomRewardResolved(TEXT("room-a"), nullptr, ERewardCategory::BasicItem);
    FA302RoomPhaseState A, B;
    TestTrue(TEXT("A state exists"), Phases->TryGetRoomPhaseState(TEXT("ROOM-A"), A));
    TestTrue(TEXT("B state exists"), Phases->TryGetRoomPhaseState(TEXT("ROOM-B"), B));
    TestEqual(TEXT("Only A receives reward"), A.Phase0ItemCount, 1);
    TestEqual(TEXT("B progress independent"), B.Phase0ItemCount, 0);
    TestTrue(TEXT("Stop A"), Phases->StopRoomPhaseTimeline(TEXT("room-a")));
    TestTrue(TEXT("B timeline remains active"), Phases->IsRoomPhaseActive(TEXT("room-b")));
    TestFalse(TEXT("A timeline removed"), Phases->IsRoomPhaseActive(TEXT("room-a")));
    TestFalse(TEXT("Repeated A stop"), Phases->StopRoomPhaseTimeline(TEXT("room-a")));
    Phases->StopRoomPhaseTimeline(TEXT("room-b"));
    return true;
}
#endif
