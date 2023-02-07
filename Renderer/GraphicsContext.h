#pragma once
#include "CoreHeader.h"
#include "Common.h"
#include "CommandList.h"

namespace Graphics
{
    class GraphicsContext : public NonCopyable
    {
    public:
        using GraphicsTask = std::function<CommandList* (CommandList*)>;
    private:
        template<typename F, typename C, typename ...Args>
        static std::enable_if_t<std::is_member_function_pointer_v<std::remove_reference_t<F>>, GraphicsTask>
            PushGraphicsTaskBindImpl(F&& f, C* clazz, Args&&... args)
        {
            auto func = std::bind(f, clazz, std::placeholders::_1, std::forward<Args>(args)...);
            return [=](GraphicsTask::argument_type commandList) -> GraphicsTask::result_type
            {
                return func(commandList);
                //return std::invoke(f, clazz, commandList, args...);
            };
        }

        // for test just comment
        //template<typename F, typename ...Args>
        //static GraphicsTask PushGraphicsTaskBindImpl(F&& f, Args&&... args)
        //{
        //    auto func = std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Args>(args)...);
        //    return [=](GraphicsTask::argument_type firstArg) -> GraphicsTask::result_type
        //    {
        //        func(firstArg);
        //    };
        //}
    public:
        template<typename F, typename ...Args>
        static GraphicsTask PushGraphicsTaskBind(F&& f, Args&&... args)
        {
            ASSERT(std::this_thread::get_id() == main_thread_id);
            return PushGraphicsTaskBindImpl(std::forward<F>(f), std::forward<Args>(args)...);
        }
    protected:
        GraphicsContext() { RegisterGlobal(); }
        ~GraphicsContext() {}

        virtual void RegisterGlobal() {}
    };


    class AsyncContext
    {
    public:
        using GraphicsTask = GraphicsContext::GraphicsTask;

        virtual bool isValid() const { return *mContextFence == 0 || *mContextFence == *mVersionId; }

        void ForceWaitContext()
        {
            WaitAsyncFence();
            *mVersionId = *mContextFence;
        }
    protected:
        AsyncContext()
        {
            mContextFence = std::make_shared<uint64_t>(0);
            mVersionId = std::make_shared<uint64_t>(0);
        }
        ~AsyncContext() {}

        template<typename F, typename ...Args>
        void PushGraphicsTaskSync(F&& f, Args&&... args)
        {
            CommitGraphicsTask(GraphicsContext::PushGraphicsTaskBind(std::forward<F>(f), std::forward<Args>(args)...));
            ForceWaitContext();
        }

        template<typename F, typename ...Args>
        void PushGraphicsTaskAsync(F&& f, Args&&... args)
        {
            if (!AsyncContext::isValid())
                ForceWaitContext();

            CommitGraphicsTaskWithCallback(GraphicsContext::PushGraphicsTaskBind(std::forward<F>(f), std::forward<Args>(args)...),
                [this](uint64_t fence) { *mVersionId = fence; });
        }

        virtual void WaitAsyncFence() { ASSERT(false, "Not Impl Err"); }

        virtual void CommitGraphicsTask(const GraphicsTask& graphicsTask) { ASSERT(false, "Not Impl Err"); }
        virtual void CommitGraphicsTaskWithCallback(const GraphicsTask& graphicsTask, const std::function<void(uint64_t)>& callback) { ASSERT(false, "Not Impl Err"); }
        virtual void RegisterFecneEvent(const std::function<void(uint64_t)>& callback) { ASSERT(false, "Not Impl Err"); }
    protected:
        std::shared_ptr<uint64_t> mContextFence;
        std::shared_ptr<uint64_t> mVersionId;
    };


    class CopyContext : public AsyncContext
    {
    protected:
        CopyContext() {}
        ~CopyContext() {}

        virtual void WaitAsyncFence() override;

        virtual void CommitGraphicsTask(const GraphicsTask& graphicsTask) override;
        virtual void CommitGraphicsTaskWithCallback(const GraphicsTask& graphicsTask, const std::function<void(uint64_t)>& callback) override;
        virtual void RegisterFecneEvent(const std::function<void(uint64_t)>& callback) override;
    };


    class ComputeContext : public AsyncContext
    {
    protected:
        ComputeContext() {}
        ~ComputeContext() {}

        virtual void WaitAsyncFence() override;

        virtual void CommitGraphicsTask(const GraphicsTask& graphicsTask) override;
        virtual void CommitGraphicsTaskWithCallback(const GraphicsTask& graphicsTask, const std::function<void(uint64_t)>& callback) override;
        virtual void RegisterFecneEvent(const std::function<void(uint64_t)>& callback) override;
    };


    class MutiGraphicsContext : public GraphicsContext
    {
        friend class FrameContextManager;
        friend struct std::default_delete<MutiGraphicsContext>;
    public:
        MutiGraphicsContext() {}
        ~MutiGraphicsContext() {}
    protected:
        virtual void Initialize() = 0;

        virtual void Update(float deltaTime) {}

        virtual void BeginRender() {}
        virtual void Render() = 0;
        virtual void EndRender() {}

        virtual void OnResizeSwapChain(uint32_t width, uint32_t height) {}
        virtual void OnResizeSceneBuffer(uint32_t width, uint32_t height) {}
    };

}
