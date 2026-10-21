#include "playerData.hpp"
#include <CX/Camera.hpp>
#include <CX/EventHandler/EventHandler.hpp>
#include <CX/Rect/Rect.hpp>
#include <CX/Text/Text.hpp>
#include <CX/Timer/BasicTimer.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>

std::atomic<bool> quit = false;
std::mutex mutex;

sf::Font defaultFont;
sf::Texture chatBubbleTexture;

struct ChatBubble {
   bool alive = false;
   float elapsed = 0.0f;
   float fade = 0.0f;
   cx::Rect rect;
   cx::Text text;
};

struct Player {
   cx::Rect player;
   ChatBubble bubble;
};

void flush() {
   std::cout << std::flush;
}

void clear_line(int lines = 1) {
   for (int i = 0; i < lines; ++i) {
      printf("\r\e[K\e[F");
   }
   printf("\r\e[K");
   flush();
}

void createBubble(std::unordered_map<int, Player> &players, const std::string& message, int playerID) {
   cx::Text text (message, defaultFont, {}, 24);
   cx::Rect rect (cx::Vec2f{std::max(140.0f, text.get_width()) + 10.0f, 50.0f});

   text.set_color(cx::Color(0));
   rect.set_texture(&chatBubbleTexture);
   players[playerID].bubble = {true, 0.0f, 0.0f, rect, text};
}

void sendMessages(sf::UdpSocket &socket, const sf::IpAddress &address, unsigned short port, int playerID, cx::Rect &player) {
   while (!quit) {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      std::lock_guard<std::mutex> lock (mutex);

      sf::Packet packet;
      packet << PlayerData{playerID, player.get_center(), player.get_color()};

      if (socket.send(packet, address, port) != sf::Socket::Status::Done) {
         printf("Could not send data.\n");
         exit(EXIT_FAILURE);
      }
   }
}

void receiveMessages(sf::UdpSocket &socket, sf::TcpSocket &chatSocket, unsigned short port, int playerID, cx::Rect &player, std::unordered_map<int, Player> &players) {
   while (!quit) {
      sf::Packet response;
      sf::IpAddress sender;
      unsigned short senderPort;

      std::lock_guard<std::mutex> lock (mutex);
      sf::Packet receivedMsg;

      if (chatSocket.receive(receivedMsg) == sf::Socket::Status::Done) {
         Message message;
         receivedMsg >> message;
         createBubble(players, message.message, message.sender);
      }

      if (socket.receive(response, sender, senderPort) != sf::Socket::Status::Done) {
         continue;
      }

      while (!response.endOfPacket()) {
         PlayerData playerData;
         response >> playerData;

         if (playerData.id == playerID) {
            player.set_color(playerData.color);
            continue;
         }

         if (!playerData.alive) {
            players.erase(playerData.id);
         }
         else if (auto it = players.find(playerData.id); it != players.end()) {
            it->second.player.set_center(playerData.position);
         }
         else {
            players[playerData.id].player = cx::Rect({50.0f, 50.0f}, playerData.position, playerData.color);
         }
      }
   }
}

int main() {
   sf::IpAddress IPAddress;
   unsigned short port = 0;

   do {
      printf("Input server's IP: ");
      std::string IP;
      std::getline(std::cin >> std::ws, IP);
      IPAddress = sf::IpAddress(IP);
      if (IPAddress.toInteger() == 0) {
         printf("Invalid IP address format, try again. ");
      }
   }
   while (IPAddress.toInteger() == 0);

   printf("Input server's port: ");
   std::cin >> std::ws >> port;

   sf::UdpSocket socket;
   if (socket.bind(sf::Socket::AnyPort) != sf::Socket::Status::Done) {
      printf("Could not connect to port %d!\n", port);
      exit(EXIT_FAILURE);
   }

   // Send register packet
   sf::Packet startingPacket;
   startingPacket << PlayerData{0, cx::Vec2f(0.0f), cx::Color::black(), true, true};

   if (socket.send(startingPacket, IPAddress, port) != sf::Socket::Status::Done) {
      printf("Could not send register packet to server.\n");
      exit(EXIT_FAILURE);
   }

   // Now the server should return our ID
   int playerID = -1;
   sf::Packet idPacket;

   if (socket.receive(idPacket, IPAddress, port) == sf::Socket::Status::Done) {
      idPacket >> playerID;
   }
   else {
      printf("Failed to receive player ID from server.\n");
      exit(EXIT_FAILURE);
   }

   sf::TcpSocket chatSocket;
   if (chatSocket.connect(IPAddress, port) != sf::Socket::Status::Done) {
      printf("Could not connect chat TCP socket to server!\n");
      exit(EXIT_FAILURE);
   }

   sf::Packet startingChatPacket;
   startingChatPacket << Message{playerID, ""};

   socket.setBlocking(false);
   chatSocket.setBlocking(false);

   if (chatSocket.send(startingChatPacket) != sf::Socket::Status::Done) {
      printf("Could not send chat TCP starting message.\n");
      exit(EXIT_FAILURE);
   }

   printf("Successfully connected to %s:%u with ID %d.\n", IPAddress.toString().c_str(), port, playerID);
   sf::RenderWindow window (sf::VideoMode(800, 600), "Multiplayer Demo");
   cx::EventHandler event (window);
   window.setFramerateLimit(60);

   defaultFont.loadFromFile("assets/DejaVuSansMono.ttf");
   chatBubbleTexture.loadFromFile("assets/chatBubble.png");

   cx::Rect chatBar (cx::Vec2f(600.f, 40.f), cx::Vec2f(), cx::Color(125, 125));
   cx::Text chatText ("", defaultFont, cx::Vec2f(), 24u);

   std::unordered_map<int, Player> players;
   players[playerID].player = cx::Rect({50.0f, 50.0f}, event.get_center());
   cx::Rect &player = players[playerID].player;

   cx::Rect platform (cx::Vec2f(400.f), event.get_center(), cx::Color::gray());
   cx::Camera camera (player, event.get_size());
   camera.set_smooth_speed(5.f);
   camera.set_smooth_zoom_speed(4.f);
   camera.set_zoom_bounds(.5f, 3.f);

   std::thread send(sendMessages, std::ref(socket), std::ref(IPAddress), port, playerID, std::ref(player));
   std::thread receive(receiveMessages, std::ref(socket), std::ref(chatSocket), port, playerID, std::ref(player), std::ref(players));

   std::string chat;
   bool chatting = false;

   while (window.isOpen()) {
      event.update();

      // Update the chat
      if (event.is_released(cx::Key::enter) && chatting && chat.size() > 1) {
         createBubble(players, chat, playerID);

         sf::Packet chatPacket;
         chatPacket << Message{playerID, chat};

         std::lock_guard<std::mutex> lock (mutex);
         if (chatSocket.send(chatPacket) != sf::Socket::Status::Done) {
            chatSocket.disconnect();
            printf("Could not send data through TCP socket!\n");
            exit(EXIT_FAILURE);
         }
         chat.clear();
         chatText.set_string("");
         chatting = false;
      }

      if (event.is_released(cx::Key::slash)) {
         chatting = !chatting;
      }

      if (chatting && event.is_typing()) {
         const char typed = event.get_typed_char();
         if (typed == 8 && chat.size() > 0) {
            chat.pop_back();
         }
         else if (typed != 8 && typed != '/' && chat.size() < 48) {
            chat += typed;
         }
         chatText.set_string(chat);
      }

      const float dt = event.get_dt();
      cx::Vec2f direction = event.get_vector(cx::Key::d, cx::Key::a, cx::Key::s, cx::Key::w, cx::ControllerVector::rs);

      // handle player movement
      std::lock_guard<std::mutex> lock (mutex);
      if (!direction.empty() && event.is_released(cx::Key::space) && !chatting) {
         player.move(direction.normalize() * dt * 3600.f + cx::Vec2f(cx::randfu(-50.f, 50.f), cx::randfu(-50.f, 50.f)));
         camera.shake(20.f, .5f, 100.f);
      }
      else if (!chatting) {
         player.move(direction.normalize() * dt * 200.f * (event.is_down(cx::Key::shift) ? 1.5f : 1.f));
      }

      const float scrollDelta = event.get_scroll_delta();
      if (scrollDelta >= .5f) {
         camera.zoom(1 - dt * 6.f);
      }
      else if (scrollDelta <= -.5f) {
         camera.zoom(1 + dt * 6.f);
      }

      // update chat bubbles
      for (auto &[id, plr]: players) {
         if (!plr.bubble.alive) {
            continue;
         }
         plr.bubble.elapsed += dt;
         plr.bubble.rect.set_center(plr.player.get_center() - cx::Vec2f(0.0f, 75.0f));
         plr.bubble.text.set_center(plr.player.get_center() - cx::Vec2f(0.0f, 80.0f));

         if (plr.bubble.elapsed >= 6.0f) {
            plr.bubble.fade += dt;
            plr.bubble.rect.set_color(cx::Color(255, 255).blend(cx::Color(255, 0), plr.bubble.fade));
            plr.bubble.text.set_color(cx::Color(0, 255).blend(cx::Color(0, 0), plr.bubble.fade));

            if (plr.bubble.fade >= 1.0f) {
               plr.bubble.alive = false;
            }
         }
      }

      // update camera and UI
      camera.update(dt);
      if (chatting) {
         chatBar.set_center(camera.get_center() + cx::Vec2f(0.f, 100.f));
         chatText.set_center(chatBar.get_center());
      }

      camera.apply(window);

      // Render to window
      window.clear();
      platform.render(window);

      for (auto &[_, plr]: players) {
         plr.player.render(window);
      }

      for (auto &[_, plr]: players) {
         if (plr.bubble.alive) {
            plr.bubble.rect.render(window);
            plr.bubble.text.render(window);
         }
      }

      if (chatting) {
         chatBar.render(window);
         chatText.render(window);
      }
      window.display();
   }
   quit = true;

   if (send.joinable()) {
      send.join();
   }

   if (receive.joinable()) {
      receive.join();
   }
}
