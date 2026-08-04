#include "Resources.h"

#include "SDL.h"

namespace NeraChessApp
{
	std::filesystem::path GetResourcePath(const std::filesystem::path& relativePath)
	{
		static const std::filesystem::path resourceDirectory = []
		{
			char* executableDirectory = SDL_GetBasePath();
			if (executableDirectory)
			{
				std::filesystem::path basePath = executableDirectory;
				SDL_free(executableDirectory);
				return basePath / "Ressources";
			}

			return std::filesystem::current_path() / "Ressources";
		}();

		return resourceDirectory / relativePath;
	}
}
