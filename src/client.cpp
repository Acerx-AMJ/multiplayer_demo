#include "playerData.hpp"
#include <CX/Camera.hpp>
#include <CX/EventHandler/EventHandler.hpp>
#include <CX/Rect/Rect.hpp>
#include <CX/Text/Text.hpp>
#include <CX/Timer/BasicTimer.hpp>
#include <SFML/Network.hpp>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>

constexpr int windowWidth = 960;
constexpr int windowHeight = 540;

sf::Font defaultFont;
sf::Texture chatBubbleTexture;
sf::Texture chatIcon;
sf::Texture brushIcon;

struct ChatBubble {
   bool alive = false;
   float elapsed = 0.0f;
   float fade = 0.0f;
   cx::Rect rect;
   cx::Text text;
};

struct Player {
   int textureId;
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

void sendMessages(sf::UdpSocket &socket, const sf::IpAddress &address, unsigned short port, int playerID, Player &player, std::atomic<bool> &quit, std::mutex &mutex) {
   int lastTextureId = -1;

   while (!quit) {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      std::lock_guard<std::mutex> lock (mutex);

      PlayerData data = {playerID, player.player.get_center(), -1};
      if (lastTextureId != player.textureId) {
         lastTextureId = player.textureId;
         data.textureId = player.textureId;
      }

      sf::Packet packet;
      packet << data;

      if (socket.send(packet, address, port) != sf::Socket::Status::Done) {
         printf("Could not send data.\n");
         exit(EXIT_FAILURE);
      }
   }
}

void receiveMessages(sf::UdpSocket &socket, sf::TcpSocket &chatSocket, unsigned short port, int playerID, Player &player, std::unordered_map<int, Player> &players, std::atomic<bool> &quit, std::mutex &mutex) {
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
            continue;
         }

         if (!playerData.alive) {
            players.erase(playerData.id);
         }
         else if (auto it = players.find(playerData.id); it != players.end()) {
            it->second.player.set_center(playerData.position);
            it->second.textureId = playerData.textureId;
         }
         else {
            players[playerData.id].player = cx::Rect({50.0f, 50.0f}, playerData.position);
            players[playerData.id].textureId = playerData.textureId;
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

   // load all assets
   defaultFont.loadFromFile("assets/DejaVuSansMono.ttf");
   chatBubbleTexture.loadFromFile("assets/chatBubble.png");
   chatIcon.loadFromFile("assets/chat.png");
   brushIcon.loadFromFile("assets/brush.png");

   auto dirIter = std::filesystem::directory_iterator("assets/players");
   int fileCount = std::count_if(begin(dirIter), end(dirIter), [](const auto &entry) { return entry.is_regular_file(); });

   struct SkinIcon {
      int textureId;
      cx::Rect rect;
   };

   std::vector<SkinIcon> textureRects;
   std::vector<sf::Texture> textures;
   textures.resize(fileCount);
   textureRects.reserve(fileCount);

   constexpr float windowPaddingX = 100.0f;
   constexpr float windowPaddingY = 50.0f;
   constexpr float iconPadding = 5.0f;
   constexpr float iconSize = 40.0f;
   constexpr int iconCountX = (windowWidth - windowPaddingX * 2.0f) / (iconSize + iconPadding);
   int column = 0;
   int row = 0;

   for (const auto &file: std::filesystem::directory_iterator("assets/players")) {
      if (!file.is_regular_file()) {
         continue;
      }
      const std::string stem = file.path().stem().string();
      size_t pos = stem.find_last_not_of("0123456789");
      size_t start = (pos == std::string::npos) ? 0 : pos + 1;
      if (start >= stem.size()) {
         continue;
      }

      int n = std::stoi(stem.substr(start)) - 1;
      if (n < 0 || (size_t)n >= textures.size()) {
         printf("Invalid player texture '%s'. Texture count is %zu.\n", file.path().string().c_str(), textures.size());
         continue;
      }

      if (!textures[n].loadFromFile(file.path().string())) {
         printf("Failed to load '%s'.\n", file.path().string().c_str());
      }
      else {
         // setup rects
         cx::Rect rect ({iconSize}, {windowPaddingX + (column + 0.5f) * (iconSize + iconPadding), windowPaddingY + (row + 0.5f) * (iconSize + iconPadding)});
         rect.set_texture(&textures[n]);
         textureRects.push_back({n, rect});

         column = (column + 1) % iconCountX;
         row += (column == 0);
      }
   }
   srand(time(nullptr));
   int initialTextureId = (textures.empty() ? 0 : rand() % textures.size());

   // Send register packet
   sf::Packet startingPacket;
   startingPacket << PlayerData{0, cx::Vec2f(0.0f), initialTextureId, true, true};

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
   sf::RenderWindow window (sf::VideoMode(windowWidth, windowHeight), "Multiplayer Demo");
   cx::EventHandler event (window);
   window.setFramerateLimit(60);

   cx::Rect chatBar ({750.f, 40.f}, event.get_center() + cx::Vec2f(0.0f, 100.0f), {125, 125});
   cx::Text chatText ("", defaultFont, chatBar.get_center(), 24);

   cx::Rect chatButton ({50.0f, 50.0f}, {30.0f, windowHeight - 30.0f});
   cx::Rect brushButton ({50.0f, 50.0f}, {85.0f, windowHeight - 30.0f});
   chatButton.set_texture(&chatIcon);
   brushButton.set_texture(&brushIcon);

   std::unordered_map<int, Player> players;
   Player &player = players[playerID];
   player.player = cx::Rect({50.0f, 50.0f}, event.get_center());
   player.textureId = initialTextureId;

   cx::Rect platform (cx::Vec2f(400.f), event.get_center(), cx::Color::gray());
   cx::Camera camera (player.player, event.get_size());
   camera.set_smooth_speed(5.f);
   camera.set_smooth_zoom_speed(4.f);
   camera.set_zoom_bounds(.5f, 3.f);

   std::atomic<bool> quit = false;
   std::mutex mutex;

   std::thread send(sendMessages, std::ref(socket), std::ref(IPAddress), port, playerID, std::ref(player), std::ref(quit), std::ref(mutex));
   std::thread receive(receiveMessages, std::ref(socket), std::ref(chatSocket), port, playerID, std::ref(player), std::ref(players), std::ref(quit), std::ref(mutex));

   std::string chat;
   bool chatting = false;
   bool skinSelection = false;

   while (window.isOpen()) {
      event.update();
      cx::MouseState mouseState = event.get_mouse_state(cx::Mouse::left);

      // Update the chat and skin selection
      chatButton.update(mouseState);
      brushButton.update(mouseState);

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

      if (brushButton.is_mouse_up()) {
         skinSelection = !skinSelection;
      }

      if (chatButton.is_mouse_up()) {
         chatting = !chatting;
      }

      if (event.is_released(cx::Key::slash) && !event.is_down(cx::Key::l_shift) && !event.is_down(cx::Key::r_shift)) {
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

      for (size_t i = 0; skinSelection && i < textureRects.size(); ++i) {
         textureRects[i].rect.update(mouseState);
         if (textureRects[i].rect.is_mouse_up()) {
            std::lock_guard<std::mutex> lock (mutex);
            player.textureId = textureRects[i].textureId;
         }
      }

      // handle player movement
      const float dt = event.get_dt();
      cx::Vec2f direction = event.get_vector(cx::Key::d, cx::Key::a, cx::Key::s, cx::Key::w, cx::ControllerVector::rs);

      std::lock_guard<std::mutex> lock (mutex);
      if (!direction.empty() && event.is_released(cx::Key::space) && !chatting) {
         player.player.move(direction.normalize() * dt * 3600.f + cx::Vec2f(cx::randfu(-50.f, 50.f), cx::randfu(-50.f, 50.f)));
         camera.shake(20.f, .5f, 100.f);
      }
      else if (!chatting) {
         player.player.move(direction.normalize() * dt * 200.f * (event.is_down(cx::Key::shift) ? 1.5f : 1.f));
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

      // Render to window
      window.clear();
      camera.update(dt);
      camera.apply(window);

      platform.render(window);
      for (auto &[_, plr]: players) {
         plr.player.set_texture((size_t)plr.textureId >= textures.size() ? nullptr : &textures[plr.textureId]);
         plr.player.render(window);
      }

      for (auto &[_, plr]: players) {
         if (plr.bubble.alive) {
            plr.bubble.rect.render(window);
            plr.bubble.text.render(window);
         }
      }

      window.setView(window.getDefaultView());
      chatButton.render(window);
      brushButton.render(window);

      for (size_t i = 0; skinSelection && i < textureRects.size(); ++i) {
         textureRects[i].rect.render(window);
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
