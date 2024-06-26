//
// Created by ggam-nyang on 2/26/24.
//

#include "Session.hpp"
#include <utility>
#include "../BattleInfo/BattleInfo.hpp"
#include "../Packet/PacketManager.hpp"
#include "Lobby.hpp"
#include "Room.hpp"
#include "Server.h"

int Session::ID_COUNTER = 0;

Session::pointer Session::create(boost::asio::io_context& io_context,
                                 Server* server) {
  const auto pointer = std::make_shared<Session>(io_context, server);
  ++ID_COUNTER;
  return pointer;
}

Session::Session(boost::asio::io_context& io_context, Server* server)
    : socket_(io_context),
      server_(server),
      battleInfo_(std::make_shared<BattleInfo>()) {}

void Session::Receive() {
  // 비동기로 구현 후, 동작 확인
  socket_.async_read_some(asio::buffer(buffer_, buffer_.size()),
                          [this](const system::error_code& ec, size_t size) {
                            ReceiveHandle(ec, size);
                          });
}

void Session::ReceiveHandle(const boost::system::error_code& ec, size_t size) {
  if (ec) {
    std::cout << "[" << boost::this_thread::get_id()
              << "] Receive failed: " << ec.message() << std::endl;
    server_->closeSession(shared_from_this());
    return;
  }

  if (size == 0) {
    std::cout << "[" << boost::this_thread::get_id() << "] user wants to end "
              << std::endl;
    return;
  }

  server_->packetManager_.ReceivePacket(shared_from_this(), buffer_.data(),
                                        size);

  Receive();
}

void Session::write(char* pBuf, uint16_t pSize) {
  socket_.async_write_some(boost::asio::buffer(pBuf, pSize),
                           [this, pBuf](const boost::system::error_code& ec,
                                        size_t _) { onWrite(ec); });
}

void Session::onWrite(const boost::system::error_code& ec) {
  if (ec) {
    std::cout << "[" << boost::this_thread::get_id()
              << "] async_write_some failed: " << ec.message() << std::endl;
    return;
  }
}

void Session::SetNameReq(SET_NAME_REQUEST_PACKET& packet) {
  auto response = SET_NAME_RESPONSE_PACKET();
  auto username = string(packet.username);
  cout << "username:" << username << endl;

  if (server_->isValidName(username)) {
    name_ = username;

    response.result = 1;
  } else {
    response.result = 0;
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::EnterLobbyReq(LOBBY_ENTER_REQUEST_PACKET& packet) {
  auto response = LOBBY_ENTER_RESPONSE_PACKET();

  if (lobby_ != nullptr) {
    response.result = 0;
  } else {
    response.result = 1;
    lobby_ = server_->lobbies_.begin()->second;  // FIXME: Lobby 여러개로 수정
    state_ = USER_STATE::LOBBY;
    lobby_->Enter(shared_from_this());
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::CreateRoomReq(ROOM_CREATE_REQUEST_PACKET& packet) {
  auto response = ROOM_CREATE_RESPONSE_PACKET();

  if (IsInRoom()) {
    response.result = 0;
  } else {
    response.result =
        lobby_->CreateRoom(shared_from_this(), string(packet.roomName));
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::ListRoomReq(ROOM_LIST_REQUEST_PACKET& packet) {
  auto response = ROOM_LIST_RESPONSE_PACKET();

  if (lobby_ == nullptr) {
    response.result = 0;
    write(reinterpret_cast<char*>(&response), response.PacketLength);
    return;
  }

  auto rooms = lobby_->GetRooms();
  response.result = 1;
  response.roomCount = rooms.size();
  for (int i = 0; i < rooms.size(); ++i) {
    response.roomList[i] =
        ROOM{rooms[i]->name_, static_cast<uint16_t>(rooms[i]->id_)};
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::EnterRoomReq(ROOM_ENTER_REQUEST_PACKET& packet) {
  auto response = ROOM_ENTER_RESPONSE_PACKET();

  if (IsInRoom()) {
    response.result = 0;
  } else if (lobby_ == nullptr) {
    response.result = 2;
  } else {
    response.result = lobby_->EnterRoom(shared_from_this(), packet.roomId);
  }

  if (response.result == 1) {
    auto broadcast_response = ROOM_ENTER_BROADCAST_PACKET();
    strncpy(broadcast_response.username, name_.c_str(), name_.size());

    room_->Broadcast(reinterpret_cast<char*>(&broadcast_response),
                     broadcast_response.PacketLength, shared_from_this());
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::LeaveRoomReq(ROOM_LEAVE_REQUEST_PACKET& packet) {
  auto response = ROOM_LEAVE_RESPONSE_PACKET();

  if (!IsInRoom()) {
    response.result = 0;
  } else if (lobby_ == nullptr) {
    response.result = 2;
  } else {
    response.result = 1;
    state_ = USER_STATE::LOBBY;
  }

  if (response.result == 1) {
    auto broadcast_response = ROOM_LEAVE_BROADCAST_PACKET();
    strncpy(broadcast_response.username, name_.c_str(), name_.size());

    room_->Broadcast(reinterpret_cast<char*>(&broadcast_response),
                     broadcast_response.PacketLength, shared_from_this(), true);
    room_->Leave(shared_from_this());
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::ChatReq(CHAT_REQUEST_PACKET& packet) {
  if (packet.chatType == CHAT_TYPE::LOBBY && (lobby_ == nullptr || IsInRoom()))
    return;
  if (packet.chatType == CHAT_TYPE::ROOM && !IsInRoom())
    return;

  auto response = CHAT_RESPONSE_PACKET();

  response.chatType = packet.chatType;
  strncpy(response.username, name_.c_str(), name_.size());
  strncpy(response.chat, packet.chat, MAX_CHAT_LEN);

  if (packet.chatType == CHAT_TYPE::LOBBY) {
    lobby_->Broadcast(reinterpret_cast<char*>(&response), response.PacketLength,
                      shared_from_this());
  } else {
    room_->Broadcast(reinterpret_cast<char*>(&response), response.PacketLength,
                     shared_from_this());
  }
}

void Session::ReadyReq(ROOM_READY_REQUEST_PACKET& packet) {
  auto response = ROOM_READY_RESPONSE_PACKET();

  if (state_ == USER_STATE::ROOM) {
    response.result = 1;
    state_ = USER_STATE::READY;
  } else if (state_ == USER_STATE::READY) {
    response.result = 1;
    state_ = USER_STATE::ROOM;
  } else {
    response.result = 0;
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}

void Session::close() {
  socket_.close();
  shared_from_this().reset();
}

void Session::setRoom(std::shared_ptr<Room> room) {
  if (room == nullptr)
    state_ = USER_STATE::LOBBY;
  else
    state_ = USER_STATE::ROOM;
  room_ = std::move(room);
}

bool Session::IsInRoom() const {
  return room_ != nullptr;
}

bool Session::IsReady() const {
  return state_ == USER_STATE::READY;
}

void Session::BattleStartReq(BATTLE_START_REQUEST_PACKET& packet) {
  auto response = BATTLE_START_RESPONSE_PACKET();

  if (!IsInRoom()) {
    response.result = 0;
  } else if (!room_->IsOwner(shared_from_this())) {
    response.result = 2;
  } else if (room_->GetClientSize() == 1) {
    response.result = 4;
  } else {
    if (room_->IsReady()) {
      response.result = 1;
      room_->BattleStart();
      room_->Broadcast(reinterpret_cast<char*>(&response),
                       response.PacketLength, shared_from_this(), false);
    } else {
      response.result = 3;
      room_->Broadcast(reinterpret_cast<char*>(&response),
                       response.PacketLength, shared_from_this(),
                       &Session::IsReady, false);
    }
  }
}

void Session::AttackReq(ATTACK_REQUEST_PACKET& packet) {
  auto response = BATTLE_INFO_PACKET();

  if (state_ != USER_STATE::BATTLE)
    response.result = 0;
  else {
    response.result = 1;
    room_->Attack(shared_from_this());

    auto clients = room_->GetClients();

    int i = 0;
    for (const auto& client : clients) {
      response.battleInfos[i] = BATTLE_INFO{
          client->name_, static_cast<uint16_t>(client->battleInfo_->max_hp_),
          static_cast<uint16_t>(client->battleInfo_->hp_)};
      ++i;
    }

    if (room_->IsBattleEnd()) {
      response.result = 2;
      room_->BattleEnd();
      room_->Broadcast(reinterpret_cast<char*>(&response),
                       response.PacketLength, shared_from_this(), false);
      return;
    }
  }

  write(reinterpret_cast<char*>(&response), response.PacketLength);
}