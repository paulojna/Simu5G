#include "AccessPointRadioInfoData.h"

namespace simu5g {

AccessPointRadioInfoData::AccessPointRadioInfoData() {
    this->accessPointId = "";
    this->dl_total_prb_usage = -1.0;
    this->ul_total_prb_usage = -1.0;
}

AccessPointRadioInfoData::AccessPointRadioInfoData(const std::string& accessPointId, double dlPrbUsage, double ulPrbUsage) {
    this->accessPointId = accessPointId;
    this->dl_total_prb_usage = dlPrbUsage;
    this->ul_total_prb_usage = ulPrbUsage;
}

// Getters
std::string AccessPointRadioInfoData::getAccessPointId() const {
    return this->accessPointId;
}

double AccessPointRadioInfoData::getDlTotalPrbUsage() const {
    return this->dl_total_prb_usage;
}

double AccessPointRadioInfoData::getUlTotalPrbUsage() const {
    return this->ul_total_prb_usage;
}

// Setters
void AccessPointRadioInfoData::setAccessPointId(const std::string& accessPointId) {
    this->accessPointId = accessPointId;
}

void AccessPointRadioInfoData::setDlTotalPrbUsage(double dlPrbUsage) {
    this->dl_total_prb_usage = dlPrbUsage;
}

void AccessPointRadioInfoData::setUlTotalPrbUsage(double ulPrbUsage) {
    this->ul_total_prb_usage = ulPrbUsage;
}

}
