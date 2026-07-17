#include "MeoOutput.h"

namespace simu5g {

MeoOutput::MeoOutput(RavensControllerApp *controllerApp)
    : RavensOutputBase(controllerApp)
{
}

void MeoOutput::onUserEntry(const std::string& userId, const std::string& meh,
                            int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "MeoOutput::onUserEntry - " << userId << " at " << meh << endl;
    emitUserUpdate(userId, "", meh);
}

void MeoOutput::onUserHandover(const std::string& userId,
                               const std::string& fromMeh, const std::string& toMeh,
                               int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "MeoOutput::onUserHandover - " << userId << " " << fromMeh << " -> " << toMeh << endl;
    emitUserUpdate(userId, fromMeh, toMeh);
}

void MeoOutput::onUserExit(const std::string& userId, const std::string& fromMeh,
                           int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "MeoOutput::onUserExit - " << userId << " left " << fromMeh << endl;
    emitUserUpdate(userId, fromMeh, "");
}

}
