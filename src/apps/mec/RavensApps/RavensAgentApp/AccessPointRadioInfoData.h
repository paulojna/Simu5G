#ifndef _ACCESSPOINTRADIOINFODATA_H_
#define _ACCESSPOINTRADIOINFODATA_H_

#include <string>
#include "omnetpp.h"

namespace simu5g {

class AccessPointRadioInfoData {
    private:
        std::string accessPointId;
        omnetpp::simtime_t timestamp;

        // From RNIS cellInfo (authoritative cell-level aggregates)
        double dl_total_prb_usage_cell;
        double ul_total_prb_usage_cell;
        double dl_nongbr_pdr_cell;
        double ul_nongbr_pdr_cell;
        int    number_of_active_ue_dl_nongbr_cell;

        // Computed from RNIS cellUEInfo (per-user aggregation done at Agent).
        // These stay even though the per-UE delay and volume fields also travel
        // in every sample: the aggregate covers every UE the RNIS reports for
        // the cell, while a sample only exists for UEs the Location Service has
        // already reported, so the two populations are not the same. They carry
        // something the samples do not.
        double avg_dl_delay;
        double avg_ul_delay;
        double total_dl_data_volume;
        double total_ul_data_volume;

    public:
        // Default constructor
        AccessPointRadioInfoData();

        // Destructor
        ~AccessPointRadioInfoData() {}

        // Getters
        std::string getAccessPointId() const;
        omnetpp::simtime_t getTimestamp() const;
        double getDlTotalPrbUsageCell() const;
        double getUlTotalPrbUsageCell() const;
        double getDlNongbrPdrCell() const;
        double getUlNongbrPdrCell() const;
        int    getNumberOfActiveUeDlNongbrCell() const;
        double getAvgDlDelay() const;
        double getAvgUlDelay() const;
        double getTotalDlDataVolume() const;
        double getTotalUlDataVolume() const;

        // Setters
        void setAccessPointId(const std::string& accessPointId);
        void setTimestamp(omnetpp::simtime_t time);
        void setDlTotalPrbUsageCell(double dlPrbUsageCell);
        void setUlTotalPrbUsageCell(double ulPrbUsageCell);
        void setDlNongbrPdrCell(double dlNongbrPdrCell);
        void setUlNongbrPdrCell(double ulNongbrPdrCell);
        void setNumberOfActiveUeDlNongbrCell(int count);
        void setAvgDlDelay(double delay);
        void setAvgUlDelay(double delay);
        void setTotalDlDataVolume(double volume);
        void setTotalUlDataVolume(double volume);
};

}

#endif /* _ACCESSPOINTRADIOINFODATA_H_ */
