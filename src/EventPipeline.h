#include <OpenScanDeviceLib.h>
#include <libtcspc/tcspc.hpp>
#include <TimeTagger.h>

#include <memory>
#include <thread>
#include <vector>

class EventPipeline final : public IteratorBase {
public:
    EventPipeline(OScDev_Device *device, OScDev_Acquisition *acq, std::shared_ptr<tcspc::context> const &ctx);
    ~EventPipeline();

protected:
    bool next_impl(std::vector<Tag> &incoming_tags, timestamp_t begin_time, timestamp_t end_time) override;
    void on_start() override;
    void on_stop() override;

private:
    OScDev_Device *device_;
    tcspc::type_erased_processor<tcspc::type_list<tcspc::swabian_tag_event>> pipeline_;
    tcspc::buffer_accessor accessor_;
    std::thread consumer_thread_;

    void PumpConsumerLoop();
};
