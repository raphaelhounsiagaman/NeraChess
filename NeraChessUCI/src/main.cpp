#include "UciSession.h"

#include <filesystem>
#include <iostream>

namespace
{
    std::filesystem::path GetOpeningBookPath(const char* executable)
    {
        std::error_code error;
        std::filesystem::path executablePath = std::filesystem::absolute(executable, error);
        if (error)
            executablePath = executable;

        const std::filesystem::path canonicalPath =
            std::filesystem::weakly_canonical(executablePath, error);
        if (!error)
            executablePath = canonicalPath;

        return executablePath.parent_path() /
            "Ressources/OpeningBook/OpeningBook.txt";
    }
}

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    const std::filesystem::path openingBookPath = argc > 0
        ? GetOpeningBookPath(argv[0])
        : std::filesystem::path{};
    UciSession session(std::cin, std::cout, openingBookPath);
    return session.Run();
}
