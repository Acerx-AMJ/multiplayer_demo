#ifndef NETWORKING_PLAYER_DATA_HPP
#define NETWORKING_PLAYER_DATA_HPP

#include <CX/Color.hpp>
#include <CX/Vector/Vec2.hpp>
#include <SFML/Network/Packet.hpp>

struct PlayerData
{
   int id;
   cx::Vec2f position;
   cx::Color color;
   bool alive = true;

   inline friend sf::Packet& operator<<(sf::Packet& packet, const PlayerData& data)
   {
      return packet << data.id << data.position.x << data.position.y << data.color.r << data.color.g << data.color.b << data.alive;
   }

   inline friend sf::Packet& operator>>(sf::Packet& packet, PlayerData& data)
   {
      return packet >> data.id >> data.position.x >> data.position.y >> data.color.r >> data.color.g >> data.color.b >> data.alive;
   }
};

struct Message
{
   int sender;
   std::string message;

   inline friend sf::Packet& operator<<(sf::Packet& packet, const Message& msg)
   {
      return packet << msg.sender << msg.message;
   }

   inline friend sf::Packet& operator>>(sf::Packet& packet, Message& msg)
   {
      return packet >> msg.sender >> msg.message;
   }
};

#endif // NETWORKING_PLAYER_DATA_HPP
