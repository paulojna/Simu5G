#include "AccessPointRadioInfoData.h"

namespace simu5g {

AccessPointRadioInfoData::AccessPointRadioInfoData() {
    // -1 = no measurement yet, consistent with the per-UE convention
    this->accessPointId = "";
    this->timestamp = 0;
    this->dl_total_prb_usage_cell = -1.0;
    this->ul_total_prb_usage_cell = -1.0;
    this->dl_nongbr_pdr_cell = -1.0;
    this->ul_nongbr_pdr_cell = -1.0;
    this->number_of_active_ue_dl_nongbr_cell = -1;
    this->avg_dl_delay = -1.0;
    this->avg_ul_delay = -1.0;
    this->total_dl_data_volume = -1.0;
    this->total_ul_data_volume = -1.0;
}

// Getters
std::string AccessPointRadioInfoData::getAccessPointId() const {
    return this->accessPointId;
}

omnetpp::simtime_t AccessPointRadioInfoData::getTimestamp() const {
    return this->timestamp;
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

int AccessPointRadioInfoData::getNumberOfActiveUeDlNongbrCell() const {
    return this->number_of_active_ue_dl_nongbr_cell;
}

double AccessPointRadioInfoData::getAvgDlDelay() const {
    return this->avg_dl_delay;
}

double AccessPointRadioInfoData::getAvgUlDelay() const {
    return this->avg_ul_delay;
}

double AccessPointRadioInfoData::getTotalDlDataVolume() const {
    return this->total_dl_data_volume;
}

double AccessPointRadioInfoData::getTotalUlDataVolume() const {
    return this->total_ul_data_volume;
}

// Setters
void AccessPointRadioInfoData::setAccessPointId(const std::string& accessPointId) {
    this->accessPointId = accessPointId;
}

void AccessPointRadioInfoData::setTimestamp(omnetpp::simtime_t time) {
    this->timestamp = time;
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

void AccessPointRadioInfoData::setNumberOfActiveUeDlNongbrCell(int count) {
    this->number_of_active_ue_dl_nongbr_cell = count;
}

void AccessPointRadioInfoData::setAvgDlDelay(double delay) {
    this->avg_dl_delay = delay;
}

void AccessPointRadioInfoData::setAvgUlDelay(double delay) {
    this->avg_ul_delay = delay;
}

void AccessPointRadioInfoData::setTotalDlDataVolume(double volume) {
    this->total_dl_data_volume = volume;
}

void AccessPointRadioInfoData::setTotalUlDataVolume(double volume) {
    this->total_ul_data_volume = volume;
}

}
