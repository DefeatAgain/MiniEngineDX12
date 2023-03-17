#include "MainView.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "ImGui/imgui.h"

void MainView::ShowUI(Scene* scene)
{
	ASSERT(scene);

	ImGui::Begin("Properties");
	ImGui::Spacing();

	ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
	if (ImGui::CollapsingHeader("Scene"))
	{
		float cameraPos3[3];
		XMStoreFloat3((XMFLOAT3*)&cameraPos3, scene->mSceneCamera.GetPosition());
		ImGui::DragFloat3("cameraPos", cameraPos3, 0.1f, -FLT_MAX, FLT_MAX, "%.1f");

		ImGui::SeparatorText("Light");
		ImGui::Text("SunDirectionTheta");
		//static bool a = false;
		//scene->mSunDirectionTheta += a ? 1.0f : -1.0f;
		//a = !a;
		ImGui::SliderAngle("SunDirectionTheta", &scene->mSunDirectionTheta, -90.0f, 90.0f, "%.1f");
		ImGui::Text("SunDirectionPhi");
		ImGui::SliderAngle("SunDirectionPhi", &scene->mSunDirectionPhi, -180.0f, 180.0f, "%.1f");

		ImGui::Text("LightIntensity");
		float lightIntensity3[3];
		XMStoreFloat3((XMFLOAT3*)&lightIntensity3, scene->mSunLightIntensity);
		ImGui::DragFloat3("SunDirectionIntensity", lightIntensity3, 0.05f, 0.0f, 10.0f, "%.1f");
		scene->mSunLightIntensity = *(XMFLOAT3*)lightIntensity3;
		if (ImGui::DragFloat("SunDirectionIntensity#all", lightIntensity3, 0.05f, 0.0f, 10.0f, "%.1f"))
			scene->mSunLightIntensity = Vector3(lightIntensity3[0]);

		ImGui::SeparatorText("ShadowBias");
		ImGui::Text("ShadowBias");
		ImGui::SliderFloat("ShadowBias", &scene->mShadowBias, 0.001f, 0.05f, "%.4f");
		ImGui::Text("ShadowMsaa");
		int shadowMsaa = ModelRenderer::gMsaaShadowSample;
		ImGui::RadioButton("off", &shadowMsaa, 0); ImGui::SameLine();
		ImGui::RadioButton("2x", &shadowMsaa, 2); ImGui::SameLine();
		ImGui::RadioButton("4x", &shadowMsaa, 4); ImGui::SameLine();
		ImGui::RadioButton("8x", &shadowMsaa, 8);
		if (shadowMsaa != (int)ModelRenderer::gMsaaShadowSample)
		{
			ModelRenderer::ResetShadowMsaa(shadowMsaa);
			scene->ResetShadowMap();
		}
	}

	ImGui::End();
}
