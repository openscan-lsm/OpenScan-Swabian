#include "EventPipeline.h"
#include "Processing.h"
#include "TimeTaggerPrivate.h"

#include <bit>
#include <stdexcept>
#include <string>

EventPipeline::EventPipeline(OScDev_Device *device, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx) :
    IteratorBase(GetData(device)->tagger.get()),
    device_(device),
    pipeline_(MakeProcessingPipeline(GetData(device), acq, ctx)),
    accessor_(ctx->access<tcspc::buffer_accessor>(kTagBufferTrackerName))
{
    auto* data = GetData(device);
    for (auto const &channel : {data->syncChannel, data->photonChannel, data->lineClockChannel}) {
        registerChannel(channel);
        registerChannel(data->tagger->getInvertedChannel(channel));
    }
    consumer_thread_ = std::thread([this]() { PumpConsumerLoop(); });
    finishInitialization();
}

EventPipeline::~EventPipeline() {
    stop();
}

bool EventPipeline::next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time, timestamp_t end_time) {
    // TODO: One idea is that, to minimize the chance of tag buildup (and then losing data),
    // all this thread should do is to push the tags onto a libtcspc buffer (which we will have to add to the pipeline),
    // and then have another thread responsible for pumping tags out of the buffer. That would require a little more
    // coordination. Mark is pretty sure this will be needed.
    //
    // TODO: Create and handle TimeReachedEvents for the end_time timestamp (can't hurt to do begin_time as well)
    OScDev_Log_Info(device_, ("EventPipeline::next_impl: " + std::to_string(incoming_tags.size()) + " tags, begin_time= " + std::to_string(begin_time) + ", end_time= " + std::to_string(end_time)).c_str());
    try {
        for (auto const &tag : incoming_tags) {
            pipeline_.handle(std::bit_cast<tcspc::swabian_tag_event>(tag));
        }
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward. finish_running() is safe to call from here (unlike
        // stop()) since next_impl() already runs under IteratorBase's own
        // lock.
        OScDev_Log_Info(device_, ("EventPipeline: acquisition complete: " + std::string(e.what())).c_str());
        finish_running();
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but log it distinctly from a normal completion.
        OScDev_Log_Error(device_, ("EventPipeline: pipeline error: " + std::string(e.what())).c_str());
        finish_running();
    }
    OScDev_Log_Info(device_, "EventPipeline::next_impl: handled all those tags");
    return false;
}

void EventPipeline::on_start() {
}

void EventPipeline::on_stop() {
    // Preempt remaining tag batches with a halt.
    accessor_.halt();
    // Finish the current batch and then exit.
    consumer_thread_.join();
}

void EventPipeline::PumpConsumerLoop() {
    try {
        accessor_.pump();
    } catch (tcspc::end_of_processing const &e) {
        // Documented libtcspc protocol (see errors.hpp): a processor
        // signals a clean, non-error completion by flushing its own
        // downstream and throwing this; we are "the data source" that's
        // required to catch it, and must not send it any more events
        // afterward. finish_running() is safe to call from here (unlike
        // stop()) since next_impl() already runs under IteratorBase's own
        // lock.
        OScDev_Log_Info(device_, ("EventPipeline: acquisition complete: " + std::string(e.what())).c_str());
        finish_running();
    } catch (tcspc::source_halted const &) {
        OScDev_Log_Info(
            device_,
            "EventPipeline: consumer halted, remaining tags discarded"
        );
    } catch (std::exception const &e) {
        // Any other exception (e.g. stop_with_error's std::runtime_error)
        // is a genuine error in the data -- can't continue either way,
        // but log it distinctly from a normal completion.
        OScDev_Log_Error(device_, ("EventPipeline: pipeline error: " + std::string(e.what())).c_str());
        finish_running();
    }
}
