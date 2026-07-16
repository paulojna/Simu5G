#include "UserRadioInfoData.h"

namespace simu5g {

UserRadioInfoData::UserRadioInfoData()
{
    accessPointId = "";
    timestamp = 0;
    dl_nongbr_delay_ue = -1.0;
    ul_nongbr_delay_ue = -1.0;
    dl_nongbr_pdr_ue = -1.0;
    ul_nongbr_pdr_ue = -1.0;
    dl_nongbr_data_volume_ue = -1.0;
    ul_nongbr_data_volume_ue = -1.0;
    rsrp = -1.0;
}

UserRadioInfoData::~UserRadioInfoData() {}

void UserRadioInfoData::setAccessPointId(const std::string& value) { accessPointId = value; }
void UserRadioInfoData::setTimestamp(omnetpp::simtime_t value) { timestamp = value; }
void UserRadioInfoData::setDlNongbrDelayUe(double value) { dl_nongbr_delay_ue = value; }
void UserRadioInfoData::setUlNongbrDelayUe(double value) { ul_nongbr_delay_ue = value; }
void UserRadioInfoData::setDlNongbrPdrUe(double value) { dl_nongbr_pdr_ue = value; }
void UserRadioInfoData::setUlNongbrPdrUe(double value) { ul_nongbr_pdr_ue = value; }
void UserRadioInfoData::setDlNongbrDataVolumeUe(double value) { dl_nongbr_data_volume_ue = value; }
void UserRadioInfoData::setUlNongbrDataVolumeUe(double value) { ul_nongbr_data_volume_ue = value; }
void UserRadioInfoData::setRsrp(double value) { rsrp = value; }

const std::string& UserRadioInfoData::getAccessPointId() const { return accessPointId; }
omnetpp::simtime_t UserRadioInfoData::getTimestamp() const { return timestamp; }
double UserRadioInfoData::getDlNongbrDelayUe() const { return dl_nongbr_delay_ue; }
double UserRadioInfoData::getUlNongbrDelayUe() const { return ul_nongbr_delay_ue; }
double UserRadioInfoData::getDlNongbrPdrUe() const { return dl_nongbr_pdr_ue; }
double UserRadioInfoData::getUlNongbrPdrUe() const { return ul_nongbr_pdr_ue; }
double UserRadioInfoData::getDlNongbrDataVolumeUe() const { return dl_nongbr_data_volume_ue; }
double UserRadioInfoData::getUlNongbrDataVolumeUe() const { return ul_nongbr_data_volume_ue; }
double UserRadioInfoData::getRsrp() const { return rsrp; }

}
