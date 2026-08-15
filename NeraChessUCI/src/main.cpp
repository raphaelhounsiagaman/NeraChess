#include "UciSession.h"

#include <filesystem>
#include <iostream>

namespace
{
std::filesystem::path GetExecutableDirectory(const char* executable)
{
	std::error_code error;
	std::filesystem::path executablePath = std::filesystem::absolute(executable, error);
	if (error)
		executablePath = executable;

	const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(executablePath, error);
	if (!error)
		executablePath = canonicalPath;

	return executablePath.parent_path();
}
} // namespace

int main(int argc, char** argv)
{
	std::ios::sync_with_stdio(false);
	std::cin.tie(nullptr);

	const std::filesystem::path executableDirectory =
		argc > 0 ? GetExecutableDirectory(argv[0]) : std::filesystem::path{};
	const std::filesystem::path openingBookPath = executableDirectory.empty()
		? std::filesystem::path{}
		: executableDirectory / "Resources/OpeningBook/OpeningBook.txt";

	UciSession session(std::cin, std::cout, openingBookPath, executableDirectory);
	return session.Run();
}
