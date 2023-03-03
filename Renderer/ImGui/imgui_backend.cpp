// dear imgui: Renderer Backend for DirectX12
// This needs to be used along with a Platform Backend (e.g. Win32)

#include "imgui_backend.h"
#include "imgui.h"
#include "../Graphics.h"
#include "../GraphicsResource.h"
#include "../Texture.h"
#include "../CommandList.h"
#include "../PixelBuffer.h"
#include "../PipelineState.h"
#include "../RootSignature.h"
#include "../ShaderCompositor.h"
#include "../FrameContext.h"
#include "../Math/VectorMath.h"
#include "../Fonts/CousineRegular.h"

namespace
{
    RootSignature* sRootSignature = nullptr;
    GraphicsPipelineState* sGraphicsPipelineState = nullptr;
}

namespace ImGuiRenderer
{
    void Initialize()
    {
        Graphics::AddRSSTask([]()
        {
            // Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling.
            FLOAT borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            D3D12_SAMPLER_DESC staticSampler = {};
            staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            staticSampler.MipLODBias = 0.f;
            staticSampler.MaxAnisotropy = 0;
            staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            staticSampler.MinLOD = 0.f;
            staticSampler.MaxLOD = 0.f;
            staticSampler.BorderColor[4] = 0.0f;

            sRootSignature = GET_RSO(L"ImGuiRenderer");
            sRootSignature->Reset(2, 1);
            sRootSignature->InitStaticSampler(0, staticSampler, D3D12_SHADER_VISIBILITY_PIXEL);
            sRootSignature->GetParam(0).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
            sRootSignature->GetParam(1).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
            sRootSignature->Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                     D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

            ADD_SHADER("ImGuiVS", L"ImGuiVS.hlsl", kVS);
            ADD_SHADER("ImGuiPS", L"ImGuiPS.hlsl", kPS);
        });

        Graphics::AddPSTask([]()
        {
            sGraphicsPipelineState = GET_GPSO(L"ImGuiRender: ImGui PSO");

        // Create the input layout
            D3D12_INPUT_ELEMENT_DESC local_layout[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)IM_OFFSETOF(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)IM_OFFSETOF(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)IM_OFFSETOF(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };
            DXGI_FORMAT renderFormat = SWAP_CHAIN_FORMAT;
            sGraphicsPipelineState->SetRootSignature(*sRootSignature);
            sGraphicsPipelineState->SetRasterizerState(Graphics::RasterizerTwoSided);
            sGraphicsPipelineState->SetBlendState(Graphics::BlendTraditional);
            sGraphicsPipelineState->SetDepthStencilState(Graphics::DepthStateDisabled);
            sGraphicsPipelineState->SetInputLayout(ARRAYSIZE(local_layout), local_layout);
            sGraphicsPipelineState->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            sGraphicsPipelineState->SetVertexShader(GET_SHADER("ImGuiVS"));
            sGraphicsPipelineState->SetPixelShader(GET_SHADER("ImGuiPS"));
            sGraphicsPipelineState->SetRenderTargetFormats(1, &renderFormat, DXGI_FORMAT_UNKNOWN);
            sGraphicsPipelineState->Finalize();
        });
    }
}

void ImGuiBackend::Initialize()
{
    ImGuiRenderer::Initialize();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiCreateFontsTexture();

    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    io.BackendPlatformUserData = (void*)this;
    io.BackendPlatformName = "imgui_impl_win32";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)

    ImGui::GetMainViewport()->PlatformHandleRaw = (void*)Graphics::ghWnd;
    SetViewSize(Graphics::gDisplayWidth, Graphics::gDisplayHeight);

    // Setup backend capabilities flags
    io.BackendRendererUserData = (void*)this;
    io.BackendRendererName = "imgui_impl_dx12";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
}

void ImGuiBackend::Render()
{
    ImGui::Render();

    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(&ImGuiBackend::RenderTask, this) });
}

void ImGuiBackend::Update(float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaTime;

    ImGui::NewFrame();
    io.Framerate = Graphics::GetFrameRate();
}

void ImGuiBackend::SetViewSize(uint32_t width, uint32_t height)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = width;
    io.DisplaySize.y = height;
}

void ImGuiBackend::Destroy()
{
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;

    ImGui::DestroyContext();
}

void ImGuiBackend::ImGuiCreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->AddFontFromMemoryCompressedTTF(g_CousineRegularFont_compressed_data, g_CousineRegularFont_compressed_size, 18.0f);
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    mFontTexture.Create2D(4 * width, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels);
    mTextureGpu = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    Graphics::gDevice->CopyDescriptorsSimple(1, mTextureGpu, mFontTexture.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    static_assert(sizeof(ImTextureID) >= sizeof(uint64_t), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
    io.Fonts->SetTexID(mTextureGpu);
}

void ImGuiBackend::ImGuiSetupRenderState(ImDrawData* draw_data, GraphicsCommandList& ghCommandList)
{
    __declspec(align(256)) struct VERTEX_CONSTANT_BUFFER_DX12
    {
        Math::Matrix4 mvp;
    };
    // 
    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
    VERTEX_CONSTANT_BUFFER_DX12 vertex_constant_buffer;
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    vertex_constant_buffer.mvp.SetX(Math::Vector4(2.0f / (R - L), 0.0f, 0.0f, 0.0f));
    vertex_constant_buffer.mvp.SetY(Math::Vector4(0.0f, 2.0f / (T - B), 0.0f, 0.0f));
    vertex_constant_buffer.mvp.SetZ(Math::Vector4(0.0f, 0.0f, 0.5f, 0.0f));
    vertex_constant_buffer.mvp.SetW(Math::Vector4((R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f));

    // Setup viewport
    ColorBuffer& currentSwapChain = CURRENT_SWAP_CHAIN;
    ghCommandList.SetViewport(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
    ghCommandList.ExceptResourceBeginState(currentSwapChain, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ghCommandList.SetRenderTarget(currentSwapChain.GetRTV());
    ghCommandList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ghCommandList.SetPipelineState(*sGraphicsPipelineState);
    //ghCommandList.SetRootSignature(*sRootSignature);
    ghCommandList.SetDynamicConstantBufferView(0, sizeof(VERTEX_CONSTANT_BUFFER_DX12), &vertex_constant_buffer);

    // Setup blend factor
    const Color blend_factor(0.f, 0.f, 0.f, 0.f );
    ghCommandList.SetBlendFactor(blend_factor);
}

CommandList* ImGuiBackend::RenderTask(CommandList* commandList)
{
    GraphicsCommandList& ghCommandList = commandList->GetGraphicsCommandList().Begin(L"ImGuiBackend");
    // Avoid rendering when minimized
    ImDrawData* draw_data = ImGui::GetDrawData();

    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return commandList;

    // Setup desired DX state
    ImGuiSetupRenderState(draw_data, ghCommandList);

    // Render command lists
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        ghCommandList.SetDynamicVB(0, cmd_list->VtxBuffer.Size, sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
        ghCommandList.SetDynamicIB(cmd_list->IdxBuffer.Size, cmd_list->IdxBuffer.Data);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGuiSetupRenderState(draw_data, ghCommandList);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply Scissor/clipping rectangle, Bind texture, Draw
                const D3D12_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                ghCommandList.SetDescriptorTable(1, pcmd->GetTexID());
                ghCommandList.SetScissor((LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y);
                ghCommandList.DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset, pcmd->VtxOffset);
            }
        }
    }

    ghCommandList.Finish();
    return commandList;
}
