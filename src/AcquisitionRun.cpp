#include "AcquisitionRun.h"
#include "Processing.h"
#include "TimeTaggerPrivate.h"

#include <string>

namespace {

std::vector<channel_t> ChannelsToRegister(TimeTagger_PrivateData *data) {
    std::vector<channel_t> channels;
    for (auto const channel :
         {data->syncChannel, data->photonChannel, data->lineClockChannel}) {
        channels.push_back(channel);
        channels.push_back(data->tagger->getInvertedChannel(channel));
    }
    return channels;
}

} // namespace

AcquisitionRun::AcquisitionRun(OScDev_Device *device, OScDev_Acquisition *acq)
    : device_(device), ctx_(tcspc::context::create()),
      processor_(MakeProcessingPipeline(GetData(device), acq, ctx_)),
      processingThread_(
          ctx_->access<tcspc::buffer_accessor>(kTagBufferTrackerName),
          [this](ProcessingThread::Outcome outcome,
                 std::string const &message) {
              switch (outcome) {
              case ProcessingThread::Outcome::Completed:
                  OScDev_Log_Info(
                      device_,
                      ("AcquisitionRun: acquisition complete: " + message)
                          .c_str());
                  break;
              case ProcessingThread::Outcome::Halted:
                  OScDev_Log_Info(device_,
                                  ("AcquisitionRun: " + message).c_str());
                  break;
              case ProcessingThread::Outcome::Failed:
                  OScDev_Log_Error(
                      device_,
                      ("AcquisitionRun: pipeline error: " + message).c_str());
                  break;
              }
          }),
      measurement_(
          GetData(device)->tagger.get(), ChannelsToRegister(GetData(device)),
          [this](std::vector<Tag> const &tags) { return Push(tags); }) {}

bool AcquisitionRun::isRunning() { return measurement_.isRunning(); }

void AcquisitionRun::waitUntilFinished() { measurement_.waitUntilFinished(); }

bool AcquisitionRun::Push(std::vector<Tag> const &tags) {
    // The consumer having ended (it logged why) is the usual way an
    // acquisition completes; checking here lets isRunning() flip promptly
    // instead of waiting for the batch processor to fill another bucket
    // and bounce end_of_processing back from the buffer.
    if (processingThread_.finished())
        return false;
    if (processor_.push(tags))
        return true;
    if (processor_.state() == TagStreamProcessor::State::Failed)
        OScDev_Log_Error(device_, ("AcquisitionRun: pipeline error: " +
                                   processor_.message())
                                      .c_str());
    else
        OScDev_Log_Info(device_, ("AcquisitionRun: acquisition complete: " +
                                  processor_.message())
                                     .c_str());
    // libtcspc requires halting the buffer when the upstream ends without
    // flushing it; harmless if the consumer has already exited.
    processingThread_.halt();
    return false;
}
