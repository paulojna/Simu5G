#ifndef _ACCESSPOINTRADIOINFODATA_H_
#define _ACCESSPOINTRADIOINFODATA_H_

#include <string>

namespace simu5g {

class AccessPointRadioInfoData {
    private:
        std::string accessPointId;
        double dl_total_prb_usage_cell;
        double ul_total_prb_usage_cell;
        double dl_nongbr_pdr_cell;
        double ul_nongbr_pdr_cell;

    public:
        // Default constructor
        AccessPointRadioInfoData();

        // Parameterized constructor
        AccessPointRadioInfoData(const std::string& accessPointId, double dlPrbUsageCell, double ulPrbUsageCell, double dlNongbrPdrCell, double ulNongbrPdrCell);

        // Destructor
        ~AccessPointRadioInfoData() {}

        // Getters
        std::string getAccessPointId() const;
        double getDlTotalPrbUsageCell() const;
        double getUlTotalPrbUsageCell() const;
        double getDlNongbrPdrCell() const;
        double getUlNongbrPdrCell() const;

        // Setters
        void setAccessPointId(const std::string& accessPointId);
        void setDlTotalPrbUsageCell(double dlPrbUsageCell);
        void setUlTotalPrbUsageCell(double ulPrbUsageCell);
        void setDlNongbrPdrCell(double dlNongbrPdrCell);
        void setUlNongbrPdrCell(double ulNongbrPdrCell);
};

}

#endif /* _ACCESSPOINTRADIOINFODATA_H_ */
