#include "Graphics.h"
#include "DescriptorHandle.h"
#include "CommandQueue.h"
#include "GameApp.h"
#include "PixelBuffer.h"
#include "FrameContext.h"
#include "SystemTime.h"
#include "Math/VectorMath.h"
#include "Utils/DebugUtils.h"
#include "Utils/CommandLineArg.h"
#include "Utils/ThreadPoolExecutor.h"

namespace
{
    Microsoft::WRL::ComPtr<IDXGISwapChain1> sSwapChain1 = nullptr;

    const uint32_t vendorID_Nvidia = 0x10DE;
    const uint32_t vendorID_AMD = 0x1002;
    const uint32_t vendorID_Intel = 0x8086;

    ColorBuffer gDisplayPlane[SWAP_CHAIN_BUFFER_COUNT];
    ColorBuffer gSceneColorBuffer[SWAP_CHAIN_BUFFER_COUNT];
    DepthBuffer gSceneDepthBuffer[SWAP_CHAIN_BUFFER_COUNT];

    float sFrameTime = 0.0f;
    uint64_t sFrameIndex = 0;
    int64_t sFrameStartTick = 0;
}

namespace Utility
{
    ThreadPoolExecutor gThreadPoolExecutor(4);
}

namespace Graphics
{
#ifndef RELEASE
    const GUID WKPDID_D3DDebugObjectName = { 0x429b8c22,0x9188,0x4b0c, { 0x87,0x42,0xac,0xb0,0xbf,0x85,0xc2,0x00 } };
#endif
    bool DebugZoom = false;

    eResolution gDisplayResolution = k720p;
    eResolution gRenderResolution = k720p;
    uint32_t gDisplayWidth = 1920;
    uint32_t gDisplayHeight = 1080;
    uint32_t gRenderWidth = 1920;
    uint32_t gRenderHeight = 1080;
    bool gEnableHDROutput = false;

    bool gEnableVSync = false;
    bool gLimitTo30Hz = false;
    bool gDropRandomFrames = false;

    Microsoft::WRL::ComPtr<ID3D12Device> gDevice = nullptr;
    HWND ghWnd = nullptr;

    D3D_FEATURE_LEVEL gD3DFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    bool gTypedUAVLoadSupport_R11G11B10_FLOAT = false;
    bool gTypedUAVLoadSupport_R16G16B16A16_FLOAT = false;


    // Check adapter support for DirectX Raytracing.
    bool IsDirectXRaytracingSupported(ID3D12Device* testDevice)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupport{};

        if (FAILED(testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupport, sizeof(featureSupport))))
            return false;

        return featureSupport.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    uint32_t GetDesiredGPUVendor()
    {
        uint32_t desiredVendor = 0;

        std::wstring vendorVal;
        if (CommandLineArgs::GetString(L"vendor", vendorVal))
        {
            // Convert to lower case
            std::transform(vendorVal.begin(), vendorVal.end(), vendorVal.begin(), std::towlower);

            if (vendorVal.find(L"amd") != std::wstring::npos)
            {
                desiredVendor = vendorID_AMD;
            }
            else if (vendorVal.find(L"nvidia") != std::wstring::npos || vendorVal.find(L"nvd") != std::wstring::npos ||
                vendorVal.find(L"nvda") != std::wstring::npos || vendorVal.find(L"nv") != std::wstring::npos)
            {
                desiredVendor = vendorID_Nvidia;
            }
            else if (vendorVal.find(L"intel") != std::wstring::npos || vendorVal.find(L"intc") != std::wstring::npos)
            {
                desiredVendor = vendorID_Intel;
            }
        }

        return desiredVendor;
    }

    const wchar_t* GPUVendorToString(uint32_t vendorID)
    {

        switch (vendorID)
        {
        case vendorID_Nvidia:
            return L"Nvidia";
        case vendorID_AMD:
            return L"AMD";
        case vendorID_Intel:
            return L"Intel";
        default:
            return L"Unknown";
            break;
        }
    }

    uint32_t GetVendorIdFromDevice(ID3D12Device* pDevice)
    {
        LUID luid = pDevice->GetAdapterLuid();

        // Obtain the DXGI factory
        Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
        CheckHR(CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

        Microsoft::WRL::ComPtr<IDXGIAdapter1> pAdapter;

        if (SUCCEEDED(dxgiFactory->EnumAdapterByLuid(luid, IID_PPV_ARGS(pAdapter.GetAddressOf()))))
        {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(pAdapter->GetDesc1(&desc)))
            {
                return desc.VendorId;
            }
        }

        return 0;
    }

    ColorBuffer& GetSwapChainBuffer(size_t i)
    {
        return gDisplayPlane[i];
    }

    ColorBuffer& GetSceneColorBuffer(size_t i)
    {
        return gSceneColorBuffer[i];
    }

    DepthBuffer& GetSceneDepthBuffer(size_t i)
    {
        return gSceneDepthBuffer[i];
    }

    D3D12_VIEWPORT GetDefaultViewPort()
    {
        D3D12_VIEWPORT viewport;
        viewport.TopLeftX = 0.0;
        viewport.TopLeftY = 0.0;
        viewport.Width = (float)gSceneColorBuffer[0].GetWidth();
        viewport.Height = (float)gSceneColorBuffer[0].GetHeight();
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        return viewport;
    }

    D3D12_RECT GetDefaultScissor()
    {
        D3D12_RECT scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = (LONG)gSceneColorBuffer[0].GetWidth();
        scissor.bottom = (LONG)gSceneColorBuffer[0].GetHeight();
        return scissor;
    }

    bool IsDeviceNvidia(ID3D12Device* pDevice)
    {
        return GetVendorIdFromDevice(pDevice) == vendorID_Nvidia;
    }

    bool IsDeviceAMD(ID3D12Device* pDevice)
    {
        return GetVendorIdFromDevice(pDevice) == vendorID_AMD;
    }

    bool IsDeviceIntel(ID3D12Device* pDevice)
    {
        return GetVendorIdFromDevice(pDevice) == vendorID_Intel;
    }

    void ResolutionToUINT(eResolution res, uint32_t& width, uint32_t& height)
    {
        switch (res)
        {
        default:
        case k720p:
            width = 1280;
            height = 720;
            break;
        case k900p:
            width = 1600;
            height = 900;
            break;
        case k1080p:
            width = 1920;
            height = 1080;
            break;
        case k1440p:
            width = 2560;
            height = 1440;
            break;
        case k1800p:
            width = 3200;
            height = 1800;
            break;
        case k2160p:
            width = 3840;
            height = 2160;
            break;
        }
    }

    void SetNativeResolution()
    {
        uint32_t NativeWidth, NativeHeight;

        ResolutionToUINT(eResolution((int)gRenderResolution), NativeWidth, NativeHeight);

        if (gRenderWidth == NativeWidth && gRenderHeight == NativeHeight)
            return;
        Utility::PrintMessage("Changing native resolution to %ux%u", NativeWidth, NativeHeight);

        gRenderWidth = NativeWidth;
        gRenderHeight = NativeHeight;

        CommandQueueManager::GetInstance()->IdleGPU();

        ResizeSceneBuffer(NativeWidth, NativeHeight);
    }

    void SetDisplayResolution()
    {
#ifdef _GAMING_DESKTOP
        static int SelectedDisplayRes = gDisplayResolution;
        if (SelectedDisplayRes == gDisplayResolution)
            return;

        SelectedDisplayRes = gDisplayResolution;
        ResolutionToUINT((eResolution)SelectedDisplayRes, gDisplayWidth, gDisplayHeight);

        ResizeSwapChain(gDisplayWidth, gDisplayHeight);

        SetWindowPos(ghWnd, 0, 0, 0, gDisplayWidth, gDisplayHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
    }

    void InitializeSwapChain()
    {
        ASSERT(sSwapChain1 == nullptr, "Graphics has already been initialized");

        Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
        CheckHR(CreateDXGIFactory2(0, IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = gDisplayWidth;
        swapChainDesc.Height = gDisplayHeight;
        swapChainDesc.Format = SWAP_CHAIN_FORMAT;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
        fsSwapChainDesc.Windowed = TRUE;

        CheckHR(dxgiFactory->CreateSwapChainForHwnd(
            CommandQueueManager::GetInstance()->GetGraphicsQueue().GetCommandQueue(),
            ghWnd,
            &swapChainDesc,
            &fsSwapChainDesc,
            nullptr,
            sSwapChain1.GetAddressOf()));

#if CONDITIONALLY_ENABLE_HDR_OUTPUT
        {
            IDXGISwapChain4* swapChain = (IDXGISwapChain4*)sSwapChain1;
            ComPtr<IDXGIOutput> output;
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 outputDesc;
            UINT colorSpaceSupport;

            // Query support for ST.2084 on the display and set the color space accordingly
            if (SUCCEEDED(swapChain->GetContainingOutput(&output)) && SUCCEEDED(output.As(&output6)) &&
                SUCCEEDED(output6->GetDesc1(&outputDesc)) && outputDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 &&
                SUCCEEDED(swapChain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &colorSpaceSupport)) &&
                (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) &&
                SUCCEEDED(swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)))
            {
                gbEnableHDROutput = true;
            }
        }
#endif // End CONDITIONALLY_ENABLE_HDR_OUTPUT

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        {
            ID3D12Resource* displayPlane = nullptr;
            CheckHR(sSwapChain1->GetBuffer(i, IID_PPV_ARGS(&displayPlane)));
            gDisplayPlane[i].CreateFromSwapChain(L"Primary SwapChain Buffer", displayPlane);

            gSceneColorBuffer[i].Create(L"Scene Color Buffer", gRenderWidth, gRenderHeight, 1, HDR_FORMAT);
            gSceneDepthBuffer[i].Create(L"Scene Depth Buffer", gRenderWidth, gRenderHeight, 1, DSV_FORMAT);
        }
    }

    void ResizeSceneBuffer(uint32_t width, uint32_t height)
    {
        CommandQueueManager::GetInstance()->IdleGPU();

        gRenderWidth = width;
        gRenderHeight = height;

        Utility::PrintMessage("Changing Render resolution to %ux%u", width, height);

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
        {
            gSceneColorBuffer[i].Destroy();
            gSceneDepthBuffer[i].Destroy();

            gSceneColorBuffer[i].Create(L"Scene Color Buffer", width, height, 1, HDR_FORMAT);
            gSceneDepthBuffer[i].Create(L"Scene Depth Buffer", gRenderWidth, gRenderHeight, 1, DSV_FORMAT);
        }
           
        FrameContextManager::GetInstance()->OnResizeSceneBuffer(width, height);
    }

    void ResizeSwapChain(uint32_t width, uint32_t height)
    {
        CommandQueueManager::GetInstance()->IdleGPU();

        gDisplayWidth = width;
        gDisplayHeight = height;

        Utility::PrintMessage("Changing display resolution to %ux%u", width, height);

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
            gDisplayPlane[i].Destroy();

        ASSERT(sSwapChain1 != nullptr);
        CheckHR(sSwapChain1->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, width, height, SWAP_CHAIN_FORMAT, 0));

        for (uint32_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
        {
            ID3D12Resource* displayPlane = nullptr;
            CheckHR(sSwapChain1->GetBuffer(i, IID_PPV_ARGS(&displayPlane)));
            gDisplayPlane[i].CreateFromSwapChain(L"Primary SwapChain Buffer", displayPlane);
        }

        FrameContextManager::GetInstance()->OnResizeSwapChain(gDisplayWidth, gDisplayHeight);
    }

    void Present()
    {
        ZoneScoped;

        UINT PresentInterval = gEnableVSync ? std::min(4, (int)Math::Round(sFrameTime * 60.0f)) : 0;

        sSwapChain1->Present(PresentInterval, 0);

        int64_t CurrentTick = SystemTime::GetCurrentTick();

        if (gEnableVSync)
        {
            // With VSync enabled, the time step between frames becomes a multiple of 16.666 ms.  We need
            // to add logic to vary between 1 and 2 (or 3 fields).  This delta time also determines how
            // long the previous frame should be displayed (i.e. the present interval.)
            sFrameTime = (gLimitTo30Hz ? 2.0f : 1.0f) / 60.0f;
            if (gDropRandomFrames)
            {
                if (std::rand() % 50 == 0)
                    sFrameTime += (1.0f / 60.0f);
            }
        }
        else
        {
            // When running free, keep the most recent total frame time as the time step for
            // the next frame simulation.  This is not super-accurate, but assuming a frame
            // time varies smoothly, it should be close enough.
            sFrameTime = (float)SystemTime::TimeBetweenTicks(sFrameStartTick, CurrentTick);
        }

        sFrameStartTick = CurrentTick;

        ++sFrameIndex;

        SetNativeResolution();
        SetDisplayResolution();
    }

    uint64_t GetFrameCount()
    {
        return sFrameIndex;
    }

    float GetFrameTime()
    {
        return sFrameTime;
    }

    float GetFrameRate()
    {
        return sFrameTime == 0.0f ? 0.0f : 1.0f / sFrameTime;
    }


    void Initialize(bool requireDXRSupport)
    {
        uint32_t useDebugLayers = 0;
        CommandLineArgs::GetInteger(L"debug", useDebugLayers);
#if _DEBUG
        // Default to true for debug builds
        useDebugLayers = 1;
#endif

        DWORD dxgiFactoryFlags = 0;

        if (useDebugLayers)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugInterface;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugInterface.GetAddressOf()))))
            {
                debugInterface->EnableDebugLayer();

                uint32_t useGPUBasedValidation = 0;
                CommandLineArgs::GetInteger(L"gpu_debug", useGPUBasedValidation);
                if (useGPUBasedValidation)
                {
                    Microsoft::WRL::ComPtr<ID3D12Debug1> debugInterface1;
                    if (SUCCEEDED((debugInterface->QueryInterface(IID_PPV_ARGS(debugInterface1.GetAddressOf())))))
                    {
                        debugInterface1->SetEnableGPUBasedValidation(true);
                    }
                }
            }
            else
            {
                Utility::Print("WARNING:  Unable to enable D3D12 debug validation layer\n");
            }

#if _DEBUG
            Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
            {
                dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

                dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
                dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

                DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
                {
                    80 /* IDXGISwapChain::GetContainingOutput: The swapchain's adapter does not control the output on which the swapchain's window resides. */,
                };
                DXGI_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumIDs = _countof(hide);
                filter.DenyList.pIDList = hide;
                dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
            }
#endif
        }

        // Obtain the DXGI factory
        Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory;
        CheckHR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

        // Create the D3D graphics device
        Microsoft::WRL::ComPtr<IDXGIAdapter1> pAdapter;

        uint32_t bUseWarpDriver = false;
        CommandLineArgs::GetInteger(L"warp", bUseWarpDriver);

        uint32_t desiredVendor = GetDesiredGPUVendor();

        if (desiredVendor)
        {
            Utility::PrintMessage(L"Looking for a %s GPU\n", GPUVendorToString(desiredVendor));
        }

        // Temporary workaround because SetStablePowerState() is crashing
        D3D12EnableExperimentalFeatures(0, nullptr, nullptr, nullptr);

        if (!bUseWarpDriver)
        {
            SIZE_T MaxSize = 0;

            for (uint32_t Idx = 0; DXGI_ERROR_NOT_FOUND != dxgiFactory->EnumAdapters1(Idx, &pAdapter); ++Idx)
            {
                DXGI_ADAPTER_DESC1 desc;
                pAdapter->GetDesc1(&desc);

                // Is a software adapter?
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    continue;

                // Is this the desired vendor desired?
                if (desiredVendor != 0 && desiredVendor != desc.VendorId)
                    continue;

                // Can create a D3D12 device?
                if (FAILED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(gDevice.GetAddressOf()))))
                    continue;

                // Does support DXR if required?
                if (requireDXRSupport && !IsDirectXRaytracingSupported(gDevice.Get()))
                    continue;

                // By default, search for the adapter with the most memory because that's usually the dGPU.
                if (desc.DedicatedVideoMemory < MaxSize)
                    continue;

                MaxSize = desc.DedicatedVideoMemory;

                Utility::PrintMessage(L"Selected GPU:  %s (%u MB)\n", desc.Description, desc.DedicatedVideoMemory >> 20);
            }
        }

        if (requireDXRSupport && !gDevice)
        {
            Utility::PrintMessage("Unable to find a DXR-capable device. Halting.\n");
            __debugbreak();
        }

        if (gDevice == nullptr)
        {
            if (bUseWarpDriver)
                Utility::Print("WARP software adapter requested.  Initializing...\n");
            else
                Utility::Print("Failed to find a hardware adapter.  Falling back to WARP.\n");
            CheckHR(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(pAdapter.GetAddressOf())));
            CheckHR(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(gDevice.GetAddressOf())));
        }
#ifndef RELEASE
        else
        {
            bool DeveloperModeEnabled = false;

            // Look in the Windows Registry to determine if Developer Mode is enabled
            HKEY hKey;
            LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock", 0, KEY_READ, &hKey);
            if (result == ERROR_SUCCESS)
            {
                DWORD keyValue, keySize = sizeof(DWORD);
                result = RegQueryValueEx(hKey, L"AllowDevelopmentWithoutDevLicense", 0, NULL, (byte*)&keyValue, &keySize);
                if (result == ERROR_SUCCESS && keyValue == 1)
                    DeveloperModeEnabled = true;
                RegCloseKey(hKey);
            }

            WARN_IF_NOT(DeveloperModeEnabled, "Enable Developer Mode on Windows 10 to get consistent profiling results");

            // Prevent the GPU from overclocking or underclocking to get consistent timings
            if (DeveloperModeEnabled)
                gDevice->SetStablePowerState(TRUE);
        }
#endif	

#if _DEBUG
        ID3D12InfoQueue* pInfoQueue = nullptr;
        if (SUCCEEDED(gDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue))))
        {
            //pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);

            // Suppress whole categories of messages
            //D3D12_MESSAGE_CATEGORY Categories[] = {};

            // Suppress messages based on their severity level
            D3D12_MESSAGE_SEVERITY Severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            // Suppress individual messages by their ID
            D3D12_MESSAGE_ID DenyIds[] =
            {
                // This occurs when there are uninitialized descriptors in a descriptor table, even when a
                // shader does not access the missing descriptors.  I find this is common when switching
                // shader permutations and not wanting to change much code to reorder resources.
                //D3D12_MESSAGE_ID_INVALID_DESCRIPTOR_HANDLE,

                // Triggered when a shader does not export all color components of a render target, such as
                // when only writing RGB to an R10G10B10A2 buffer, ignoring alpha.
                D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_PS_OUTPUT_RT_OUTPUT_MISMATCH,

                // This occurs when a descriptor table is unbound even when a shader does not access the missing
                // descriptors.  This is common with a root signature shared between disparate shaders that
                // don't all need the same types of resources.
                //D3D12_MESSAGE_ID_COMMAND_LIST_DESCRIPTOR_TABLE_NOT_SET,

                // RESOURCE_BARRIER_DUPLICATE_SUBRESOURCE_TRANSITIONS
                (D3D12_MESSAGE_ID)1008,
            };

            D3D12_INFO_QUEUE_FILTER NewFilter = {};
            //NewFilter.DenyList.NumCategories = _countof(Categories);
            //NewFilter.DenyList.pCategoryList = Categories;
            NewFilter.DenyList.NumSeverities = _countof(Severities);
            NewFilter.DenyList.pSeverityList = Severities;
            NewFilter.DenyList.NumIDs = _countof(DenyIds);
            NewFilter.DenyList.pIDList = DenyIds;

            pInfoQueue->PushStorageFilter(&NewFilter);
            pInfoQueue->Release();
        }
#endif

        // We like to do read-modify-write operations on UAVs during post processing.  To support that, we
        // need to either have the hardware do typed UAV loads of R11G11B10_FLOAT or we need to manually
        // decode an R32_UINT representation of the same buffer.  This code determines if we get the hardware
        // load support.
        D3D12_FEATURE_DATA_D3D12_OPTIONS FeatureData = {};
        if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &FeatureData, sizeof(FeatureData))))
        {
            if (FeatureData.TypedUAVLoadAdditionalFormats)
            {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT Support =
                {
                    DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE
                };

                ASSERT(SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) &&
                        (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)

                //if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) &&
                //    (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                //{
                //    gTypedUAVLoadSupport_R11G11B10_FLOAT = true;
                //}

                //Support.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                //if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) &&
                //    (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                //{
                //    gTypedUAVLoadSupport_R16G16B16A16_FLOAT = true;
                //}
            }
        }
    }

    void Shutdown()
    {
#if defined(_GAMING_DESKTOP) && defined(_DEBUG)
        ID3D12DebugDevice* debugInterface;
        if (SUCCEEDED(gDevice->QueryInterface(&debugInterface)))
        {
            debugInterface->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            debugInterface->Release();
        }
#endif

        gDevice = nullptr;
    }
};
