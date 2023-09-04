#pragma once
#include "GraphicsContext.h"

class PostEffect;

namespace PostEffectRenderer
{
    extern PostEffect* gPostEffectContext;
}

class PostEffect : public Graphics::MutiGraphicsContext
{
public:
    PostEffect();
    ~PostEffect() {}

    virtual void Initialize() override;

    virtual void Update(float deltaTime);

    virtual void BeginRender() {}
    virtual void Render() override;
    virtual void EndRender() {}

    CommandList* RenderTaskToneMapping(CommandList* commandList);

    virtual void OnResizeSceneBuffer(uint32_t width, uint32_t height) override;
private:
    DescriptorHandle mTextureGpuHandles;
};

