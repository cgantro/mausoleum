#include "lobby/handlers/RoomHandler.h"
#include "common/logging/Logger.h"
#include "lobby/LobbyConstants.h"

using namespace Lobby::Protocol;
using namespace Lobby::Errors;

auto RoomHandler::createSendFunc(WebSocketType *ws)
{
    return [ws](const std::string &msg)
    {
        ws->send(msg, uWS::OpCode::TEXT);
    };
}

void RoomHandler::sendError(WebSocketType *ws, const std::string &message)
{
    ws->send(json({{std::string(KEY_TYPE), RES_ERROR},
                   {std::string(KEY_DATA), {{std::string(KEY_MESSAGE), message}}}})
                 .dump(),
             uWS::OpCode::TEXT);
}

bool RoomHandler::checkNameAndSendErrorIfTaken(WebSocketType *ws, const std::string &playerName)
{
    if (playerName.empty() || playerName.size() >= 32)
    {
        sendError(ws, "Player name must contain 1-31 UTF-8 bytes");
        return true;
    }
    if (roomManager.isNameTakenGlobal(playerName))
    {
        sendError(ws, std::string(NAME_TAKEN));
        return true;
    }
    return false;
}

bool RoomHandler::extractContext(const json &data, std::string &outRoomCode, std::string &outPlayerName, Room *&outRoom)
{
    outRoomCode = data.value(KEY_ROOM_CODE, "");
    outPlayerName = data.value(KEY_PLAYER_NAME, "");
    outRoom = roomManager.getRoom(outRoomCode);
    return outRoom != nullptr;
}

void RoomHandler::handleCreateRoom(WebSocketType *ws, const json &data)
{
    std::string playerName = data.value(KEY_PLAYER_NAME, "");
    if (clientManager.hasClient(ws)) { sendError(ws, "Leave current room before joining another"); return; }

    if (checkNameAndSendErrorIfTaken(ws, playerName))
        return;

    Room &room = roomManager.createRoom();
    room.setHostId(playerName);
    room.addPlayer(Player(playerName, createSendFunc(ws)));
    clientManager.addClient(ws, {room.getRoomCode(), playerName});

    LOG_INFO("Lobby", "[" << room.getRoomCode() << "] " << playerName << " 님 방 생성 (방장)");

    ws->send(json({{std::string(KEY_TYPE), RES_ROOM_CREATED},
                   {std::string(KEY_DATA), {{std::string(KEY_ROOM_CODE), room.getRoomCode()}, {std::string(KEY_PLAYER_NAME), playerName}, {"voiceSession", clientManager.getClientInfo(ws)->voiceSession}, {std::string(KEY_IS_HOST), true}}}})
                 .dump(),
             uWS::OpCode::TEXT);
}

void RoomHandler::handleJoinRoom(WebSocketType *ws, const json &data)
{
    std::string roomCode = data.value(KEY_ROOM_CODE, "");
    std::string playerName = data.value(KEY_PLAYER_NAME, "");
    if (clientManager.hasClient(ws)) { sendError(ws, "Leave current room before joining another"); return; }

    if (!roomManager.roomExists(roomCode))
    {
        sendError(ws, std::string(ROOM_NOT_FOUND));
        return;
    }

    if (checkNameAndSendErrorIfTaken(ws, playerName))
        return;

    Room *room = roomManager.getRoom(roomCode);
    if (!room) { clientManager.removeClient(ws); return; }
    if (room)
    {
        room->addPlayer(Player(playerName, createSendFunc(ws)));
        clientManager.addClient(ws, {roomCode, playerName});

        LOG_INFO("Lobby", "[" << roomCode << "] " << playerName << " 님 입장");

        // 1. 기존 유저 배열 모으기
        json existingPlayersArray = json::array();
        for (const auto &[name, p] : room->getPlayers())
        {
            existingPlayersArray.push_back({{std::string(KEY_PLAYER_NAME), name},
                                            {std::string(KEY_IS_HOST), name == room->getHostId()},
                                            {"isReady", p.isReady()}});
        }
        // 2. 현재 방 정보 전송
        ws->send(json({{std::string(KEY_TYPE), RES_ROOM_JOINED},
                       {std::string(KEY_DATA),
                        {
                            {std::string(KEY_ROOM_CODE), roomCode},
                            {std::string(KEY_PLAYER_NAME), playerName},
                            {std::string(KEY_IS_HOST), room->getHostId() == playerName},
                            {"voiceSession", clientManager.getClientInfo(ws)->voiceSession},
                            {"existingPlayers", existingPlayersArray} // 배열 탑재
                        }}})
                     .dump(),
                 uWS::OpCode::TEXT);
        // 3. 브로드캐스트 (나에게는 안 보냄 -> 중복 방지)
        room->broadcastExcept(playerName, {{std::string(KEY_TYPE), RES_PLAYER_ENTERED},
                                           {std::string(KEY_DATA), {{std::string(KEY_PLAYER_NAME), playerName}}}});
    }
}

void RoomHandler::handleGetRoomList(WebSocketType *ws, const json &data)
{
    json roomList = json::array();

    for (const auto &[roomCode, room] : roomManager.getAllRooms())
    {
        if (room.getPhase() == GamePhase::Waiting)
        {
            roomList.push_back({{std::string(KEY_ROOM_CODE), roomCode},
                                {std::string(KEY_PLAYER_COUNT), room.getPlayerCount()}});
        }
    }

    ws->send(json({{std::string(KEY_TYPE), RES_ROOM_LIST},
                   {std::string(KEY_DATA), {{std::string(KEY_ROOMS), roomList}}}})
                 .dump(),
             uWS::OpCode::TEXT);
}

void RoomHandler::handleCheckNickname(WebSocketType *ws, const json &data)
{
    std::string playerName = data.value(KEY_PLAYER_NAME, "");

    if (checkNameAndSendErrorIfTaken(ws, playerName))
        return;

    LOG_INFO("Lobby", "Sending nickname_available to " << playerName);
    ws->send(json({{std::string(KEY_TYPE), RES_NICKNAME_AVAILABLE},
                   {std::string(KEY_DATA), {{std::string(KEY_PLAYER_NAME), playerName}}}})
                 .dump(),
             uWS::OpCode::TEXT);
}

void RoomHandler::handleLeaveRoom(WebSocketType *ws, const json &data)
{
    std::string roomCode, playerName;
    Room *room = nullptr;

    const auto* owner = clientManager.getClientInfo(ws);
    if (!owner) return;
    roomCode = owner->roomCode;
    playerName = owner->playerName;
    room = roomManager.getRoom(roomCode);
    if (!room) { clientManager.removeClient(ws); return; }
    if (room)
    {
        bool bHostChanged = room->removePlayer(playerName);
        clientManager.removeClient(ws);
        LOG_INFO("Lobby", "[" << roomCode << "] " << playerName << " 님 방에서 퇴장");
        // 1. 단순 캐릭터 퇴장 알림 브로드캐스트
        room->broadcast({{std::string(KEY_TYPE), RES_PLAYER_LEFT},
                         {std::string(KEY_DATA), {{std::string(KEY_PLAYER_NAME), playerName}}}});
        // 2. 방장이 새로 뽑혔다면 클라이언트들에게 알림!
        if (bHostChanged && room->getPlayerCount() > 0)
        {
            room->broadcast({{std::string(KEY_TYPE), "host_changed"},
                             {std::string(KEY_DATA), {{"hostName", room->getHostId()}}}});
        }
        roomManager.removeRoomIfEmpty(roomCode);
    }
}
