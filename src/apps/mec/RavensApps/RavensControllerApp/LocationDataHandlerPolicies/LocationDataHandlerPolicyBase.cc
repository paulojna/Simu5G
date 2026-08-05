#include "LocationDataHandlerPolicyBase.h"
#include "../DataUpdates/UserMEHUpdate.h"

namespace simu5g {

UserSample makeUserSample(inet::Ptr<const RavensLinkDataFrameMessage> frame,
                          const std::string& userId,
                          const UserData& observation,
                          const std::string& confirmedMEH)
{
    // Copied once because UserData hands out its location by value.
    UserLocation location = observation.getCurrentLocation();
    const UserRadioInfoData& radio = observation.getRadioInfo();

    UserSample sample;
    sample.frameSentAt           = frame->getTimeStamp();
    sample.locationTimestamp     = observation.getTimestamp();
    sample.radioTimestamp        = radio.getTimestamp();
    sample.userId                = userId;
    sample.observedMEH           = frame->getMecHostId();
    sample.confirmedMEH          = confirmedMEH;
    sample.accessPointId         = observation.getAccessPointId();
    sample.rnisCellId            = radio.getAccessPointId();
    sample.x                     = location.getX();
    sample.y                     = location.getY();
    sample.z                     = location.getZ();
    sample.speed                 = location.getHorizontalSpeed();
    sample.bearing               = location.getBearing();
    sample.distanceToAccessPoint = observation.getDistanceToAP();
    sample.dlNongbrDelayUe       = radio.getDlNongbrDelayUe();
    sample.ulNongbrDelayUe       = radio.getUlNongbrDelayUe();
    sample.dlNongbrPdrUe         = radio.getDlNongbrPdrUe();
    sample.ulNongbrPdrUe         = radio.getUlNongbrPdrUe();
    sample.dlNongbrDataVolumeUe  = radio.getDlNongbrDataVolumeUe();
    sample.ulNongbrDataVolumeUe  = radio.getUlNongbrDataVolumeUe();
    sample.rsrp                  = radio.getRsrp();
    return sample;
}

std::string userSampleCsvHeader()
{
    // Walks an empty record purely for its field names — the values are
    // irrelevant here, the order is the point. Same list as the rows.
    std::string header;
    UserSample().forEachField([&header](const char* name, const auto&) {
        if (!header.empty())
            header += ',';
        header += name;
    });
    return header;
}

void LocationDataHandlerPolicyBase::emitUserUpdate(const std::string& address,
                                                   const std::string& lastMeh,
                                                   const std::string& newMeh)
{
    UserMEHUpdate update;
    update.setAddress(address);
    update.setLastMEHId(lastMeh);
    update.setNewMEHId(newMeh);
    controllerApp_->userUpdates.insert_or_assign(address, update);
}

}
