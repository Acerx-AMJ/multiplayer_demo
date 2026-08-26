#include "io.hpp"
#include "playerData.hpp"
#include <Cx/Cx.hpp>
#include <SFML/Network.hpp>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>

using Key = cx::Key;
using Button = cx::ControllerKey;
using Axis = cx::ControllerAxis;
using Vector = cx::ControllerVector;
using ID = int;
using Port = unsigned short;
using ChatBubbleContainer = std::unordered_map<ID, std::pair<std::pair<cx::BasicTimer, float>, std::pair<cx::Rect*, cx::Text*>>>;

std::atomic<bool> quit = false;
std::mutex mutex;

sf::Font defaultFont;
sf::Texture chatBubbleTexture;

void sendMessages(sf::UdpSocket& socket, Port port, ID playerID, cx::Rect& player);
void receiveMessages(sf::UdpSocket& socket, sf::TcpSocket& chatSocket, Port port, ID playerID, cx::Rect& player, std::unordered_map<ID, cx::Rect*>& others, ChatBubbleContainer& container);
void createBubble(ChatBubbleContainer& container, const std::string& message, ID playerID);

int main()
{
   // Bind socket to the given port
   Port port = getInteger("Input desired server's port: "s);

   sf::UdpSocket socket;
   socket.bind(sf::Socket::AnyPort);

   // Send garbage data to trigger new client in server
   sf::Packet startingPacket;
   startingPacket << PlayerData{-1, cx::Vec2f(1.f)};

   if (socket.send(startingPacket, {127, 0, 0, 1}, port) != sf::Socket::Status::Done)
      throw std::runtime_error("Could not send starting data to server.");

   // Retrieve a valid player ID from server
   ID playerID = -1;

   sf::Packet idPacket;
   sf::IpAddress serverIp;
   Port serverPort;

   if (socket.receive(idPacket, serverIp, serverPort) == sf::Socket::Status::Done)
      idPacket >> playerID;
   else
      throw std::runtime_error("Failed to receive player ID from server.");

   sf::TcpSocket chatSocket;

   if (chatSocket.connect(serverIp, serverPort) != sf::Socket::Status::Done)
      throw std::runtime_error("Could not connect TCP socket to server!");

   sf::Packet startingChatPacket;
   startingChatPacket << Message{playerID, ""s};

   socket.setBlocking(false);
   chatSocket.setBlocking(false);

   if (chatSocket.send(startingChatPacket) != sf::Socket::Status::Done)
      throw std::runtime_error("Could not send TCP starting message.");

   // Successfully connected
   std::cout << "Connected to port " << port << " with ID " << playerID << "." << std::endl;

   // Create the window
   sf::RenderWindow window (sf::VideoMode(960, 540), "Multiplayer Demo"s);
   cx::EventHandler event (window);
   window.setFramerateLimit(60u);

   // Create the objects
   cx::Rect platform (cx::Vec2f(400.f), event.getCenter(), cx::Color::gray);
   cx::Rect player (cx::Vec2f(50.f), event.getCenter());

   cx::Camera camera (player, event.getSize());
   camera.setSmoothCameraSpeed(5.f);
   camera.setSmoothCameraZoomSpeed(4.f);
   camera.setZoomBounds(.5f, 3.f);

   defaultFont.loadFromFile("assets/DejaVuSansMono.ttf"s);
   chatBubbleTexture.loadFromFile("assets/chatBubble.png"s);

   cx::Rect chatBar (cx::Vec2f(600.f, 40.f), cx::Vec2f(), cx::Color(125, 125));
   cx::Text chatText (""s, defaultFont, cx::Vec2f(), 24u);

   // Create containers
   std::unordered_map<ID, cx::Rect*> players;
   ChatBubbleContainer chatBubbles;

   // Run receive and send messages in other threads
   std::thread send(sendMessages, std::ref(socket), port, playerID, std::ref(player));
   std::thread receive(receiveMessages, std::ref(socket), std::ref(chatSocket), port, playerID, std::ref(player), std::ref(players), std::ref(chatBubbles));

   bool chatting = false;
   std::string chat;

   // Game loop
   while (window.isOpen())
   {
      // Process events
      event.update();

      // Update the chat
      if (event.isKeyReleased(Key::enter) && chatting && chat.size() > 1)
      {
         createBubble(chatBubbles, chat, playerID);

         sf::Packet chatPacket;
         chatPacket << Message{playerID, chat};

         std::lock_guard<std::mutex> lock (mutex);

         if (chatSocket.send(chatPacket) != sf::Socket::Status::Done)
         {
            chatSocket.disconnect();
            throw std::runtime_error("Could not send data through TCP socket!");
         }

         chat.clear();
         chatText.setString(""s);
         chatting = false;
      }

      if (event.isKeyReleased(Key::slash))
         chatting = !chatting;

      if (chatting && event.isTyping())
      {
         const char typed = event.getTypedChar();
         
         if (typed == 8 && chat.size() > 0)
            chat.pop_back();
         else if (typed != 8 && typed != '/' && chat.size() < 48)
            chat += typed;
         chatText.setString(chat);
      }

      const float dt = event.getDt();
      cx::Vec2f direction = event.getInputVector(Key::d, Key::a, Key::s, Key::w, Vector::rs);

      std::lock_guard<std::mutex> lock (mutex);

      // Move the player
      if (!direction.empty() && event.isKeyReleased(Key::space) && !chatting)
      {
         player.move(direction.normalize() * dt * 3600.f + cx::Vec2f(cx::randfu(-50.f, 50.f), cx::randfu(-50.f, 50.f)));
         camera.shake(20.f, .5f, 100.f);
      }
      else if (!chatting)
         player.move(direction.normalize() * dt * 200.f * (event.isKeyDown(Key::shift) ? 1.5f : 1.f));

      // Zoom the camera
      const float scrollDelta = event.getMouseScrollDelta();

      if (scrollDelta >= .5f)
         camera.zoom(1 - dt * 6.f);
      else if (scrollDelta <= -.5f)
         camera.zoom(1 + dt * 6.f);
      
      // Update chat bubbles
      for (auto it = chatBubbles.begin(); it != chatBubbles.end();)
      {
         // Update chat bubble position
         if (it->first == playerID)
         {
            it->second.second.first->setCenter(player.getCenter() - cx::Vec2f(0.f, 75.f));
            it->second.second.second->setCenter(player.getCenter() - cx::Vec2f(0.f, 80.f));
         }
         else
         {
            it->second.second.first->setCenter(players.at(it->first)->getCenter() - cx::Vec2f(0.f, 75.f));
            it->second.second.second->setCenter(players.at(it->first)->getCenter() - cx::Vec2f(0.f, 80.f));
         }

         // Fade them out and delete them
         if (it->second.first.first.getElapsed() >= 6.f)
         {
            it->second.first.second += dt;

            it->second.second.first->setColor(cx::Color::white.blend(cx::Color(255, 0), it->second.first.second));
            it->second.second.second->setColor(cx::Color::black.blend(cx::Color(0, 0), it->second.first.second));

            if (it->second.first.second >= 1.f)
            {
               delete it->second.second.first;
               delete it->second.second.second;
               it = chatBubbles.erase(it);
            }
            else ++it;
         }
         else ++it;
      }

      // Update camera and chat position
      camera.update(dt);

      if (chatting)
      {
         chatBar.setCenter(camera.getCenter() + cx::Vec2f(0.f, 100.f));
         chatText.setCenter(chatBar.getCenter());
      }

      camera.apply(window);

      // Render to window
      window.clear();
      platform.render(window);

      for (const auto& other : players)
         other.second->render(window);

      player.render(window);

      for (const auto& bubble : chatBubbles)
      {
         bubble.second.second.first->render(window);
         bubble.second.second.second->render(window);
      }

      if (chatting)
      {
         chatBar.render(window);
         chatText.render(window);
      }

      window.display();
   }

   quit = true;

   // Delete all left over bubbles
   for (auto& [id, bubble] : chatBubbles)
   {
      delete bubble.second.first;
      delete bubble.second.second;
   }
   chatBubbles.clear();

   // Join the threads
   if (send.joinable())
      send.join();

   if (receive.joinable())
      receive.join();
}

void sendMessages(sf::UdpSocket& socket, Port port, ID playerID, cx::Rect& player)
{
   while (true)
   {
      if (quit) return;

      // Sleep for a while to relax the CPU
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      std::lock_guard<std::mutex> lock (mutex);

      // Send the packets
      sf::Packet packet;
      packet << PlayerData{playerID, player.getCenter(), player.getColor()};

      if (socket.send(packet, {127, 0, 0, 1}, port) != sf::Socket::Status::Done)
         throw std::runtime_error("Could not send data");
   }
}

void receiveMessages(sf::UdpSocket& socket, sf::TcpSocket& chatSocket, Port port, ID playerID, cx::Rect& player, std::unordered_map<ID, cx::Rect*>& others, ChatBubbleContainer& container)
{
   while (true)
   {
      if (quit) return;

      sf::Packet response;
      sf::IpAddress sender;
      Port senderPort;

      std::lock_guard<std::mutex> lock (mutex);

      // Receive any incoming chat messages
      sf::Packet receivedMsg;

      if (chatSocket.receive(receivedMsg) == sf::Socket::Status::Done)
      {
         Message message;
         receivedMsg >> message;
         createBubble(container, message.message, message.sender);
      }

      // Receive any incoming packets
      if (socket.receive(response, sender, senderPort) == sf::Socket::Status::Done)
      {
         // Process every single packet
         while (!response.endOfPacket())
         {
            PlayerData playerData;
            response >> playerData;

            if (playerData.id == playerID)
            {
               player.setColor(playerData.color);
               continue;
            }

            // Update other players
            if (!playerData.alive)
            {
               delete others.at(playerData.id);
               others.erase(playerData.id);

               for (auto it = container.begin(); it != container.end();)
               {
                  if (it->first == playerData.id)
                  {
                     delete it->second.second.first;
                     delete it->second.second.second;
                     it = container.erase(it);
                  }
                  else ++it;
               }
            }
            else if (others.count(playerData.id))
               others[playerData.id]->setCenter(playerData.position);
            else
            {
               cx::Rect* shape = new cx::Rect(cx::Vec2f(50.f));
               shape->setCenter(playerData.position);
               shape->setColor(playerData.color);
               others.insert({playerData.id, shape});
            }
         }
      }
   }
}

void createBubble(ChatBubbleContainer& container, const std::string& message, int playerID)
{
   if (container.count(playerID))
   {
      delete container.at(playerID).second.second;
      delete container.at(playerID).second.first;
      container.erase(playerID);
   }

   cx::Text* text = new cx::Text(message, defaultFont, cx::Vec2f(), 24u);
   text->setColor(cx::Color::black);

   cx::Rect* shape = new cx::Rect(cx::Vec2f(std::max(140.f, text->getWidth()) + 10.f, 50.f));
   shape->setTexture(chatBubbleTexture);

   container[playerID] = {{cx::BasicTimer(), 0.f}, {shape, text}};
}
