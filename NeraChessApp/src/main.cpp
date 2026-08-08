#include "BackgroundLayer.h"
#include "BoardLayer.h"
#include "Core/Application.h"
#include "GameManagerLayer.h"
#include "UILayer.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>

int main(int argc, char** argv)
{
	try
	{
		const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
		const bool clockSmokeTest = argc == 2 && std::string_view(argv[1]) == "--clock-smoke-test";

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
		if (clockSmokeTest)
		{
			using namespace std::chrono_literals;
			GameManagerLayer* gameManager = app.GetLayer<GameManagerLayer>();
			gameManager->SetTimeControl({40ms, 2s});
			gameManager->StartGame();

			const auto deadline = std::chrono::steady_clock::now() + 2s;
			GameSnapshot snapshot;
			do
			{
				std::this_thread::sleep_for(5ms);
				snapshot = gameManager->GetSnapshot();
			} while (snapshot.gameStarted && std::chrono::steady_clock::now() < deadline);

			if (snapshot.gameStarted || snapshot.result != GameResult::BlackWon ||
				snapshot.endReason != GameEndReason::Timeout || snapshot.clock.whiteRemaining.count() != 0)
			{
				std::cerr << "Timed-game smoke test failed\n";
				gameManager->StopGame();
				return 1;
			}
			return 0;
		}

		app.Run();
	}
	catch (const std::exception& exception)
	{
		std::cerr << "NeraChess failed to start: " << exception.what() << '\n';
		return 1;
	}
}
