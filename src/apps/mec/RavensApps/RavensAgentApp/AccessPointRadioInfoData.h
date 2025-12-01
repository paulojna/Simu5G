#ifndef _ACCESSPOINTRADIOINFODATA_H_
#define _ACCESSPOINTRADIOINFODATA_H_

#include <string>

namespace simu5g {

class AccessPointRadioInfoData {
    private:
        std::string accessPointId;
        double dl_total_prb_usage;
        double ul_total_prb_usage;

    public:
        // Default constructor
        AccessPointRadioInfoData();

        // Parameterized constructor
        AccessPointRadioInfoData(const std::string& accessPointId, double dlPrbUsage, double ulPrbUsage);

        // Destructor
        ~AccessPointRadioInfoData() {}

        // Getters
        std::string getAccessPointId() const;
        double getDlTotalPrbUsage() const;
        double getUlTotalPrbUsage() const;

        // Setters
        void setAccessPointId(const std::string& accessPointId);
        void setDlTotalPrbUsage(double dlPrbUsage);
        void setUlTotalPrbUsage(double ulPrbUsage);
};

}

#endif /* _ACCESSPOINTRADIOINFODATA_H_ */
