#include "TextRenderer.h"
#include "Graphics.h"
#include "GraphicsResource.h"
#include "Texture.h"
#include "SystemTime.h"
#include "CommandList.h"
#include "PixelBuffer.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "ShaderCompositor.h"
#include "FrameContext.h"
#include "Math/VectorMath.h"
#include "Utils/FileUtility.h"
#include "Utils/DebugUtils.h"
#include "Fonts/consola24.h"

#include <malloc.h>

using namespace Graphics;
using namespace Math;
using namespace std;

namespace
{
    RootSignature* sRootSignature;
    GraphicsPipelineState* sTextPSO;
    GraphicsPipelineState* sShadowPSO;
}

namespace TextRenderer
{
    class Font
    {
    public:
        Font()
        {
            m_NormalizeXCoord = 0.0f;
            m_NormalizeYCoord = 0.0f;
            m_FontLineSpacing = 0.0f;
            m_AntialiasRange = 0.0f;
            m_FontHeight = 0;
            m_BorderSize = 0;
            m_TextureWidth = 0;
            m_TextureHeight = 0;
        }

        ~Font()
        {
            m_Dictionary.clear();

            DEALLOC_DESCRIPTOR_GPU(mTextureGpu, 1);
        }

        void LoadFromBinary(const wchar_t* fontName, const uint8_t* pBinary, const size_t binarySize)
        {
            (fontName);

            // We should at least use this to assert that we have a complete file
            (binarySize);

            struct FontHeader
            {
                char FileDescriptor[8];		// "SDFFONT\0"
                uint8_t  majorVersion;		// '1'
                uint8_t  minorVersion;		// '0'
                uint16_t borderSize;		// Pixel empty space border width
                uint16_t textureWidth;		// Width of texture buffer
                uint16_t textureHeight;		// Height of texture buffer
                uint16_t fontHeight;		// Font height in 12.4
                uint16_t advanceY;			// Line height in 12.4
                uint16_t numGlyphs;			// Glyph count in texture
                uint16_t searchDist;		// Range of search space 12.4
            };

            FontHeader* header = (FontHeader*)pBinary;
            m_NormalizeXCoord = 1.0f / (header->textureWidth * 16);
            m_NormalizeYCoord = 1.0f / (header->textureHeight * 16);
            m_FontHeight = header->fontHeight;
            m_FontLineSpacing = (float)header->advanceY / (float)header->fontHeight;
            m_BorderSize = header->borderSize * 16;
            m_AntialiasRange = (float)header->searchDist / header->fontHeight;
            uint16_t textureWidth = header->textureWidth;
            uint16_t textureHeight = header->textureHeight;
            uint16_t NumGlyphs = header->numGlyphs;

            const wchar_t* wcharList = (wchar_t*)(pBinary + sizeof(FontHeader));
            const Glyph* glyphData = (Glyph*)(wcharList + NumGlyphs);
            const void* texelData = glyphData + NumGlyphs;

            for (uint16_t i = 0; i < NumGlyphs; ++i)
                m_Dictionary[wcharList[i]] = glyphData[i];

            m_Texture.mName = L"Textrender Font";
            m_Texture.Create2D(textureWidth, textureWidth, textureHeight, DXGI_FORMAT_R8_SNORM, texelData);
            mTextureGpu = ALLOC_DESCRIPTOR_GPU(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
            Graphics::gDevice->CopyDescriptorsSimple(1, mTextureGpu, m_Texture.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            Utility::PrintMessage("Loaded SDF font:  %ls (ver. %d.%d)", fontName, header->majorVersion, header->minorVersion);
        }

        bool Load(const wstring& fileName)
        {
            Utility::ByteArray ba = Utility::ReadFileSync(fileName);

            if (ba->size() == 0)
            {
                ASSERT(false, "Cannot open file %ls", fileName.c_str());
                return false;
            }

            LoadFromBinary(fileName.c_str(), ba->data(), ba->size());

            return true;
        }

        // Each character has an XY start offset, a width, and they all share the same height
        struct Glyph
        {
            uint16_t x, y, w;
            int16_t bearing;
            uint16_t advance;
        };

        const Glyph* GetGlyph(wchar_t ch) const
        {
            auto it = m_Dictionary.find(ch);
            return it == m_Dictionary.end() ? nullptr : &it->second;
        }

        // Get the texel height of the font in 12.4 fixed point
        uint16_t GetHeight(void) const { return m_FontHeight; }

        // Get the size of the border in 12.4 fixed point
        uint16_t GetBorderSize(void) const { return m_BorderSize; }

        // Get the line advance height given a certain font size
        float GetVerticalSpacing(float size) const { return size * m_FontLineSpacing; }

        // Get the texture object
        const Texture& GetTexture(void) const { return m_Texture; }
        DescriptorHandle GetTextureGpuSRV(void) const { return mTextureGpu; }

        float GetXNormalizationFactor() const { return m_NormalizeXCoord; }
        float GetYNormalizationFactor() const { return m_NormalizeYCoord; }

        // Get the range in terms of height values centered on the midline that represents a pixel
        // in screen space (according to the specified font size.)
        // The pixel alpha should range from 0 to 1 over the height range 0.5 +/- 0.5 * aaRange.
        float GetAntialiasRange(float size) const { return Max(1.0f, size * m_AntialiasRange); }

    private:
        float m_NormalizeXCoord;
        float m_NormalizeYCoord;
        float m_FontLineSpacing;
        float m_AntialiasRange;
        uint16_t m_FontHeight;
        uint16_t m_BorderSize;
        uint16_t m_TextureWidth;
        uint16_t m_TextureHeight;
        Texture m_Texture;
        DescriptorHandle mTextureGpu;
        map<wchar_t, Glyph> m_Dictionary;
    };

    map< wstring, unique_ptr<Font> > LoadedFonts;

    const Font* GetOrLoadFont(const wstring& filename)
    {
        auto fontIter = LoadedFonts.find(filename);
        if (fontIter != LoadedFonts.end())
            return fontIter->second.get();

        Font* newFont = new Font();
        if (filename == L"default")
            newFont->LoadFromBinary(L"default", g_pconsola24, sizeof(g_pconsola24));
        else
            newFont->Load(L"Fonts/" + filename + L".fnt");
        LoadedFonts[filename].reset(newFont);
        return newFont;
    }
} // namespace TextRenderer

void TextRenderer::Initialize(void)
{
    Graphics::AddRSSTask([]()
        {
            sRootSignature = GET_RSO(L"TextRenderer");
            sRootSignature->Reset(3, 1);
            sRootSignature->InitStaticSampler(0, SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
            sRootSignature->GetParam(0).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
            sRootSignature->GetParam(1).InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
            sRootSignature->GetParam(2).InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
            sRootSignature->Finalize(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ADD_SHADER("TextVS", L"TextVS.hlsl", kVS);
            ADD_SHADER("TextAntialiasPS", L"TextAntialiasPS.hlsl", kPS);
        }
    );
   
    Graphics::AddPSTask([]()
        {
            PipeLineStateManager* psoInstance = PipeLineStateManager::GetInstance();

            sTextPSO = GET_GPSO(L"TextRender: Text PSO");
            sShadowPSO = GET_GPSO(L"TextRender: Text Shadow PSO");

            // The glyph vertex description.  One vertex will correspond to a single character.
            D3D12_INPUT_ELEMENT_DESC vertElem[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT     , 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
                { "TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
            };
            DXGI_FORMAT renderFormat = SWAP_CHAIN_FORMAT;
            sTextPSO->SetRootSignature(*sRootSignature);
            sTextPSO->SetRasterizerState(Graphics::RasterizerTwoSided);
            sTextPSO->SetBlendState(Graphics::BlendPreMultiplied);
            sTextPSO->SetDepthStencilState(Graphics::DepthStateDisabled);
            sTextPSO->SetInputLayout(ARRAYSIZE(vertElem), vertElem);
            sTextPSO->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            sTextPSO->SetVertexShader(GET_SHADER("TextVS"));
            sTextPSO->SetPixelShader(GET_SHADER("TextAntialiasPS"));
            sTextPSO->SetRenderTargetFormats(1, &renderFormat, DXGI_FORMAT_UNKNOWN);
            sTextPSO->Finalize();

            *sShadowPSO = *sTextPSO;
            sTextPSO->SetRenderTargetFormats(1, &renderFormat, DXGI_FORMAT_UNKNOWN);
            sTextPSO->Finalize();
        }
    );
}

void TextRenderer::Shutdown(void)
{
    LoadedFonts.clear();
}

TextContext::TextContext(float ViewWidth, float ViewHeight)
{
    m_HDR = FALSE;
    m_CurrentFont = nullptr;
    m_ViewWidth = ViewWidth;
    m_ViewHeight = ViewHeight;

    // Transform from text view space to clip space.
    const float vpX = 0.0f;
    const float vpY = 0.0f;
    const float twoDivW = 2.0f / ViewWidth;
    const float twoDivH = 2.0f / ViewHeight;
    m_VSParams.ViewportTransform = Vector4(twoDivW, -twoDivH, -vpX * twoDivW - 1.0f, vpY * twoDivH + 1.0f);

    // The font texture dimensions are still unknown
    m_VSParams.NormalizeX = 1.0f;
    m_VSParams.NormalizeY = 1.0f;

    mDrawBuffer = "";

    ResetSettings();
}

void TextContext::ResetSettings(void)
{
    m_EnableShadow = true;
    ResetCursor(0.0f, 0.0f);
    m_ShadowOffsetX = 0.05f;
    m_ShadowOffsetY = 0.05f;
    m_PSParams.ShadowHardness = 0.5f;
    m_PSParams.ShadowOpacity = 1.0f;
    m_PSParams.TextColor = Color(1.0f, 1.0f, 1.0f, 1.0f);

    m_VSConstantBufferIsStale = true;
    m_PSConstantBufferIsStale = true;
    m_TextureIsStale = true;

    SetFont(L"default", 24.0f);
}

void  TextContext::SetLeftMargin(float x) { m_LeftMargin = x; }
void  TextContext::SetCursorX(float x) { m_TextPosX = x; }
void  TextContext::SetCursorY(float y) { m_TextPosY = y; }
void  TextContext::NewLine(void) { m_TextPosX = m_LeftMargin; m_TextPosY += m_LineHeight; }
float TextContext::GetLeftMargin(void) { return m_LeftMargin; }
float TextContext::GetCursorX(void) { return m_TextPosX; }
float TextContext::GetCursorY(void) { return m_TextPosY; }


void TextContext::ResetCursor(float x, float y)
{
    m_LeftMargin = x;
    m_TextPosX = x;
    m_TextPosY = y;
}

void TextContext::EnableDropShadow(bool enable, CommandList& commandList)
{
    if (m_EnableShadow == enable)
        return;

    m_EnableShadow = enable;

    commandList.SetPipelineState(m_EnableShadow ? sShadowPSO[m_HDR] : sTextPSO[m_HDR]);
}

void TextContext::SetShadowOffset(float xPercent, float yPercent)
{
    m_ShadowOffsetX = xPercent;
    m_ShadowOffsetY = yPercent;
    m_PSParams.ShadowOffsetX = m_CurrentFont->GetHeight() * m_ShadowOffsetX * m_VSParams.NormalizeX;
    m_PSParams.ShadowOffsetY = m_CurrentFont->GetHeight() * m_ShadowOffsetY * m_VSParams.NormalizeY;
    m_PSConstantBufferIsStale = true;
}

void TextContext::SetShadowParams(float opacity, float width)
{
    m_PSParams.ShadowHardness = 1.0f / width;
    m_PSParams.ShadowOpacity = opacity;
    m_PSConstantBufferIsStale = true;
}

void TextContext::SetColor(Color c)
{
    m_PSParams.TextColor = c;
    m_PSConstantBufferIsStale = true;
}

float TextContext::GetVerticalSpacing(void)
{
    return m_LineHeight;
}

void TextContext::SetFont(const wstring& fontName, float size)
{
    // If that font is already set or doesn't exist, return.
    const TextRenderer::Font* NextFont = TextRenderer::GetOrLoadFont(fontName);
    if (NextFont == m_CurrentFont || NextFont == nullptr)
    {
        if (size > 0.0f)
            SetTextSize(size);

        return;
    }

    m_CurrentFont = NextFont;

    // Check to see if a new size was specified
    if (size > 0.0f)
        m_VSParams.TextSize = size;

    // Update constants directly tied to the font or the font size
    m_LineHeight = NextFont->GetVerticalSpacing(m_VSParams.TextSize);
    m_VSParams.NormalizeX = m_CurrentFont->GetXNormalizationFactor();
    m_VSParams.NormalizeY = m_CurrentFont->GetYNormalizationFactor();
    m_VSParams.Scale = m_VSParams.TextSize / m_CurrentFont->GetHeight();
    m_VSParams.DstBorder = m_CurrentFont->GetBorderSize() * m_VSParams.Scale;
    m_VSParams.SrcBorder = m_CurrentFont->GetBorderSize();
    m_PSParams.ShadowOffsetX = m_CurrentFont->GetHeight() * m_ShadowOffsetX * m_VSParams.NormalizeX;
    m_PSParams.ShadowOffsetY = m_CurrentFont->GetHeight() * m_ShadowOffsetY * m_VSParams.NormalizeY;
    m_PSParams.HeightRange = m_CurrentFont->GetAntialiasRange(m_VSParams.TextSize);
    m_VSConstantBufferIsStale = true;
    m_PSConstantBufferIsStale = true;
    m_TextureIsStale = true;
}

void TextContext::SetTextSize(float size)
{
    if (m_VSParams.TextSize == size)
        return;

    m_VSParams.TextSize = size;
    m_VSConstantBufferIsStale = true;

    if (m_CurrentFont != nullptr)
    {
        m_PSParams.HeightRange = m_CurrentFont->GetAntialiasRange(m_VSParams.TextSize);
        m_VSParams.Scale = m_VSParams.TextSize / m_CurrentFont->GetHeight();
        m_VSParams.DstBorder = m_CurrentFont->GetBorderSize() * m_VSParams.Scale;
        m_PSConstantBufferIsStale = true;
        m_LineHeight = m_CurrentFont->GetVerticalSpacing(size);
    }
    else
        m_LineHeight = 0.0f;
}

void TextContext::SetViewSize(float ViewWidth, float ViewHeight)
{
    m_ViewWidth = ViewWidth;
    m_ViewHeight = ViewHeight;

    const float vpX = 0.0f;
    const float vpY = 0.0f;
    const float twoDivW = 2.0f / ViewWidth;
    const float twoDivH = 2.0f / ViewHeight;

    // Essentially transform from screen coordinates to to clip space with W = 1.
    m_VSParams.ViewportTransform = Vector4(twoDivW, -twoDivH, -vpX * twoDivW - 1.0f, vpY * twoDivH + 1.0f);
    m_VSConstantBufferIsStale = true;
}

CommandList* TextContext::RenderTask(CommandList* commandList, BOOL enableHDR)
{
    GraphicsCommandList& graphicsCL = commandList->GetGraphicsCommandList().Begin(L"TextContext");
    // begin
    ResetSettings();

    m_HDR = (BOOL)enableHDR;

    ColorBuffer& swapChain = FrameContextManager::GetInstance()->GetCurrentSwapChain();
    graphicsCL.ExceptResourceBeginState(swapChain, D3D12_RESOURCE_STATE_RENDER_TARGET);
    graphicsCL.SetViewportAndScissor(0, 0, swapChain.GetWidth(), swapChain.GetHeight());
    graphicsCL.SetRenderTarget(swapChain.GetRTV());
    graphicsCL.SetPipelineState(*sTextPSO);
    graphicsCL.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // render
    SetRenderState(graphicsCL);

    void* stackMem = _malloca((mDrawBuffer.size() + 1) * 16);
    TextVert* vbPtr = Math::AlignUp((TextVert*)stackMem, 16);
    UINT primCount = FillVertexBuffer(vbPtr, mDrawBuffer.c_str(), 1, mDrawBuffer.size());

    if (primCount > 0)
    {
        graphicsCL.SetDynamicVB(0, primCount, sizeof(TextVert), vbPtr);
        graphicsCL.DrawInstanced(4, primCount);
    }

    _freea(stackMem);

    // end
    m_VSConstantBufferIsStale = true;
    m_PSConstantBufferIsStale = true;
    m_TextureIsStale = true;

    mDrawBuffer.clear();

    commandList->Finish();
    return commandList;
}

void TextContext::Initialize()
{
    TextRenderer::Initialize();
}

void TextContext::Render()
{
    if (mDrawBuffer.empty())
        return;

    PUSH_MUTIRENDER_TASK({ D3D12_COMMAND_LIST_TYPE_DIRECT, PushGraphicsTaskBind(&TextContext::RenderTask, this, FALSE) });
}

void TextContext::SetRenderState(GraphicsCommandList& commandList)
{
    WARN_IF(nullptr == m_CurrentFont, "Attempted to draw text without a font");

    if (m_VSConstantBufferIsStale)
    {
        commandList.SetDynamicConstantBufferView(0, sizeof(m_VSParams), &m_VSParams);
        m_VSConstantBufferIsStale = false;
    }

    if (m_PSConstantBufferIsStale)
    {
        commandList.SetDynamicConstantBufferView(1, sizeof(m_PSParams), &m_PSParams);
        m_PSConstantBufferIsStale = false;
    }

    if (m_TextureIsStale)
    {
        commandList.SetDescriptorTable(2, 0, m_CurrentFont->GetTextureGpuSRV());
        m_TextureIsStale = false;
    }
}

// These are made with templates to handle char and wchar_t simultaneously.
UINT TextContext::FillVertexBuffer(TextVert volatile* verts, const char* str, size_t stride, size_t slen)
{
    UINT charsDrawn = 0;

    const float UVtoPixel = m_VSParams.Scale;

    float curX = m_TextPosX;
    float curY = m_TextPosY;

    const uint16_t texelHeight = m_CurrentFont->GetHeight();

    const char* iter = str;
    for (size_t i = 0; i < slen; ++i)
    {
        wchar_t wc = (stride == 2 ? *(wchar_t*)iter : *iter);
        iter += stride;

        // Terminate on null character (this really shouldn't happen with string or wstring)
        if (wc == L'\0')
            break;

        // Handle newlines by inserting a carriage return and line feed
        if (wc == L'\n')
        {
            curX = m_LeftMargin;
            curY += m_LineHeight;
            continue;
        }

        const TextRenderer::Font::Glyph* gi = m_CurrentFont->GetGlyph(wc);

        // Ignore missing characters
        if (nullptr == gi)
            continue;

        verts->X = curX + (float)gi->bearing * UVtoPixel;
        verts->Y = curY;
        verts->U = gi->x;
        verts->V = gi->y;
        verts->W = gi->w;
        verts->H = texelHeight;
        ++verts;

        // Advance the cursor position
        curX += (float)gi->advance * UVtoPixel;
        ++charsDrawn;
    }

    m_TextPosX = curX;
    m_TextPosY = curY;

    return charsDrawn;
}

void TextContext::DrawString(const std::wstring& str)
{
    DrawString(Utility::WideStringToCSTR(str));
    //SetRenderState();

    //void* stackMem = _malloca((str.size() + 1) * 16);
    //TextVert* vbPtr = Math::AlignUp((TextVert*)stackMem, 16);
    //UINT primCount = FillVertexBuffer(vbPtr, (char*)str.c_str(), 2, str.size());

    //if (primCount > 0)
    //{
    //    m_Context.SetDynamicVB(0, primCount, sizeof(TextVert), vbPtr);
    //    m_Context.DrawInstanced(4, primCount);
    //}

    //_freea(stackMem);
}

void TextContext::DrawString(const std::string& str)
{
    mDrawBuffer += str;
}

void TextContext::DrawFormattedString(const wchar_t* format, ...)
{
    wchar_t buffer[256];
    va_list ap;
    va_start(ap, format);
    vswprintf(buffer, 256, format, ap);
    va_end(ap);
    DrawString(Utility::WideStringToCSTR(buffer));
}

void TextContext::DrawFormattedString(const char* format, ...)
{
    char buffer[256];
    va_list ap;
    va_start(ap, format);
    vsprintf_s(buffer, 256, format, ap);
    va_end(ap);
    DrawString(string(buffer));
}
