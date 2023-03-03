#include "GameApp.h"
#include "Scene.h"
#include "ModelConverter.h"
#include "glTF.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "Texture.h"
#include "ImGui/imgui.h"

static glTF::Asset sAsset;
static Scene* sScenePtr;

namespace GameApp
{
	class SceneGameApp : public IGameApp
	{
		virtual void Start() override;

		virtual void RegisterContext() override;

		virtual void Update(float deltaTime);

		virtual void Cleanup();
	};


	void SceneGameApp::Start()
	{
		sAsset.Parse(L"Asset/Sponza2/sponza2.gltf");

		ModelConverter::BuildMaterials(sAsset);
		ModelConverter::BuildAllMeshes(sAsset);
		ModelConverter::BuildScene(sScenePtr, sAsset);

		sScenePtr->Startup();

		TextureRef radianceIBL = GET_TEX(ModelConverter::GetIBLTextureFilename(L"CloudCommon_S"));
		TextureRef irradianceIBL = GET_TEX(ModelConverter::GetIBLTextureFilename(L"CloudCommon_D"));
		sScenePtr->SetIBLTextures(irradianceIBL, radianceIBL);
	}

	void SceneGameApp::RegisterContext()
	{
		MaterialManager::GetOrCreateInstance();
		MeshManager::GetOrCreateInstance();

		sScenePtr = REGISTER_CONTEXT(Scene);

		ModelRenderer::Initialize();
	}

	void SceneGameApp::Update(float deltaTime)
	{
		MaterialManager::GetInstance()->Update();

		sScenePtr->Update(deltaTime);
			
		static unsigned counter = 0;
		ImGui::ShowDemoWindow();

		//ImGui::Begin("Hello, world!");
		//ImGui::Text("This is some useful text.");
		//ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
		//ImGui::Checkbox("Another Window", &show_another_window);

		//ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		//ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

		//if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
		//	counter++;
		//ImGui::SameLine();
		//ImGui::Text("counter = %d", counter);

		//ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", ImGui::GetIO().DeltaTime, ImGui::GetIO().Framerate);
		//ImGui::End();
	}

	void SceneGameApp::Cleanup()
	{
		ModelRenderer::Destroy();

		MaterialManager::RemoveInstance();
		MeshManager::RemoveInstance();
	}
}

CREATE_APPLICATION(GameApp::SceneGameApp);
