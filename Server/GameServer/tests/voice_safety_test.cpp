#include "voice/VoiceSessionRegistry.h"
#include "voice/VoiceConstants.h"
#include "Room/RoomRuntimePolicy.h"
#include "common/ConcurrentQueue.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
int main() {
    assert(!Voice::Config::IsQueuedVoiceExpired(99.99));
    assert(!Voice::Config::IsQueuedVoiceExpired(100));
    assert(Voice::Config::IsQueuedVoiceExpired(100.01));
    using A302RoomRuntimePolicy::ShouldCollect;
    assert(!ShouldCollect(0, 119.99, 120, true, 0));
    assert(ShouldCollect(0, 120, 120, true, 0));
    assert(!ShouldCollect(0, 121, 120, true, 1));
    assert(!ShouldCollect(0, 121, 120, true, -1));
    assert(!ShouldCollect(0, 121, 120, false, 0));
    assert(!ShouldCollect(0, 9.99, 0, true, 0));
    assert(ShouldCollect(0, 10, 0, true, 0));
    assert(!ShouldCollect(200, 100, 120, true, 0));
    auto& sessions = VoiceSessionRegistry::instance();
    auto token = sessions.issue("room-a", "alice");
    auto otherToken = sessions.issue("room-b", "bob");
    assert(token.size() == 64 && token != otherToken);
    assert(sessions.authorize(otherToken, "room-b", "bob", 2));
    assert(!sessions.authorize("", "room-a", "alice", 1));
    assert(!sessions.authorize(token, "room-b", "alice", 1));
    assert(!sessions.authorize(token, "room-a", "bob", 1));
    assert(sessions.authorize(token, "room-a", "alice", 1));
    assert(!sessions.authorize(token, "room-a", "alice", 2));
    assert(sessions.isEndpointAuthorized("room-a", 1));
    sessions.revoke(token);
    assert(!sessions.authorize(token, "room-a", "alice", 1));
    assert(!sessions.isEndpointAuthorized("room-a", 1));
    assert(sessions.isEndpointAuthorized("room-b", 2));
    sessions.revoke(otherToken);
    ConcurrentQueue<int> queue(2);
    queue.push(1); queue.push(2); queue.push(3);
    assert(queue.size() == 2 && queue.dropped() == 1);
    int value; assert(queue.pop(value) && value == 2);
    queue.close(); queue.push(4);
    assert(queue.pop(value) && value == 3);
    assert(!queue.pop(value));
    ConcurrentQueue<int> overload(128);
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) producers.emplace_back([&] {
        for (int i = 0; i < 10000; ++i) overload.push(i);
    });
    for (auto& thread : producers) thread.join();
    assert(overload.size() == 128 && overload.dropped() == 40000 - 128);
    overload.close();
    std::cout << "voice session isolation, revocation, endpoint binding and bounded queue passed\n";
}
