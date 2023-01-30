#include "CommandQueue.h"
#include "Utils/DebugUtils.h"
#include "Graphics.h"

CommandQueue::CommandQueue(D3D12_COMMAND_LIST_TYPE type) :
    mType(type),
    mCommandQueue(nullptr),
    mFence(nullptr),
    mNextFenceValue(0)
{
}

void CommandQueue::Create(ID3D12Device* pDevice)
{
    ASSERT(!mCommandQueue);

    std::wstring queueNamePrefix = Graphics::GetQueueName(mType);

    D3D12_COMMAND_QUEUE_DESC QueueDesc{};
    QueueDesc.Type = mType;
    QueueDesc.NodeMask = 0;
    CheckHR(pDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(mCommandQueue.GetAddressOf())));
    mCommandQueue->SetName((queueNamePrefix + L" Queue").c_str());

    CheckHR(pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.GetAddressOf())));
    mFence->SetName((queueNamePrefix + L" Fence").c_str());
    mFence->Signal(0);

    mFenceEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ASSERT(mFenceEventHandle != NULL);
}

void CommandQueue::Shutdown()
{
    CloseHandle(mFenceEventHandle);
}

uint64_t CommandQueue::IncrementFence()
{
    mCommandQueue->Signal(mFence.Get(), ++mNextFenceValue);
    return mNextFenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
    return fenceValue <= mFence->GetCompletedValue();
}

void CommandQueue::StallForProducer(CommandQueue& producer)
{
    ASSERT(producer.mNextFenceValue > 0);
    mCommandQueue->Wait(producer.mFence.Get(), producer.mNextFenceValue);
}

void CommandQueue::WaitForFence(uint64_t fenceValue)
{
    if (IsFenceComplete(fenceValue))
        return;

    mFence->SetEventOnCompletion(fenceValue, mFenceEventHandle);
    WaitForSingleObject(mFenceEventHandle, INFINITE);
}

void CommandQueue::WaitForIdle()
{
    WaitForFence(mNextFenceValue);
}

void CommandQueue::SelectQueueEvent()
{
    uint64_t curCompletedValue = mFence->GetCompletedValue();
    while (!mEventFunc.empty())
    {
        auto& frontEvent = mEventFunc.front();
        if (curCompletedValue < frontEvent.first)
            break;

        for (auto& event_ : frontEvent.second)
            event_(frontEvent.first);

        mEventFunc.pop();
    }
}

void CommandQueue::RegisterQueueEvent(uint64_t fenceValue, const EventType& event_)
{
    if (mEventFunc.empty() || mEventFunc.back().first != fenceValue)
    {
        mEventFunc.emplace(std::make_pair(fenceValue, std::vector<EventType>{ std::move(event_) }));
    }
    else
    {
        mEventFunc.back().second.emplace_back(std::move(event_));
    }
}

uint64_t CommandQueue::ExecuteCommandLists(ID3D12CommandList** pList, size_t numLists)
{
    mCommandQueue->ExecuteCommandLists(numLists, pList);

    // Signal the next fence value (with the GPU)
    return IncrementFence();
}

void CommandQueueManager::Create()
{
    mGraphicsQueue.Create(Graphics::gDevice.Get());
    mComputeQueue.Create(Graphics::gDevice.Get());
    mCopyQueue.Create(Graphics::gDevice.Get());
}

void CommandQueueManager::Shutdown()
{
    mGraphicsQueue.Shutdown();
    mComputeQueue.Shutdown();
    mCopyQueue.Shutdown();
}

CommandQueue& CommandQueueManager::GetQueue(D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
    case D3D12_COMMAND_LIST_TYPE_COMPUTE: return mComputeQueue;
    case D3D12_COMMAND_LIST_TYPE_COPY: return mCopyQueue;
    default: return mGraphicsQueue;
    }
    return mGraphicsQueue;
}

void CommandQueueManager::IdleGPU()
{
    mGraphicsQueue.WaitForIdle();
    mComputeQueue.WaitForIdle();
    mCopyQueue.WaitForIdle();
}

void CommandQueueManager::SelectQueueEvent()
{
    ZoneScoped;

    mGraphicsQueue.SelectQueueEvent();
    mComputeQueue.SelectQueueEvent();
    mCopyQueue.SelectQueueEvent();
}
