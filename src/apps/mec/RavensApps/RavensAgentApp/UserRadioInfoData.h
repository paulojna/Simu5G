#ifndef _USERRADIOINFODATA_H_
#define _USERRADIOINFODATA_H_

#include <string>
#include "omnetpp.h"

namespace simu5g {

// Per-UE L2 measurements from the RNIS. Only non-GBR delay/PDR/data-volume
// metrics are carried. GBR metrics are excluded (Simu5G's UeStatsCollector
// does not implement them; its GBR getters are hardcoded to -1). Throughput
// is excluded too: Simu5G implements it as 3GPP TS 36.314 Scheduled IP
// Throughput, which discards single-TTI bursts and therefore never yields a
// value for the small periodic packets used in our scenarios.
class UserRadioInfoData
{
    protected:
        std::string accessPointId;
        omnetpp::simtime_t timestamp;

        double dl_nongbr_delay_ue;
        double ul_nongbr_delay_ue;
        double dl_nongbr_pdr_ue;
        double ul_nongbr_pdr_ue;
        double dl_nongbr_data_volume_ue;
        double ul_nongbr_data_volume_ue;
        double rsrp; // serving-cell mean RSRP (dBm) reported by the UE PHY

    public:
        UserRadioInfoData();
        virtual ~UserRadioInfoData();

        void setAccessPointId(const std::string& accessPointId);
        void setTimestamp(omnetpp::simtime_t timestamp);
        void setDlNongbrDelayUe(double value);
        void setUlNongbrDelayUe(double value);
        void setDlNongbrPdrUe(double value);
        void setUlNongbrPdrUe(double value);
        void setDlNongbrDataVolumeUe(double value);
        void setUlNongbrDataVolumeUe(double value);
        void setRsrp(double value);

        const std::string& getAccessPointId() const;
        omnetpp::simtime_t getTimestamp() const;
        double getDlNongbrDelayUe() const;
        double getUlNongbrDelayUe() const;
        double getDlNongbrPdrUe() const;
        double getUlNongbrPdrUe() const;
        double getDlNongbrDataVolumeUe() const;
        double getUlNongbrDataVolumeUe() const;
        double getRsrp() const;
};

}

#endif /* _USERRADIOINFODATA_H_ */
