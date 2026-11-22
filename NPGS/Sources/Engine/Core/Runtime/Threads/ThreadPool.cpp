#include "ThreadPool.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include <sched.h>

_NPGS_BEGIN
_RUNTIME_BEGIN
_THREAD_BEGIN

namespace
{
    int GetAllowedCpuCount()
    {
        // Grow the mask for systems with more CPUs than CPU_SETSIZE. Workers
        // inherit this mask; let the scheduler place them instead of pinning.
        std::vector<cpu_set_t> AllowedCpus(1);
        for (;;)
        {
            const std::size_t MaskSize = AllowedCpus.size() * sizeof(cpu_set_t);
            if (sched_getaffinity(0, MaskSize, AllowedCpus.data()) == 0)
            {
                const int CpuCount = CPU_COUNT_S(MaskSize, AllowedCpus.data());
                if (CpuCount == 0)
                {
                    throw std::runtime_error("The process has no allowed CPUs.");
                }
                return CpuCount;
            }

            if (errno != EINVAL)
            {
                throw std::system_error(errno, std::generic_category(), "sched_getaffinity");
            }
            AllowedCpus.resize(AllowedCpus.size() * 2);
        }
    }
}

// ThreadPool implementations
// --------------------------
FThreadPool::FThreadPool()
    :
    _Terminate(false),
    _kMaxThreadCount(GetAllowedCpuCount())
{
    _Threads.reserve(_kMaxThreadCount);
    try
    {
        for (int i = 0; i != _kMaxThreadCount; ++i)
        {
            _Threads.emplace_back([this]() -> void
            {
                while (true)
                {
                    std::function<void()> Task;
                    {
                        std::unique_lock<std::mutex> Mutex(_Mutex);
                        _Condition.wait(Mutex, [this]() -> bool { return !_Tasks.empty() || _Terminate; });
                        if (_Terminate && _Tasks.empty())
                        {
                            return;
                        }

                        Task = std::move(_Tasks.front());
                        _Tasks.pop();
                    }

                    Task();
                }
            });
        }
    }
    catch (...)
    {
        Terminate();
        throw;
    }
}

FThreadPool::~FThreadPool()
{
    Terminate();
}

void FThreadPool::Terminate()
{
    {
        std::unique_lock<std::mutex> Mutex(_Mutex);
        _Terminate = true;
    }
    _Condition.notify_all();
    for (auto& Thread : _Threads)
    {
        if (Thread.joinable())
        {
            Thread.join();
        }
    }
}

FThreadPool* FThreadPool::GetInstance()
{
    static FThreadPool kInstance;
    return &kInstance;
}

_THREAD_END
_RUNTIME_END
_NPGS_END
