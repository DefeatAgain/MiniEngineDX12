#pragma once
#include <mutex>
#include <list>
#include <condition_variable>

template<typename T>
class LinkedBlockQueue
{
public:
	LinkedBlockQueue() {}
	~LinkedBlockQueue() {}

	void Put(const T& t);
	T Get();
	bool Get(T& t);
	size_t size();
private:
	std::mutex mLock;
	std::list<T> mList;
	std::condition_variable cv_get;
	std::condition_variable cv_put;
};

template<typename T>
inline void LinkedBlockQueue<T>::Put(const T& t)
{
	std::unique_lock<std::mutex> locker(mLock);
	while (mList.max_size() == mList.size())
		cv_put.wait(locker);

	mList.push_back(t);
	cv_get.notify_all();
}

template<typename T>
inline T LinkedBlockQueue<T>::Get()
{
	std::unique_lock<std::mutex> lock_(mLock);
	while (mList.empty())
		cv_get.wait(lock_);

	T t = std::move(mList.front());
	mList.pop_front();
	cv_put.notify_all();
	return t;
}

template<typename T>
inline bool LinkedBlockQueue<T>::Get(T& t)
{
	std::unique_lock<std::mutex> lock_(mLock);
	if (mList.empty())
		return false;

	t = mList.front();
	mList.pop_front();
	cv_put.notify_all();
	return true;
}

template<typename T>
inline size_t LinkedBlockQueue<T>::size()
{
	std::unique_lock<std::mutex> lock_(mLock);
	return mList.size();
}
