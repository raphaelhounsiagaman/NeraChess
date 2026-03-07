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

	app.Run();
}