#include "ProcessingThread.h"

#include <exception>
#include <utility>

ProcessingThread::ProcessingThread(tcspc::buffer_accessor accessor,
                                   FinishedCallback onFinished)
    : accessor_(std::move(accessor)), onFinished_(std::move(onFinished)),
      thread_([this] { Run(); }) {}

ProcessingThread::~ProcessingThread() {
    halt();
    if (thread_.joinable())
        thread_.join();
}

void ProcessingThread::halt() noexcept { accessor_.halt(); }

bool ProcessingThread::finished() const noexcept { return finished_; }

void ProcessingThread::Run() {
    Outcome outcome;
    std::string message;
    try {
        accessor_.pump();
        outcome = Outcome::Completed;
        message = "processing flushed";
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward.
        outcome = Outcome::Completed;
        message = e.what();
    } catch (tcspc::source_halted const &) {
        outcome = Outcome::Halted;
        message = "consumer halted, remaining tags discarded";
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but report it distinctly from a normal completion.
        outcome = Outcome::Failed;
        message = e.what();
    }
    finished_ = true;
    onFinished_(outcome, message);
}
