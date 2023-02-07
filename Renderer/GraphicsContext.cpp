#include "GraphicsContext.h"
#include "CommandQueue.h"
#include "FrameContext.h"

namespace Graphics
{
	void CopyContext::WaitAsyncFence()
	{
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetCopyQueue();
		queue.WaitForFence(*mContextFence);
	}

	void CopyContext::CommitGraphicsTask(const GraphicsTask& graphicsTask)
	{
		*mContextFence = FrameContextManager::GetInstance()->CommitAsyncCopyTask(graphicsTask);
	}

	void CopyContext::CommitGraphicsTaskWithCallback(const GraphicsTask& graphicsTask, const std::function<void(uint64_t)>& callback)
	{
		*mContextFence = FrameContextManager::GetInstance()->CommitAsyncCopyTask(graphicsTask);
		RegisterFecneEvent(callback);
	}

	void CopyContext::RegisterFecneEvent(const std::function<void(uint64_t)>& callback)
	{
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetCopyQueue();
		queue.RegisterQueueEvent(*mContextFence, callback);
	}


	// -- ComputeContext --
	void ComputeContext::WaitAsyncFence()
	{
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetComputeQueue();
		queue.WaitForFence(*mContextFence);
	}

	void ComputeContext::CommitGraphicsTask(const GraphicsTask& graphicsTask)
	{
		*mContextFence = FrameContextManager::GetInstance()->CommitAsyncComputeTask(graphicsTask);
	}

	void ComputeContext::CommitGraphicsTaskWithCallback(const GraphicsTask& graphicsTask, const std::function<void(uint64_t)>& callback)
	{
		*mContextFence = FrameContextManager::GetInstance()->CommitAsyncComputeTask(graphicsTask);
		RegisterFecneEvent(callback);
	}

	void ComputeContext::RegisterFecneEvent(const std::function<void(uint64_t)>& callback)
	{
		CommandQueue& queue = CommandQueueManager::GetInstance()->GetComputeQueue();
		CommandQueueManager::GetInstance()->GetComputeQueue().RegisterQueueEvent(*mContextFence, callback);
	}
}
