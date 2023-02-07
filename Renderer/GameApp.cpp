#include "GameApp.h"
#include "Graphics.h"
#include "GraphicsResource.h"
#include "SystemTime.h"
#include "GameInput.h"
#include "CommandQueue.h"
#include "FrameContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ShaderCompositor.h"
#include "Texture.h"
#include "SamplerManager.h"
#include "TextRenderer.h"
#include "PostEffect.h"
#include "Utils/CommandLineArg.h"
#include "Utils/DebugUtils.h"

#include <shellapi.h>

namespace GameApp
{
    using namespace CommandLineArgs;

    LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    void DrawUI();

    void InitializeApplication(IGameApp& game)
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        CommandLineArgs::Initialize(argc, argv);

        Graphics::Initialize(game.RequiresRaytracingSupport());
        SystemTime::Initialize();
        GameInput::Initialize();

        InitSingleton();

        Graphics::InitializeSwapChain(); // need commandQueue

        // TextContext As built-in context
        REGISTER_CONTEXT(PostEffect, Graphics::gRenderWidth, Graphics::gRenderHeight);
        TextRenderer::gTextContext = REGISTER_CONTEXT(TextContext, Graphics::gDisplayWidth, Graphics::gDisplayHeight);
        game.RegisterContext();

        Graphics::InitializeResource();

        game.Start();
    }

    void TerminateApplication(IGameApp& game)
    {
        CommandQueueManager::GetInstance()->IdleGPU();

        game.Cleanup();

        GameInput::Shutdown();

        Graphics::DestroyResource();

        DestroySingleton();
    }

    bool UpdateApplication(IGameApp& game)
    {
        ZoneScoped;

        float DeltaTime = Graphics::GetFrameTime();

        CommandQueueManager::GetInstance()->SelectQueueEvent();

        GameInput::Update(DeltaTime);
        game.Update(DeltaTime);

        DrawUI();

        FrameContextManager* frameContextMgr = FrameContextManager::GetInstance();
        frameContextMgr->BeginRender();
        frameContextMgr->Render();
        frameContextMgr->EndRender();
        frameContextMgr->ComitRenderTask();

        Graphics::Present();

        FrameMark;

        return !game.IsDone();
    }

    int RunApplication(IGameApp* gameApp, const wchar_t* className, HINSTANCE hInst, int nCmdShow)
    {
        if (!XMVerifyCPUSupport())
            return 1;

        Microsoft::WRL::Wrappers::RoInitializeWrapper InitializeWinRT(RO_INIT_MULTITHREADED);
        CheckHR(InitializeWinRT);

        // Register class
        WNDCLASSEX wcex;
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInst;
        wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = className;
        wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
        ASSERT(0 != RegisterClassEx(&wcex), "Unable to register a window");

        // Create window
        Graphics::ResolutionToUINT(Graphics::gDisplayResolution, Graphics::gDisplayWidth, Graphics::gDisplayHeight);

        std::filesystem::current_path(std::filesystem::current_path() / L"..\\");

        RECT rc = { 0, 0, (LONG)Graphics::gDisplayWidth, (LONG)Graphics::gDisplayHeight };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        Graphics::ghWnd = CreateWindow(className, className, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);

        ASSERT(Graphics::ghWnd != 0);

        InitializeApplication(*gameApp);

        ShowWindow(Graphics::ghWnd, nCmdShow/*SW_SHOWDEFAULT*/);

        do
        {
            MSG msg = {};
            bool done = false;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);

                if (msg.message == WM_QUIT)
                    done = true;
            }

            if (done)
                break;
        } while (UpdateApplication(*gameApp));	// Returns false to quit loop

        TerminateApplication(*gameApp);
        Graphics::Shutdown();
        return 0;
    }

    void InitSingleton()
    {
        DescriptorAllocatorManager::GetOrCreateInstance();
        CommandQueueManager::GetOrCreateInstance();
        FrameContextManager::GetOrCreateInstance();
        RootSignatureManager::GetOrCreateInstance();
        PipeLineStateManager::GetOrCreateInstance();
        ShaderCompositor::GetOrCreateInstance(L"Shader");
        TextureManager::GetOrCreateInstance(L"");
        SamplerManager::GetOrCreateInstance();
    }

    void DestroySingleton()
    {
        CommandQueueManager::RemoveInstance();
        FrameContextManager::RemoveInstance();
        RootSignatureManager::RemoveInstance();
        PipeLineStateManager::RemoveInstance();
        ShaderCompositor::RemoveInstance();
        TextureManager::RemoveInstance();
        SamplerManager::RemoveInstance();
        DescriptorAllocatorManager::RemoveInstance();
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            Graphics::ResizeSwapChain((UINT)(UINT64)lParam & 0xFFFF, (UINT)(UINT64)lParam >> 16);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }

        return 0;
    }

    void DrawUI()
    {
        ZoneScoped;

        TextRenderer::gTextContext->DrawFormattedString("FPS %7.2f, mspf %7.5f s\n",
            Graphics::GetFrameRate(), Graphics::GetFrameTime());
    }

    bool IGameApp::IsDone()
    {
        return GameInput::IsFirstPressed(GameInput::kKey_escape);
    }
}
