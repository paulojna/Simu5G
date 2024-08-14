#ifndef RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_
#define RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_

#include "DataHandlerPolicyBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include <string>

// class RavensControllerApp;

namespace simu5g {

struct ueStanbyElement
{
    const std::string ue_reference;
    simtime_t time_limit;
    std::string meh; 

    ueStanbyElement(const std::string& ue_ref, const simtime_t& time_lim, const std::string& meh_val)
        : ue_reference(ue_ref), time_limit(time_lim), meh(meh_val) {}
};


class NotifyOnDataChange : public DataHandlerPolicyBase
{
    protected:
        simtime_t interval_;
        simtime_t start;
        int stanby_treshold_;
        int max_iterations;
        std::map<std::string, ueStanbyElement> standby;
        virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet) override;
    public:
        NotifyOnDataChange(RavensControllerApp* controllerApp, int treshold);
        void addUserUpdate(UserMEHUpdate &update);
        virtual ~NotifyOnDataChange(){}
};

}

#endif /* "RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_" */
