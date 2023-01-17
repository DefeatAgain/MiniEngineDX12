#pragma once
#include "CoreHeader.h"
#include "Common.h"


class CommandQueue : public NonCopyable
{
    friend class CommandQueueManager;
    friend class FrameContext;
    friend class FrameContextManager;
public:
    using EventType = std::function<void(uint64_t)>;
public:
    CommandQueue(D3D12_COMMAND_LIST_TYPE Type);
    ~CommandQueue() {}

    void Create(ID3D12Device* pDevice);

    void Shutdown();

    bool IsReady() { return mCommandQueue != nullptr; }

    uint64_t IncrementFence();

    uint64_t GetCurrentFenceValue() { return mNextFenceValue; }

    bool IsFenceComplete(uint64_t fenceValue);

    void StallForProducer(CommandQueue& producer);

    void WaitForFence(uint64_t fenceValue);

    void WaitForIdle();

    void SelectQueueEvent();

    void RegisterQueueEvent(uint64_t fenceValue, const EventType& event_);

    ID3D12CommandQueue* GetCommandQueue() { return mCommandQueue.Get(); }

    const D3D12_COMMAND_LIST_TYPE GetType() const { return mType; }
private:
    uint64_t ExecuteCommandLists(ID3D12CommandList** pList, size_t numLists);
private:
    const D3D12_COMMAND_LIST_TYPE mType;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    uint64_t mNextFenceValue;
    HANDLE mFenceEventHandle;

    std::queue<std::pair<uint64_t, std::vector<EventType>>> mEventFunc;
};


class CommandQueueManager : public Singleton<CommandQueueManager>
{
    USE_SINGLETON;
private:
    CommandQueueManager(): 
        mGraphicsQueue(D3D12_COMMAND_LIST_TYPE_DIRECT),
        mComputeQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE),
        mCopyQueue(D3D12_COMMAND_LIST_TYPE_COPY)
    { Create(); }
    ~CommandQueueManager() { Shutdown(); }

    void Create();
public:
    void Shutdown();

    CommandQueue& GetGraphicsQueue() { return mGraphicsQueue; }
    CommandQueue& GetComputeQueue() { return mComputeQueue; }
    CommandQueue& GetCopyQueue() { return mCopyQueue; }

    // Test to see if a fence has already been reached
    bool IsFenceComplete(D3D12_COMMAND_LIST_TYPE type, uint64_t fenceValue) { return GetQueue(type).IsFenceComplete(fenceValue); }

    CommandQueue& GetQueue(D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT);

    // The CPU will wait for all command queues to empty (so that the GPU is idle)
    void IdleGPU();

    void SelectQueueEvent();
private:
    CommandQueue mGraphicsQueue;
    CommandQueue mComputeQueue;
    CommandQueue mCopyQueue;
};
