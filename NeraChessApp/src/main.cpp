#include "Core/Application.h"

#include "BackgroundLayer.h"
#include "BoardLayer.h"
#include "GameManagerLayer.h"
#include "UILayer.h"

int main()
{
	NeraCore::ApplicationSpecification appSpecs;
	appSpecs.Name = "Nera Chess App";
	appSpecs.WindowSpec.Width = 1280;
	appSpecs.WindowSpec.Height = 720;

	NeraCore::Application app(appSpecs);
	app.PushLayer<BackgroundLayer>();
	app.PushLayer<BoardLayer>();
	app.PushLayer<GameManagerLayer>();
	app.PushLayer<UILayer>();

	app.Run();
}