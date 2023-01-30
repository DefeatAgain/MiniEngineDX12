#include "GameApp.h"

namespace GameApp
{
	class SceneGameApp : public IGameApp
	{
		virtual void Start() override;

		virtual void RegisterContext() override;

		virtual void Update(float deltaTime) {};

		virtual void Cleanup() {}

		virtual void Render() {}
	};

	void SceneGameApp::Start()
	{

	}

	void SceneGameApp::RegisterContext()
	{

	}
}

CREATE_APPLICATION(GameApp::SceneGameApp);

