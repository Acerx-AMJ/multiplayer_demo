#include <SFML/Network.hpp>
#include "playerData.hpp"
#include <memory>
#include <unordered_map>

struct Client {
   sf::IpAddress address;
   unsigned short port;
};

struct Player {
   int textureId;
   cx::Vec2f position;
};

int main() {
   sf::UdpSocket socket;
   if (socket.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done) {
      printf("Could not bind to port %d!\n", socket.getLocalPort());
      exit(EXIT_FAILURE);
   }

   socket.setBlocking(false);
   printf("Server listening on %s:%d...\n", sf::IpAddress::getLocalAddress().toString().c_str(), socket.getLocalPort());
   srand(time(nullptr));

   std::unordered_map<int, Client> UDPclients;
   std::unordered_map<int, Player> players;
   int nextID = 1;

   sf::Packet senderPacket;
   sf::IpAddress senderAddress;
   unsigned short senderPort;

   sf::TcpListener listener;
   if (listener.listen(socket.getLocalPort()) != sf::Socket::Status::Done) {
      printf("Could not bind TCP listener.\n");
      exit(EXIT_FAILURE);
   }

   std::vector<std::unique_ptr<sf::TcpSocket>> TCPclients;
   std::unordered_map<sf::TcpSocket*, int> TCPids;
   sf::SocketSelector TCPselector;
   TCPselector.add(listener);
   listener.setBlocking(false);

   while (true) {
      if (TCPselector.wait(sf::milliseconds(1))) {
         if (TCPselector.isReady(listener)) {
            auto socket = std::make_unique<sf::TcpSocket>();
            if (listener.accept(*socket) != sf::Socket::Status::Done) {
               printf("Could not accept socket!\n");
               exit(EXIT_FAILURE);
            }

            TCPselector.add(*socket);
            TCPclients.push_back(std::move(socket));
            printf("New TCP socket connected.\n");
         }

         for (auto it = TCPclients.begin(); it != TCPclients.end();) {
            if (!TCPselector.isReady(**it)) {
               ++it;
               continue;
            }

            sf::Packet packet;
            sf::Socket::Status status = (*it)->receive(packet);
            if (status == sf::Socket::Status::Partial || status == sf::Socket::Status::NotReady) {
               ++it;
               continue;
            }

            if (status == sf::Socket::Status::Done) {
               Message message;
               packet >> message;

               TCPids[it->get()] = message.sender;
               if (message.message.empty()) {
                  ++it;
                  continue;
               }

               printf("Chat message from ID %d: %s\n", message.sender, message.message.c_str());
               sf::Packet echoPacket;
               echoPacket << message;

               for (std::unique_ptr<sf::TcpSocket> &other: TCPclients) {
                  if (other.get() == it->get()) {
                     continue;
                  }

                  if (other->send(echoPacket) != sf::Socket::Status::Done) {
                     printf("Failed to echo user's message!\n");
                  }
               }
               ++it;
               continue;
            }

            if (auto found = TCPids.find(it->get()); found != TCPids.end()) {
               int leftId = found->second;

               sf::Packet endPacket;
               endPacket << PlayerData{leftId, cx::Vec2f(), 0, false};

               UDPclients.erase(leftId);
               players.erase(leftId);
               TCPids.erase(found);

               printf("TCP client %d disconnected.\n", leftId);
               for (const auto &[cid, client]: UDPclients) {
                  socket.send(endPacket, client.address, client.port);
               }
            }
            TCPselector.remove(**it);
            it = TCPclients.erase(it);
         }
      }

      while (socket.receive(senderPacket, senderAddress, senderPort) == sf::Socket::Status::Done) {
         PlayerData player;
         senderPacket >> player;
         senderPacket.clear();

         int id = -1;
         for (const auto &[cid, client]: UDPclients) {
            if (client.address == senderAddress && client.port == senderPort) {
               id = cid;
               break;
            }
         }

         if (id == -1) {
            if (!player.registering) {
               continue;
            }
            id = nextID++;
            UDPclients[id] = {senderAddress, senderPort};
            players[id].textureId = player.textureId;

            printf("New sender connected: %d@%s:%u.\n", id, senderAddress.toString().c_str(), senderPort);
            sf::Packet idPacket;
            idPacket << id;
            socket.send(idPacket, senderAddress, senderPort);
         }
         else {
            players[id] = {player.textureId, player.position};
         }

         sf::Packet response;
         for (const auto &[id, info]: players) {
            response << PlayerData{id, info.position, info.textureId};
         }

         for (const auto &[id, client]: UDPclients) {
            socket.send(response, client.address, client.port);
         }
      }
   }

   printf("Server disconnected.\n");
   for (std::unique_ptr<sf::TcpSocket> &socket: TCPclients) {
      socket->disconnect();
   }
}
