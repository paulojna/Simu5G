//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

#ifndef APPS_MEC_MESERVICES_RNISERVICE_RESOURCES_L2MEASSUBSCRIPTION_H_
#define APPS_MEC_MESERVICES_RNISERVICE_RESOURCES_L2MEASSUBSCRIPTION_H_

#include "common/utils/utils.h"
#include "nodes/mec/MECPlatform/MECServices/Resources/SubscriptionBase.h"
#include "nodes/mec/MECPlatform/MECServices/RNIService/resources/Ecgi.h"
#include "nodes/mec/MECPlatform/MECServices/RNIService/resources/AssociateId.h"
#include "nodes/mec/MECPlatform/MECServices/RNIService/resources/RNICellInfo.h"
#include "nodes/mec/MECPlatform/MECServices/RNIService/resources/CellUEInfo.h"
#include "nodes/mec/MECPlatform/EventNotification/L2MeasNotificationEvent.h"
#include "nodes/mec/utils/MecCommon.h"
#include "corenetwork/statsCollector/BaseStationStatsCollector.h"
#include "corenetwork/statsCollector/UeStatsCollector.h"
#include "common/binder/Binder.h"

namespace simu5g {

using namespace omnetpp;

class L2MeasSubscription : public SubscriptionBase
{

    struct FilterCriteriaL2Meas {
        std::string appIstanceId;
        AssociateId associteId_;
        Ecgi ecgi;
    };

    public:
        L2MeasSubscription();
        L2MeasSubscription(unsigned int subId, inet::TcpSocket *socket, const std::string& baseResLocation, std::set<cModule*, simu5g::utils::cModule_LessId>& eNodeBs);
        virtual ~L2MeasSubscription();

        virtual bool fromJson(const nlohmann::ordered_json& json) override;
        virtual void sendSubscriptionResponse() override;
        virtual void sendNotification(EventNotification *event) override;
        virtual EventNotification* handleSubscription() override;

        bool getFirstNotification() const {return firstNotificationSent;}
        omnetpp::simtime_t getLastoNotification() const { return lastNotification_;}

        std::string getResourceUrl() const { return resourceURL;}
        virtual bool getCheckImmediate() const { return checkImmediate_;}

    protected:
        FilterCriteriaL2Meas filterCriteria_;

        // Subscription metadata
        std::string resourceURL;
        std::string callbackData;
        std::string notifyURL;
        bool checkImmediate_;
        int frequency_;
        std::set<MacCellId> cells_;
        simtime_t lastNotification_;
        bool firstNotificationSent;
        Binder* binder_;

        // Direct access to stats collectors
        std::map<MacCellId, BaseStationStatsCollector*> statsCollectors_;

        // Helper methods for data collection
        nlohmann::ordered_json collectCellInfo();
        nlohmann::ordered_json collectUEInfo();
};

} //namespace

#endif /* APPS_MEC_MESERVICES_RNISERVICE_RESOURCES_L2MEASSUBSCRIPTION_H_ */
