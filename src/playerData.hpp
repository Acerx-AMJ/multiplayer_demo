#pragma once
#include <CX/Color.hpp>
#include <CX/Vector/Vec2.hpp>
#include <SFML/Network/Packet.hpp>

struct PlayerData {
   int id;
   cx::Vec2f position;
   int textureId;
   bool alive = true;
   bool registering = false;

   inline friend sf::Packet& operator<<(sf::Packet& packet, const PlayerData& data) {
      return packet << data.id << data.position.x << data.position.y << data.textureId << data.alive << data.registering;
   }

   inline friend sf::Packet& operator>>(sf::Packet& packet, PlayerData& data) {
      return packet >> data.id >> data.position.x >> data.position.y >> data.textureId >> data.alive >> data.registering;
   }
};

struct Message {
   int sender;
   std::string message;

   inline friend sf::Packet& operator<<(sf::Packet& packet, const Message& msg) {
      return packet << msg.sender << msg.message;
   }

   inline friend sf::Packet& operator>>(sf::Packet& packet, Message& msg) {
      return packet >> msg.sender >> msg.message;
   }
};
