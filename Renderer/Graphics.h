#pragma once
#include "CoreHeader.h"
#include "DescriptorHandle.h"

class DescriptorAllocator;
class ColorBuffer;

namespace Graphics
{
#ifndef RELEASE
	extern const GUID WKPDID_D3DDebugObjectName;
#endif
    enum eResolution { k600p, k720p, k900p, k1080p, k1440p, k1800p, k2160p };

	void Initialize(bool requireDXRSupport = false);
    void InitializeSwapChain();
	void Shutdown();

    void ResizeSwapChain(uint32_t width, uint32_t height);
    void ResizeSceneBuffer(uint32_t width, uint32_t height);
    void Present();

    // Returns the number of elapsed frames since application start
    uint64_t GetFrameCount();

    // The amount of time elapsed during the last completed frame.  The CPU and/or
    // GPU may be idle during parts of the frame.  The frame time measures the time
    // between calls to present each frame.
    float GetFrameTime();

    // The total number of frames per second
    float GetFrameRate();

    void ResolutionToUINT(eResolution res, uint32_t& width, uint32_t& height);

    ColorBuffer& GetSwapChainBuffer(size_t i);
    ColorBuffer& GetSceneColorBuffer(size_t i);

    bool IsDeviceNvidia(ID3D12Device* pDevice);
    bool IsDeviceAMD(ID3D12Device* pDevice);
    bool IsDeviceIntel(ID3D12Device* pDevice);

    DescriptorHandle AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count = 1);
    void DeAllocateDescriptor(DescriptorHandle& handle, UINT count);

    extern HWND ghWnd;
    extern Microsoft::WRL::ComPtr<ID3D12Device> gDevice;
    extern DescriptorAllocator gDescriptorAllocator[];

    extern D3D_FEATURE_LEVEL gD3DFeatureLevel;
    extern bool gTypedUAVLoadSupport_R11G11B10_FLOAT; // assume false
    extern bool gTypedUAVLoadSupport_R16G16B16A16_FLOAT; // assume false

    extern uint32_t gDisplayWidth;
    extern uint32_t gDisplayHeight;
    extern uint32_t gRenderWidth;
    extern uint32_t gRenderHeight;
    extern eResolution gDisplayResolution;
    extern eResolution gRenderResolution;
    extern bool gEnableHDROutput; // assume false

    extern bool gEnableVSync;
    extern bool gLimitTo30Hz;
    extern bool gDropRandomFrames;
};
