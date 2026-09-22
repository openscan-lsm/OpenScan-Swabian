#include "TagStreamProcessor.h"

#include <exception>
#include <type_traits>
#include <utility>

// swabian_tag_event is a byte-array view with the same layout as Tag.
static_assert(sizeof(Tag) == sizeof(tcspc::swabian_tag_event));
static_assert(std::is_trivially_copyable_v<Tag>);

TagStreamProcessor::TagStreamProcessor(Pipeline pipeline)
    : pipeline_(std::move(pipeline)) {}

bool TagStreamProcessor::push(std::vector<Tag> const &tags) {
    if (state_ != State::Open)
        return false;
    if (tags.empty())
        return true;
    try {
        pipeline_.handle(TagSpan(
            reinterpret_cast<tcspc::swabian_tag_event const *>(tags.data()),
            tags.size()));
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward.
        state_ = State::Completed;
        message_ = e.what();
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but report it distinctly from a normal completion.
        state_ = State::Failed;
        message_ = e.what();
    }
    return state_ == State::Open;
}

TagStreamProcessor::State TagStreamProcessor::state() const noexcept {
    return state_;
}

std::string const &TagStreamProcessor::message() const noexcept {
    return message_;
}
