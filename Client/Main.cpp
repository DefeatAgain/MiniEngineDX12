#include "GameApp.h"

namespace GameApp
{
	class TestGameApp : public IGameApp
	{
		virtual void Start() {}

		virtual void RegisterContext() {}

		virtual void Update(float deltaTime) {};

		virtual void Cleanup() {}

		virtual void Render() {}
	};
}

CREATE_APPLICATION(GameApp::TestGameApp);
