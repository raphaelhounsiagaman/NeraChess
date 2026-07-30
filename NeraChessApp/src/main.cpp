#include "Core/Application.h"

#include "BackgroundLayer.h"
#include "BoardLayer.h"
#include "GameManagerLayer.h"
#include "UILayer.h"

int main()
{
	ApplicationCore::ApplicationSpecification appSpecs;
	appSpecs.Name = "NeraChess App";
	appSpecs.WindowSpec.Width = 1280;
	appSpecs.WindowSpec.Height = 720;

	ApplicationCore::Application app(appSpecs);

	app.PushLayer<BackgroundLayer>();
	app.PushLayer<BoardLayer>();
	app.PushLayer<GameManagerLayer>();
	app.PushLayer<UILayer>();
	// Keep the thread-owning manager last so reverse layer destruction joins its
	// worker before any layer the worker can look up is removed.
	app.PushLayer<GameManagerLayer>();

	app.Run();
}