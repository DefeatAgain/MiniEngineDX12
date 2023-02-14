#include "FrameContext.h"
#include "CommandQueue.h"
#include "CommandList.h"
#include "Graphics.h"
#include "PixelBuffer.h"
#include "Utils/Hash.h"
#include "Utils/ThreadPoolExecutor.h"

//namespace CollectionHelper
//{
//	template <>
//	inline void CollectImpl2(std::string&& name, GpuResource*& res)
//	{
//		FrameContextManager::GetInstance()->RegisterGlobalResource(name, res);
//	}
//
//	template <>
//	inline void CollectImpl2(const char*&& name, GpuResource*& res)
//	{
//		FrameContextManager::GetInstance()->RegisterGlobalResource(name, res);
//	}
//};

void FrameContext::RecordGraphicsTask()
{
	ZoneScoped;

	FrameContextManager* frameContextMgr = FrameContextManager::GetInstance();

	for (size_t i = 0; i < mGraphicsTask.size(); i++)
	{
		auto& renderTask = mGraphicsTask[i];

		D3D12_COMMAND_LIST_TYPE& renderTaskType = renderTask.mRenderType;
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetQueue(renderTaskType);

		// test this frame hit fence
		uint64_t fence = mCurTaskFence[Graphics::GetQueueType(renderTaskType)];
		queue.WaitForFence(fence);

		CommandList* commandList = frameContextMgr->RequireCommandList(renderTaskType);
		
		BeginRecordGraphicsTask(*commandList, i);

		mFutureQueue.emplace(Utility::gThreadPoolExecutor.Submit(std::ref(renderTask.mRenderTask), commandList));
		if (mCurRecordType == -1)
			mQueueProduce = &queue;

		else if (renderTaskType != mCurRecordType)
		{
			Flush(queue, true);
			queue.StallForProducer(*mQueueProduce);
		}

		mCurRecordType = renderTaskType;
		mQueueProduce = &queue;
	}
}

void FrameContext::Flush(CommandQueue& queue, bool isTempTask)
{
	ZoneScoped;

	using namespace Graphics;

	mFinalCommandLists.clear();
	mFinalCommandLists.reserve(mFutureQueue.size());

	CommandList* beforeList = nullptr;
	CommandList* afterList = nullptr;
	while (!mFutureQueue.empty())
	{
		CommandList* futureList = mFutureQueue.front().get();
		afterList = futureList;

		EndRecordGraphicsTask(beforeList, afterList, mFutureQueue.size() == 1, isTempTask);

		beforeList = futureList;
		mFinalCommandLists.emplace_back(futureList->GetDeviceCommandList());
		mFutureQueue.pop();
	}

	RENDER_TASK_TYPE taskType = GetQueueType(queue.mType);
	mCurTaskFence[taskType] = queue.ExecuteCommandLists(mFinalCommandLists.data(), mFinalCommandLists.size());

	FrameContextManager::GetInstance()->DiscardUsedCommandLists(taskType, mCurTaskFence[taskType]);

	if (!isTempTask)
	{
		mIsStartRenderTask = false;
		mFrameResourceStateCache.clear();
		mGraphicsTask.clear();
	}
}

void FrameContext::Finish(bool waitForCompletion)
{
	ZoneScoped;

	using namespace Graphics;

	if (mQueueProduce)
		Flush(*mQueueProduce, false);

	if (waitForCompletion)
	{
		CommandQueue& graphicsQueue = CommandQueueManager::GetInstance()->GetGraphicsQueue();
		CommandQueue& computeQueue = CommandQueueManager::GetInstance()->GetComputeQueue();
		CommandQueue& copyQueue = CommandQueueManager::GetInstance()->GetCopyQueue();

		graphicsQueue.WaitForFence(mCurTaskFence[QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_DIRECT>]);
		computeQueue.WaitForFence(mCurTaskFence[QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COMPUTE>]);
		copyQueue.WaitForFence(mCurTaskFence[QUEUE_TYPE<D3D12_COMMAND_LIST_TYPE_COPY>]);
	}

	//EngineProfiling::EndBlock(this);
}

void FrameContext::BeginRecordGraphicsTask(CommandList& commanList, size_t idx)
{
	commanList.mCommandListIndex = idx;

	if (idx == 0)
		commanList.PIXSetMarker(L"Start RenderTask");

	if (!mIsStartRenderTask && commanList.mType == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
		mIsStartRenderTask = true;

		PrepareRevealBufferBegin(commanList.GetGraphicsCommandList());
	}
}

void FrameContext::EndRecordGraphicsTask(CommandList* beforeList, CommandList* afterList, bool isRear, bool isTemp)
{
	if (!beforeList && !isRear)
	{
		mFrameResourceStateCache = std::move(afterList->mResourceStateCache);
	}
	else
	{
		// berfore state already in mFrameResourceStateCache
		std::map<size_t, ResourceStateCache>& afterResCache = afterList->mResourceStateCache;
		for (auto kvAfterIter = afterResCache.begin(); kvAfterIter != afterResCache.end();)
		{
			auto curFindIter = mFrameResourceStateCache.find(kvAfterIter->first);
			if (curFindIter == mFrameResourceStateCache.end())
			{
				auto releaseIter = kvAfterIter++;
				mFrameResourceStateCache.insert(afterResCache.extract(releaseIter));
				continue;
			}

			ResourceStateCache& curStateCache = curFindIter->second;
			ResourceStateCache& afterStateCache = kvAfterIter->second;
			if (curStateCache.mStateCurrent != afterStateCache.mStateCurrent)
			{
				beforeList->TransitionResource(curStateCache, afterStateCache);
			}
			kvAfterIter++;
		}
	}

	if (beforeList)
		beforeList->BeforeCommandListSubmit();
	if (isRear)
	{
		if (mIsStartRenderTask && !isTemp)
		{
			PrepareRevealBufferEnd(*afterList);
		}

		// now resource has real state
		for (auto& resourceCache : mFrameResourceStateCache)
			resourceCache.second.mGpuResource.mUsageState = resourceCache.second.mStateCurrent;

		afterList->FlushResourceBarriers();

		if (!isTemp)
			afterList->PIXSetMarker(L"Finish RenderTask");

		afterList->BeforeCommandListSubmit();
	}
}

void FrameContext::PrepareRevealBufferBegin(GraphicsCommandList& ghCommandList)
{
	ColorBuffer& swapChain = FrameContextManager::GetInstance()->GetCurrentSwapChain();
	ghCommandList.TransitionResource(swapChain, D3D12_RESOURCE_STATE_RENDER_TARGET, true);

	ColorBuffer& sceneBuffer = FrameContextManager::GetInstance()->GetCurrentSceneColorBuffer();
	ghCommandList.TransitionResource(sceneBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	ghCommandList.ClearColor(sceneBuffer);
	ghCommandList.SetRenderTarget(sceneBuffer.GetRTV());
	ghCommandList.SetViewportAndScissor(0, 0, sceneBuffer.GetWidth(), sceneBuffer.GetHeight());
	//{
	//	ghCommandList.SetViewportAndScissor(0, 0, swapChain.GetWidth(), swapChain.GetHeight());
	//	ghCommandList.SetRenderTarget(swapChain.GetRTV());
	//}
}

void FrameContext::PrepareRevealBufferEnd(CommandList& ghCommandList)
{
	ColorBuffer& renderTarget = FrameContextManager::GetInstance()->GetCurrentSwapChain();
	auto stateCacheIter = mFrameResourceStateCache.find(Utility::HashState((GpuResource*)&renderTarget));
	ASSERT(stateCacheIter != mFrameResourceStateCache.end());
	ResourceStateCache presentStateCache = stateCacheIter->second;
	presentStateCache.mStateCurrent = D3D12_RESOURCE_STATE_PRESENT;
	ghCommandList.TransitionResource(stateCacheIter->second, presentStateCache);
}


// -- FrameContextManager --
void FrameContextManager::InitFrameContexts()
{
	for (size_t i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
	{
		mFrameContexts.emplace_back(new FrameContext);
	}
}

uint64_t FrameContextManager::CommitAsyncCopyTask(const Graphics::GraphicsContext::GraphicsTask& copyTask)
{
	using namespace Graphics;
	CommandQueue& queue = CommandQueueManager::GetInstance()->GetCopyQueue();
	CommandList* commandList = RequireTempCommandList(RENDER_TASK_TYPE_COPY, queue.GetCurrentFenceValue() + 1);
	copyTask(commandList);
	commandList->UpdateResourceState();
	commandList->BeforeCommandListSubmit();
	return queue.ExecuteCommandLists((ID3D12CommandList**)commandList->GetDeviceCommandListOf(), 1);
}

uint64_t FrameContextManager::CommitAsyncComputeTask(const Graphics::GraphicsContext::GraphicsTask& computeTask)
{
	using namespace Graphics;
	CommandQueue& queue = CommandQueueManager::GetInstance()->GetComputeQueue();
	CommandList* commandList = RequireTempCommandList(RENDER_TASK_TYPE_COMPUTE, queue.GetCurrentFenceValue() + 1);
	computeTask(commandList);
	commandList->UpdateResourceState();
	commandList->BeforeCommandListSubmit();
	return queue.ExecuteCommandLists((ID3D12CommandList**)commandList->GetDeviceCommandListOf(), 1);
}

uint64_t FrameContextManager::CommitAsyncGraphicsTask(const Graphics::GraphicsContext::GraphicsTask& graphicsTask)
{
	using namespace Graphics;
	CommandQueue& queue = CommandQueueManager::GetInstance()->GetGraphicsQueue();
	CommandList* commandList = RequireTempCommandList(RENDER_TASK_TYPE_GRAPHICS, queue.GetCurrentFenceValue() + 1);
	graphicsTask(commandList);
	commandList->UpdateResourceState();
	commandList->BeforeCommandListSubmit();
	return queue.ExecuteCommandLists((ID3D12CommandList**)commandList->GetDeviceCommandListOf(), 1);
}

ColorBuffer& FrameContextManager::GetCurrentSwapChain()
{
	return Graphics::GetSwapChainBuffer(mCurFrameContextIdx);
}

ColorBuffer& FrameContextManager::GetCurrentSceneColorBuffer()
{
	return Graphics::GetSceneColorBuffer(mCurFrameContextIdx);
}

DepthBuffer& FrameContextManager::GetCurrentSceneDepthBuffer()
{
	return Graphics::GetSceneDepthBuffer(mCurFrameContextIdx);
}

void FrameContextManager::PushMutiGraphicsTask(const MutiGraphicsCommand& renderTask)
{
	GetCurFrameContext()->mGraphicsTask.push_back(std::move(renderTask));
}

void FrameContextManager::OnResizeSceneBuffer(uint32_t width, uint32_t height)
{
	for (auto& renderContext : mGraphicsContexts)
		renderContext.second->OnResizeSceneBuffer(width, height);
}

void FrameContextManager::OnResizeSwapChain(uint32_t width, uint32_t height)
{
	mCurFrameContextIdx = 0;

	for (auto& renderContext : mGraphicsContexts)
		renderContext.second->OnResizeSwapChain(width, height);
}

void FrameContextManager::EndRender()
{
	ZoneScoped;

	for (size_t i = 0; i < mGraphicsContexts.size(); i++)
		mGraphicsContexts[i]->EndRender();
}

void FrameContextManager::Render()
{
	ZoneScoped;

	for (size_t i = 0; i < mGraphicsContexts.size(); i++)
		mGraphicsContexts[i]->Render();
}

void FrameContextManager::BeginRender()
{
	ZoneScoped;

	for (size_t i = 0; i < mGraphicsContexts.size(); i++)
		mGraphicsContexts[i]->BeginRender();
}

void FrameContextManager::ComitRenderTask()
{
	ZoneScoped;

	GetCurFrameContext()->RecordGraphicsTask();
	GetCurFrameContext()->Finish();

	mCurFrameContextIdx = (mCurFrameContextIdx + 1) % SWAP_CHAIN_BUFFER_COUNT;
}

void FrameContextManager::DestroyAllContexts()
{
}

CommandList* FrameContextManager::CreateCommandList(D3D12_COMMAND_LIST_TYPE type, std::list<std::unique_ptr<CommandList>>& toWhich)
{
	using namespace Graphics;

	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT:
		return toWhich.emplace_back(new GraphicsCommandList).get();
	case D3D12_COMMAND_LIST_TYPE_COMPUTE:
		return toWhich.emplace_back(new ComputeCommandList).get();
	case D3D12_COMMAND_LIST_TYPE_COPY:
		return toWhich.emplace_back(new CopyCommandList).get();
	default:
		ASSERT(false, "check list type");
		return toWhich.emplace_back(new CommandList(type)).get();
	}
}

CommandList* FrameContextManager::RequireCommandList(D3D12_COMMAND_LIST_TYPE type)
{
	using namespace Graphics;

	for (int i = 0; i < NUM_RENDER_TASK_TYPE; i++)
	{
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetQueue(GetQueueType(((RENDER_TASK_TYPE)i)));
		for (auto beg = mRetiredCommandLists[i].begin(); beg != mRetiredCommandLists[i].end();)
		{
			if (queue.IsFenceComplete((*beg)->mRetiredFenceValue))
			{
				auto removeIter = beg++;
				mCommandListPool[i].splice(mCommandListPool[i].end(), mRetiredCommandLists[i], removeIter);
			}
			else
			{
				beg++;
			}
		}
	}

	RENDER_TASK_TYPE typeAlloc = GetQueueType(type);
	auto& pooledList = mCommandListPool[typeAlloc];
	auto& usingList = mUsingCommandLists[typeAlloc];
	if (pooledList.empty())
	{
		CommandList* newList = CreateCommandList(type, usingList);
		newList->Initialize();
		return newList;
	}
	
	usingList.splice(usingList.end(), pooledList, pooledList.begin());
	return usingList.back()->Reset();
}

void FrameContextManager::DiscardUsedCommandLists(Graphics::RENDER_TASK_TYPE type, uint64_t fenceValue)
{
	for (auto& usingList : mUsingCommandLists[type])
		usingList->mRetiredFenceValue = fenceValue;

	mRetiredCommandLists[type].splice(mRetiredCommandLists[type].end(), mUsingCommandLists[type]);
}

CommandList* FrameContextManager::RequireTempCommandList(Graphics::RENDER_TASK_TYPE type, uint64_t finishFenceValue)
{
	using namespace Graphics;

	auto& pooledList = mCommandListPool[type];
	auto& retiredList = mRetiredCommandLists[type];
	if (pooledList.empty())
	{
		CommandList* tempCommandList = CreateCommandList(GetQueueType(type), retiredList);
		tempCommandList->Initialize();
		tempCommandList->mRetiredFenceValue = finishFenceValue;
		return tempCommandList;
	}

	retiredList.splice(retiredList.end(), pooledList, pooledList.begin());
	retiredList.back()->mRetiredFenceValue = finishFenceValue;
	return retiredList.back()->Reset();
}
