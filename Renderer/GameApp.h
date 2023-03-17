#pragma once
#include "CoreHeader.h"

namespace GameApp
{
	class IGameApp
	{
	public:
		virtual void Start() = 0;

		virtual void RegisterContext() = 0;

		virtual void Update(float deltaTime) = 0;

		virtual void Cleanup() = 0;

		virtual bool RequiresRaytracingSupport() { return false; }

		virtual bool IsDone();
	};

	int RunApplication(IGameApp* app, const wchar_t* className, HINSTANCE hInst, int nCmdShow);

	void InitSingleton();
	void DestroySingleton();
}

#define CREATE_APPLICATION( app_class, app_name ) \
    int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/, _In_ int nCmdShow) \
    { \
		auto appInst = app_class(); \
        return GameApp::RunApplication( &appInst, L#app_name, hInstance, nCmdShow ); \
    }