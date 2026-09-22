#pragma once

#include "ProcessingThread.h"
#include "TagStreamMeasurement.h"
#include "TagStreamProcessor.h"

#include <OpenScanDeviceLib.h>
#include <libtcspc/tcspc.hpp>

#include <memory>
#include <vector>

// One armed acquisition, alive from Arm() until Stop(), re-Arm(), or Close().
// Members are declared in dependency order so that their reverse-order
// destruction is the shutdown sequence: the measurement stops delivering,
// the processing thread halts and joins, then the graph is destroyed.
class AcquisitionRun {
  public:
    AcquisitionRun(OScDev_Device *device, OScDev_Acquisition *acq);

    [[nodiscard]] bool isRunning();
    void waitUntilFinished();

  private:
    bool Push(std::vector<Tag> const &tags, timestamp_t end_time);

    OScDev_Device *device_;
    std::shared_ptr<tcspc::context> ctx_;
    TagStreamProcessor processor_;
    ProcessingThread processingThread_;
    TagStreamMeasurement measurement_;
};
