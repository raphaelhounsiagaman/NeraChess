#pragma once

#include <filesystem>

namespace NeraChessApp
{
	std::filesystem::path GetResourcePath(const std::filesystem::path& relativePath);
}
