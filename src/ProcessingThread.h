#pragma once

#include <libtcspc/tcspc.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Pumps a libtcspc buffer's downstream on its own thread and records how the
// pumping ended. The thread is started in the constructor and halted and
// joined in the destructor.
class ProcessingThread {
  public:
    enum class Outcome { Completed, Halted, Failed };

    // Invoked once, on the processing thread, just before it exits. Must not
    // call back into this ProcessingThread.
    using FinishedCallback =
        std::function<void(Outcome, std::string const &message)>;

    ProcessingThread(tcspc::buffer_accessor accessor,
                     FinishedCallback onFinished);
    ~ProcessingThread();
    ProcessingThread(ProcessingThread const &) = delete;
    ProcessingThread &operator=(ProcessingThread const &) = delete;

    // Makes pump() return without flushing; no effect if pumping has already
    // ended. Idempotent.
    void halt() noexcept;

    // True once pump() has returned or thrown on the processing thread.
    [[nodiscard]] bool finished() const noexcept;

  private:
    void Run();

    tcspc::buffer_accessor accessor_;
    FinishedCallback onFinished_;
    std::atomic<bool> finished_{false};
    std::thread thread_;
};
