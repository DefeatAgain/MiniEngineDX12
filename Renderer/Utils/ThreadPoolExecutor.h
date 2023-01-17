#pragma once
#include <future>
#include <thread>
#include <vector>

#include "LinkedBlockQueue.h"
#include "DebugUtils.h"

class ThreadPoolExecutor
{
	using task_type = std::function<void()>;
public:
	ThreadPoolExecutor(size_t thread_cout): 
		mIsRunning(true)
	{
		Init(thread_cout);
	}

	~ThreadPoolExecutor() { Shutdown(); }
public:
	template<typename F, typename ...Args>
	decltype(auto) Submit(F&& f, Args&& ...args)
	{
		using RetType = std::invoke_result_t<F, Args...>;
		std::shared_ptr<std::packaged_task<RetType()>> task = std::make_shared<std::packaged_task<RetType()>>(
			std::bind<RetType, F, Args...>(std::forward<F>(f), std::forward<Args>(args)...));
		auto future = task->get_future();
		mTaskList.Put([=]() { task->operator()(); });
		return future;
	}

	void Shutdown() 
	{ 
		for (auto& t : mThreads)
		{
			for (size_t i = 0; i < mThreads.size(); i++)
				Submit([=]() {mIsRunning = false; });

			if (t.joinable())
				t.join();
		}
	}
private:
	void Init(size_t thread_cout)
	{
		for (size_t i = 0; i < thread_cout; i++)
		{
			mThreads.emplace_back([this]()
			{
				while (mIsRunning)
					mTaskList.Get()();
			});
		}
	}
private:
	bool mIsRunning;
	LinkedBlockQueue<task_type> mTaskList;
	std::vector<std::thread> mThreads;
};

namespace Utility
{
	extern ThreadPoolExecutor gThreadPoolExecutor;
}
