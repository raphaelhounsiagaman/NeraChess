#include "UciSession.h"

#include <iostream>

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    UciSession session(std::cin, std::cout);
    return session.Run();
}
