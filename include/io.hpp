#ifndef NETWORKING_IO_HPP
#define NETWORKING_IO_HPP

#include <iostream>
#include <limits>

using namespace std::string_literals;

/// @brief Clear lines from terminal.
/// @param lines Amount of lines to clear.
inline void clearln(int lines = 1) noexcept
{
   for (int i = 0; i <= lines; ++i)
   {
      std::cout << "\r\033[K";
      if (i < lines)
         std::cout << "\033[F";
   }
   std::cout << std::flush;
}

/// @brief Get number input.
/// @return Number.
inline int getInteger(const std::string& prompt) noexcept
{
   int number;

   while (true)
   {
      std::cout << prompt;
      std::cin >> number;

      if (std::cin.fail())
      {
         std::cin.clear();
         std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
         clearln();
         std::cout << "Invalid input. Please enter a valid integer.\n";
      }
      else
      {
         std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
         return number;
      } 
   }
}

#endif // NETWORKING_IO_HPP
