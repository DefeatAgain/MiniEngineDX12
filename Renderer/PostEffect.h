#pragma once
#include "GraphicsContext.h"

namespace PostEffectRender
{

}

class PostEffect : public Graphics::MutiGraphicsContext
{
public:
    PostEffect(float bufferWidth, float bufferHeight);
    ~PostEffect() {}

    virtual void Initialize() override;

    virtual void Update(float deltaTime) {}

    virtual void BeginRender() {}
    virtual void Render() override;
    virtual void EndRender() {}

    CommandList* RenderTaskToneMapping(CommandList* commandList);

    //virtual void OnResizeSwapChain(uint32_t width, uint32_t height) {}
    virtual void OnResizeSceneBuffer(uint32_t width, uint32_t height) override;
};

