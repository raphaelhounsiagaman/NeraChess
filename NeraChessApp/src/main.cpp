#include "Core/Application.h"

#include "BackgroundLayer.h"
#include "BoardLayer.h"
#include "GameManagerLayer.h"
#include "UILayer.h"

#include <string_view>

int main(int argc, char** argv)
{
	const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";

	ApplicationCore::ApplicationSpecification appSpecs;
	appSpecs.Name = "NeraChess App";
	appSpecs.WindowSpec.Width = 1280;
	appSpecs.WindowSpec.Height = 720;

	ApplicationCore::Application app(appSpecs);

	app.PushLayer<BackgroundLayer>();
	app.PushLayer<BoardLayer>();
	app.PushLayer<UILayer>();
	// Keep the thread-owning manager last so reverse layer destruction joins its
	// worker before any layer the worker can look up is removed.
	app.PushLayer<GameManagerLayer>();

	if (smokeTest)
	{
		GameManagerLayer* gameManager = app.GetLayer<GameManagerLayer>();
		gameManager->StartGame();
		gameManager->StopGame();
		return 0;
	}

	app.Run();
}
