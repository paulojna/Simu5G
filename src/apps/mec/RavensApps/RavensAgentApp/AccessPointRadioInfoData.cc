#include "AccessPointRadioInfoData.h"

namespace simu5g {

AccessPointRadioInfoData::AccessPointRadioInfoData() {
    this->accessPointId = "";
    this->dl_total_prb_usage_cell = -1.0;
    this->ul_total_prb_usage_cell = -1.0;
    this->dl_nongbr_pdr_cell = -1.0;
    this->ul_nongbr_pdr_cell = -1.0;
}

AccessPointRadioInfoData::AccessPointRadioInfoData(const std::string& accessPointId, double dlPrbUsageCell, double ulPrbUsageCell, double dlNongbrPdrCell, double ulNongbrPdrCell) {
    this->accessPointId = accessPointId;
    this->dl_total_prb_usage_cell = dlPrbUsageCell;
    this->ul_total_prb_usage_cell = ulPrbUsageCell;
    this->dl_nongbr_pdr_cell = dlNongbrPdrCell;
    this->ul_nongbr_pdr_cell = ulNongbrPdrCell;
}

// Getters
std::string AccessPointRadioInfoData::getAccessPointId() const {
    return this->accessPointId;
}

double AccessPointRadioInfoData::getDlTotalPrbUsageCell() const {
    return this->dl_total_prb_usage_cell;
}

double AccessPointRadioInfoData::getUlTotalPrbUsageCell() const {
    return this->ul_total_prb_usage_cell;
}

double AccessPointRadioInfoData::getDlNongbrPdrCell() const {
    return this->dl_nongbr_pdr_cell;
}

double AccessPointRadioInfoData::getUlNongbrPdrCell() const {
    return this->ul_nongbr_pdr_cell;
}

// Setters
void AccessPointRadioInfoData::setAccessPointId(const std::string& accessPointId) {
    this->accessPointId = accessPointId;
}

void AccessPointRadioInfoData::setDlTotalPrbUsageCell(double dlPrbUsageCell) {
    this->dl_total_prb_usage_cell = dlPrbUsageCell;
}

void AccessPointRadioInfoData::setUlTotalPrbUsageCell(double ulPrbUsageCell) {
    this->ul_total_prb_usage_cell = ulPrbUsageCell;
}

void AccessPointRadioInfoData::setDlNongbrPdrCell(double dlNongbrPdrCell) {
    this->dl_nongbr_pdr_cell = dlNongbrPdrCell;
}

void AccessPointRadioInfoData::setUlNongbrPdrCell(double ulNongbrPdrCell) {
    this->ul_nongbr_pdr_cell = ulNongbrPdrCell;
}

}
