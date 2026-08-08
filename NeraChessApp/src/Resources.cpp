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

			const std::filesystem::path siblingResources = basePath / "Resources";
			if (std::filesystem::is_directory(siblingResources))
				return siblingResources;

			// sdl2-compat can return an application resource directory directly
			// on macOS, even for a non-bundled executable.
			if (basePath.filename() == "Resources" && std::filesystem::is_directory(basePath))
				return basePath;
		}

		const std::filesystem::path workingDirectoryResources = std::filesystem::current_path() / "Resources";
		if (std::filesystem::is_directory(workingDirectoryResources))
			return workingDirectoryResources;

		return std::filesystem::current_path() / "NeraChessApp" / "Resources";
	}();

	return resourceDirectory / relativePath;
}
} // namespace NeraChessApp
