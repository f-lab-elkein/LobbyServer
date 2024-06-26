cmake_minimum_required(VERSION 3.26)
project(LobbyServer)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost
        COMPONENTS
        thread
        chrono
        REQUIRED
)
if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    #    link_directories(${Boost_LIBRARY_DIRS})
    #    add_definitions( "-DHAS_BOOST" )
    #    Message(STATUS "Boost found")
endif ()

add_executable(LobbyServer
        server/serverMain.cpp
        server/Server.cpp
        server/Server.h
        server/Session.cpp
        server/Lobby.cpp
        server/Lobby.hpp
        server/Room.cpp
        server/Room.hpp
        Packet/Packet.hpp
        Packet/PacketManager.cpp
        Packet/PacketManager.hpp
        BattleInfo/BattleInfo.cpp
        BattleInfo/BattleInfo.hpp
        BattleInfo/BattleManager.cpp
        BattleInfo/BattleManager.hpp
)

add_executable(LobbyClient
        client/clientMain.cpp
        client/Client.cpp
        client/Client.h
        Packet/Packet.hpp
        client/ClientPacketManager.cpp
        client/ClientPacketManager.hpp
        BattleInfo/BattleInfo.cpp
        BattleInfo/BattleInfo.hpp
        BattleInfo/BattleManager.cpp
        BattleInfo/BattleManager.hpp
)

target_link_libraries(LobbyServer ${Boost_LIBRARIES})
target_link_libraries(LobbyClient ${Boost_LIBRARIES})
add_executable(test temp.cpp)
