#pragma once
#include "GraphicsContext.h"

class GpuResource;
class CommandQueue;

//enum eRenderPoint
//{
//    kBegin,
//    kProcess,
//    kEnd,
//};

struct MutiGraphicsCommand
{
    //eRenderPoint mRenderPoint;
    D3D12_COMMAND_LIST_TYPE mRenderType;
    Graphics::GraphicsContext::GraphicsTask mRenderTask;
};


class FrameContext : public NonCopyable
{
    friend class FrameContextManager;
public:
    ~FrameContext() {}
private:
    FrameContext() :
        mIsStartRenderTask(false), mCurRecordType((D3D12_COMMAND_LIST_TYPE)-1), mQueueProduce(nullptr)
    {
        ZeroMemory(mCurTaskFence, sizeof(mCurTaskFence));
    }

    void Flush(CommandQueue& queue, bool isTempTask);

    void RecordGraphicsTask();

    void Finish(bool waitForCompletion = false);

    void BeginRecordGraphicsTask(CommandList& commanList, size_t idx);
    void EndRecordGraphicsTask(CommandList* before, CommandList* after, bool isRear, bool isTemp); //resolve resource transition
    void ResolveResourceState(ResourceStateCache& curState, ResourceStateCache& afterState);

    void PrepareRevealBufferBegin(GraphicsCommandList& ghCommandList);
    void PrepareRevealBufferEnd(CommandList& ghCommandList);
private:
    bool mIsStartRenderTask;
    uint64_t mCurTaskFence[Graphics::NUM_RENDER_TASK_TYPE];
    D3D12_COMMAND_LIST_TYPE mCurRecordType;
    CommandQueue* mQueueProduce; 

    std::vector<MutiGraphicsCommand> mGraphicsTask;
    std::queue<std::future<Graphics::GraphicsContext::GraphicsTask::result_type>> mFutureQueue;
    std::vector<ID3D12CommandList*> mFinalCommandLists;
    std::map<size_t, ResourceStateCache> mFrameResourceStateCache;
};


class FrameContextManager : public Singleton<FrameContextManager>
{
    USE_SINGLETON;
    friend class FrameContext;
public:
    template<typename Renderable, typename ...Args>
    Renderable* RegisterContext(Args&& ...args)
    {
        static_assert(std::is_base_of_v<Graphics::MutiGraphicsContext, Renderable>);

        auto renderable = std::make_unique<Renderable>(std::forward<Args>(args)...);
        auto ptr = renderable.get();
        renderable->Initialize();
        mGraphicsContexts[mGraphicsContexts.size()] = std::move(renderable);
        return ptr;
    }

    void RegisterGlobalResource(const std::string& name, GpuResource*& resource)
    {
        WARN_IF(mGlobalResrouce.find(name) != mGlobalResrouce.end());
        mGlobalResrouce[name] = resource;
    }

    uint64_t CommitAsyncCopyTask(const Graphics::GraphicsContext::GraphicsTask& copyTask);
    uint64_t CommitAsyncComputeTask(const Graphics::GraphicsContext::GraphicsTask& computeTask);
    uint64_t CommitAsyncGraphicsTask(const Graphics::GraphicsContext::GraphicsTask& graphicsTask);

    ColorBuffer& GetCurrentSwapChain();
    ColorBuffer& GetCurrentSceneColorBuffer();
    DepthBuffer& GetCurrentSceneDepthBuffer();
    size_t GetCurFrameContextIdx() { return mCurFrameContextIdx; }

    void PushMutiGraphicsTask(const MutiGraphicsCommand& renderTask);

    void OnResizeSceneBuffer(uint32_t width, uint32_t height);
    void OnResizeSwapChain(uint32_t width, uint32_t height);
    void EndRender();
    void Render();
    void BeginRender();
    void ComitRenderTask();
private:
    FrameContextManager() { InitFrameContexts(); }
    ~FrameContextManager() { DestroyAllContexts(); }

    void InitFrameContexts();
    void DestroyAllContexts();

    CommandList* CreateCommandList(D3D12_COMMAND_LIST_TYPE type, std::list<std::unique_ptr<CommandList>>& toWhich);
    CommandList* RequireCommandList(D3D12_COMMAND_LIST_TYPE type);
    void DiscardUsedCommandLists(Graphics::RENDER_TASK_TYPE type, uint64_t fenceValue);
    CommandList* RequireTempCommandList(Graphics::RENDER_TASK_TYPE type, uint64_t finishFenceValue); // For Async Task

    FrameContext* GetCurFrameContext() const { return mFrameContexts[mCurFrameContextIdx].get(); }
private:
    size_t mCurFrameContextIdx = 0;
    std::vector<std::unique_ptr<FrameContext>> mFrameContexts;

    std::list<std::unique_ptr<CommandList>> mCommandListPool[Graphics::NUM_RENDER_TASK_TYPE];
    std::list<std::unique_ptr<CommandList>> mUsingCommandLists[Graphics::NUM_RENDER_TASK_TYPE];
    std::list<std::unique_ptr<CommandList>> mRetiredCommandLists[Graphics::NUM_RENDER_TASK_TYPE];

    std::unordered_map<size_t, std::unique_ptr<Graphics::MutiGraphicsContext>> mGraphicsContexts;
    std::unordered_map<std::string, GpuResource*> mGlobalResrouce;
};

#define GLOBAL_RES(name, resource) FrameContextManager::GetInstance()->RegisterGlobalResource(name, resource)
#define CURRENT_SWAP_CHAIN FrameContextManager::GetInstance()->GetCurrentSwapChain()
#define CURRENT_SCENE_COLOR_BUFFER FrameContextManager::GetInstance()->GetCurrentSceneColorBuffer()
#define CURRENT_SCENE_DEPTH_BUFFER FrameContextManager::GetInstance()->GetCurrentSceneDepthBuffer()
#define CURRENT_FARME_BUFFER_INDEX FrameContextManager::GetInstance()->GetCurFrameContextIdx()
#define REGISTER_CONTEXT(Renderable, ...) FrameContextManager::GetInstance()->RegisterContext<Renderable>(__VA_ARGS__);
#define PUSH_MUTIRENDER_TASK(...) FrameContextManager::GetInstance()->PushMutiGraphicsTask(__VA_ARGS__);
