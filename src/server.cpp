#include <SFML/Network.hpp>
#include "io.hpp"
#include "playerData.hpp"
#include <memory>
#include <unordered_map>

using ID = int;
using Port = unsigned short;
using Address = sf::IpAddress;
using Color = cx::Color;
using Vector2 = cx::Vec2f;

// Colors
const std::vector<Color> colors
{
   Color::blue(), Color::yellow(), Color::cyan(), Color::green(), Color::magenta(),
   Color::orange(), Color::pink(), Color::purple(), Color::red()
};

int main()
{
   srand(time(nullptr));

   // Get the server port from the user
   Port port = getInteger("Input server port: "s);

   // Setup the UDP sockets
   sf::UdpSocket UDPsocket;
   UDPsocket.setBlocking(false);

   std::unordered_map<ID, std::pair<Address, Port>> UDPclients;
   std::unordered_map<ID, std::pair<Vector2, Color>> players;
   ID nextID = 1;

   sf::Packet senderPacket;
   Address senderAddress;
   Port senderPort;

   if (UDPsocket.bind(port, sf::IpAddress::Any) != sf::Socket::Status::Done)
      throw std::runtime_error("Could not bind UDP to port.");

   std::cout << "UDP Server initialized on port " << port << "." << std::endl;

   // Setup the TCP sockets
   sf::TcpListener listener;

   if (listener.listen(port, sf::IpAddress::Any) != sf::Socket::Status::Done)
      throw std::runtime_error("Could not bind TCP to port.");

   std::vector<std::unique_ptr<sf::TcpSocket>> TCPclients;
   std::unordered_map<sf::TcpSocket*, ID> TCPids;
   sf::SocketSelector TCPselector;
   TCPselector.add(listener);
   listener.setBlocking(false);

   std::cout << "TCP Listener initialized on port " << port << "." << std::endl;

   // Main server loop
   while (true)
   {
      // Check if any chat messages can be received
      if (TCPselector.wait(sf::milliseconds(1)))
      {
         if (TCPselector.isReady(listener))
         {
            auto socket = std::make_unique<sf::TcpSocket>();
            if (listener.accept(*socket) != sf::Socket::Status::Done)
               throw std::runtime_error("Could not accept socket!");

            TCPselector.add(*socket);
            TCPclients.push_back(std::move(socket));

            std::cout << "New TCP socket connected.\n";
         }

         // Accept and echo messages
         for (auto it = TCPclients.begin(); it != TCPclients.end();)
         {
            if (TCPselector.isReady(**it))
            {
               sf::Packet packet;

               if ((*it)->receive(packet) == sf::Socket::Status::Done)
               {
                  Message message;
                  packet >> message;

                  TCPids[it->get()] = message.sender;

                  if (message.message.size() == 0)
                  {
                     ++it;
                     continue;
                  }

                  std::cout << "Chat message from ID " << message.sender << ": " << message.message << std::endl;

                  sf::Packet echoPacket;
                  echoPacket << message;

                  for (auto& chatSocket : TCPclients)
                     if (chatSocket.get() != it->get())
                        if (chatSocket->send(echoPacket) != sf::Socket::Status::Done)
                           std::cout << "Failed to echo user's message!" << std::endl;
                  
                  ++it;
               }
               else
               {
                  // Send "Death" packages
                  sf::Packet endPacket;
                  endPacket << PlayerData{TCPids.at(it->get()), Vector2(), Color(), false};

                  UDPclients.erase(TCPids.at(it->get()));
                  players.erase(TCPids.at(it->get()));

                  std::cout << "TCP client disconnected." << std::endl;
                  TCPselector.remove(**it);
                  it = TCPclients.erase(it);

                  for (const auto& [id, client] : UDPclients)
                     UDPsocket.send(endPacket, client.first, client.second);
               }
            }
            else ++it;
         }
      }

      // Check if any packets can be received
      senderPacket.clear();
      if (UDPsocket.receive(senderPacket, senderAddress, senderPort) == sf::Socket::Status::Done)
      {
         // Unload the packet
         PlayerData player;
         senderPacket >> player;

         if (player.position == Vector2() && player.color == Color())
            continue;

         // Add the client if it just connected
         bool newClient = true;

         for (const auto& [id, client] : UDPclients)
         {
            if (client.first == senderAddress && client.second == senderPort)
            {
               player.id = id;
               newClient = false;
               break;
            }
         }

         // Setup any new clients
         if (newClient)
         {
            player.id = nextID++;
            UDPclients[player.id] = {senderAddress, senderPort};

            players[player.id].second = colors.at(rand() % colors.size());
            std::cout << "New sender connected: ID: " << player.id << " INFO: " << senderAddress << ":" << senderPort << std::endl;

            sf::Packet idPacket;
            idPacket << player.id;
            UDPsocket.send(idPacket, senderAddress, senderPort);
         }
         else
            players[player.id] = {player.position, player.color};

         // Echo the message to other connected clients
         sf::Packet response;
         for (const auto& [id, info] : players)
            response << PlayerData{id, info.first, info.second};

         for (const auto& [id, client] : UDPclients)
            UDPsocket.send(response, client.first, client.second);
      }
   }

   std::cout << "Server disconnected." << std::endl;

   for (auto& socket : TCPclients)
      socket->disconnect();
}
