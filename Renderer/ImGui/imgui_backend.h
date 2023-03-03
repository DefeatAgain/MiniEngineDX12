// dear imgui: Renderer Backend for DirectX12
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'D3D12_GPU_DESCRIPTOR_HANDLE' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.

// Important: to compile on 32-bit systems, this backend requires code to be compiled with '#define ImTextureID ImU64'.
// See imgui_impl_dx12.cpp file for details.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#pragma once
#include "../Texture.h"

struct ImDrawData;
class ImGuiBackend;

namespace ImGuiRenderer
{
    inline ImGuiBackend* gImguiContext = nullptr;
}


class ImGuiBackend : public Graphics::MutiGraphicsContext
{
public:
    ImGuiBackend() {}
    virtual ~ImGuiBackend() { Destroy(); }

    virtual void OnResizeSwapChain(uint32_t width, uint32_t height) override { SetViewSize(width, height); }
    virtual void Initialize() override;
    virtual void Render() override;
    void Update(float deltaTime);

    void SetViewSize(uint32_t width, uint32_t height);
private:
    void Destroy();

    void ImGuiCreateFontsTexture();

    void ImGuiSetupRenderState(ImDrawData* draw_data, GraphicsCommandList& ghCommandList);

    CommandList* RenderTask(CommandList* commandList);
private:
    Texture mFontTexture;
    DescriptorHandle mTextureGpu;
};
