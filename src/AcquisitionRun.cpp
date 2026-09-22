#include "AcquisitionRun.h"
#include "Processing.h"
#include "TimeTaggerPrivate.h"
#include "UniqueFileName.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace {

ProcessingParams MakeProcessingParams(TimeTagger_PrivateData *data,
                                      OScDev_Acquisition *acq) {
    uint32_t x, y, width, height;
    OScDev_Acquisition_GetROI(acq, &x, &y, &width, &height);
    double const pixelRate = OScDev_Acquisition_GetPixelRate(acq);

    ProcessingParams params{
        .width = width,
        .height = height,
        .pixelTime_ps = std::llround(1e12 / pixelRate),
        .numFrames = OScDev_Acquisition_GetNumberOfFrames(acq),
        .lineClockChannel = data->lineClockChannel,
        .syncChannel = data->syncChannel,
        .photonLeadingChannel = data->photonChannel,
        .photonTrailingChannel =
            data->tagger->getInvertedChannel(data->photonChannel),
        .syncDelay_ps = data->syncDelay_ps,
        .lineDelay_ps = data->lineDelay_ps,
        .maxPhotonPulseWidth_ps = data->maxPhotonPulseWidth_ps,
        .maxDiffTime_ps = data->maxDiffTime_ps,
        .cumulative = data->cumulative,
        .histogramBins = data->histogramBins,
        .histogramBinWidth_ps = data->histogramBinWidth_ps,
        .histogramDumpFileName = std::nullopt,
        .rawDataFileName = std::nullopt,
    };

    // File names follow OpenScan-BH_SPC's scheme (File Name Prefix setting
    // + "_NNNN" index shared by all output files); see UniqueFileName.h.
    if (data->saveHistograms || data->saveRawData) {
        std::optional<std::string> const uniqueName =
            UniqueFileName(data->fileNamePrefix, {".raw", ".hist"});
        if (!uniqueName)
            throw std::runtime_error(
                "Could not find a unique file name for output files "
                "(prefix '" +
                data->fileNamePrefix + "')");
        if (data->saveHistograms)
            params.histogramDumpFileName = *uniqueName + ".hist";
        if (data->saveRawData)
            params.rawDataFileName = *uniqueName + ".raw";
    }

    return params;
}

FrameCallback MakeFrameCallback(OScDev_Acquisition *acq) {
    return [acq](std::uint32_t channel, std::span<tcspc::u16 const> pixels) {
        OScDev_Acquisition_CallFrameCallback(
            acq, channel,
            const_cast<void *>(static_cast<void const *>(pixels.data())));
    };
}

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
      processor_(
          MakeProcessingPipeline(MakeProcessingParams(GetData(device), acq),
                                 MakeFrameCallback(acq), ctx_)),
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
      measurement_(GetData(device)->tagger.get(),
                   ChannelsToRegister(GetData(device)),
                   [this](std::vector<Tag> const &tags, timestamp_t end_time) {
                       return Push(tags, end_time);
                   }) {}

bool AcquisitionRun::isRunning() { return measurement_.isRunning(); }

void AcquisitionRun::waitUntilFinished() { measurement_.waitUntilFinished(); }

bool AcquisitionRun::Push(std::vector<Tag> const &tags, timestamp_t end_time) {
    // The consumer having ended (it logged why) is the usual way an
    // acquisition completes; checking here lets isRunning() flip promptly
    // instead of waiting for the next push to bounce end_of_processing
    // back from the buffer.
    if (processingThread_.finished())
        return false;
    if (processor_.push(tags, end_time))
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
