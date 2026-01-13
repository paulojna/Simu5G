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

/**
 * This file was modified by paulojna in order to work just as the example of a subscription model
 * in the Location Service.
 */

#include "common/utils/utils.h"
#include "nodes/mec/MECPlatform/MECServices/RNIService/resources/L2MeasSubscription.h"
#include <iostream>

namespace simu5g {

using namespace omnetpp;

L2MeasSubscription::L2MeasSubscription() :SubscriptionBase() {};
L2MeasSubscription::L2MeasSubscription(unsigned int subId, inet::TcpSocket *socket, const std::string& baseResLocation,
        std::set<cModule*, simu5g::utils::cModule_LessId>& eNodeBs)
        : SubscriptionBase(subId,socket,baseResLocation, eNodeBs)
{
    firstNotificationSent = false;
    checkImmediate_ = false;
    frequency_ = 1;
    lastNotification_ = 0;
    binder_ = getBinder();
    resourceURL = "";

    // Build stats collectors map (like L2Meas.cc lines 26-30)
    // This gives us DIRECT ACCESS to base station statistics
    auto it = eNodeBs.begin();
    for(; it != eNodeBs.end(); ++it)
    {
        BaseStationStatsCollector* collector =
            check_and_cast<BaseStationStatsCollector*>((*it)->getSubmodule("collector"));
        statsCollectors_.insert(
            std::pair<MacCellId, BaseStationStatsCollector*>(collector->getCellId(), collector)
        );
    }
};

L2MeasSubscription:: ~L2MeasSubscription(){};

bool L2MeasSubscription::fromJson(const nlohmann::ordered_json& body)
{
    if(body.contains("L2MeasurementSubscription")) // mandatory attribute
    {
        subscriptionType_ = "L2MeasurementSubscription";
    }
    else
    {
        Http::send400Response(socket_); // callbackReference is mandatory and takes exactly 1 att
        return false;
    }

    nlohmann::ordered_json jsonBody = body["L2MeasurementSubscription"];

    // callback reference to be able to send notifications to the correct application instance
    if(jsonBody.contains("callbackReference"))
    {
        nlohmann::ordered_json callbackReference = jsonBody["callbackReference"];
        if(callbackReference.contains("callbackData"))
            callbackData = callbackReference["callbackData"];
        if(callbackReference.contains("notifyURL"))
        {
            notifyURL = callbackReference["notifyURL"];
            std::size_t found = notifyURL.find("/");
            if (found!=std::string::npos)
            {
                clientHost_ = notifyURL.substr(0, found);
                clientUri_ = notifyURL.substr(found);
            }

        }
        else
        {
            EV << "UsersListNotificationSubscription::fromJson - notifyURL not found and it is mandatory" << endl;
            Http::send400Response(socket_); //notifyUrl is mandatory
            return false;
        }

    }
    else
    {
        EV << "UsersListyNotificationSubscription::fromJson - callbackReference not found and it is mandatory" << endl;
        Http::send400Response(socket_, "callbackReference JSON name is mandatory");
        return false;
    }

    // checkImmediate
    if(jsonBody.contains("checkImmediate"))
    {
        std::string check = jsonBody["checkImmediate"];
        checkImmediate_ = check.compare("true") == 0 ? true : false;
    }
    else
    {
        // if not specified, set to false
        EV << "UsersListNotificationSubscription::fromJson - checkImmediate not found and it is mandatory" << endl;
        checkImmediate_ = false;
    }

    if(jsonBody.contains("frequency"))
    {
        frequency_ = jsonBody["frequency"];
    }
    else
    {
        EV << "UsersListNotificationSubscription::fromJson - frequency not found and it is mandatory" << endl;
        Http::send400Response(socket_, "frequency JSON name is mandatory");
        return false;
    }

    // add basis information
    bool result = SubscriptionBase::fromJson(jsonBody);

    // add information relative to this type of subscription

    if(result)
    {
        callbackReference_ += "notifications/"+ std::to_string(subscriptionId_);

        if(!jsonBody.contains("filterCriteria") || jsonBody["filterCriteria"].is_array())
        {
            std::cout << "1"  << std::endl;
           Http::send400Response(socket_); // filterCriteria is mandatory and takes exactly 1 att
           return false;
        }


        nlohmann::json filterCriteria = jsonBody["filterCriteria"];

        //check for appInstanceId filter
        if(filterCriteria.contains("appInstanceId")  )
        {
            if(filterCriteria["appInstanceId"].is_array())
            {
                std::cout << "2"  << std::endl;

                Http::send400Response(socket_); // appInstanceId, if present, takes exactly 1 att
                return false;
            }
            filterCriteria_.appIstanceId = filterCriteria["appInstanceId"];
        }

        //check ues filter
        if(filterCriteria.contains("associateId"))
        {
            if(filterCriteria["associateId"].is_array())
            {
                std::cout << "3"  << std::endl;

                Http::send400Response(socket_); // only one ip
                return false;
//                nlohmann::json ueVector = filterCriteria["associateId"];
//                for(int i = 0; i < ueVector.size(); ++i)
//                {
//                    if(ueVector.at(i)["associateId"]["type"] == "UE_IPv4_ADDRESS")
//                    {
//                        std::string address = ueVector.at(i)["associateId"]["value"];
//                        ues.push_back(binder_->getMacNodeId(IPv4Address(address.c_str())));
//                    }
//                    else
//                    {
//                        Http::send400Response(socket_); // must be ipv4
//                        return false;
//                     }
//                 }
            }
            else
            {
                if(filterCriteria["associateId"]["type"] == "UE_IPv4_ADDRESS")
                {
                    filterCriteria_.associteId_.setType(filterCriteria["associateId"]["type"]);
                    filterCriteria_.associteId_.setValue(filterCriteria["associateId"]["value"]);

                }
            }
        }
        else
        {
            std::cout << "4"  << std::endl;

            Http::send400Response(socket_); // a user must be indicated
            return false;
        }

        //check cellIds filter
        if(filterCriteria.contains("ecgi"))
        {
            if(filterCriteria["ecgi"].is_array())
            {
//                nlohmann::json cellVector = filterCriteria["cellId"];
//                for(int i = 0; i < cellVector.size(); ++i)
//                {
//                    std::string cellId = cellVector.at(i)["cellId"];
//                    cellids.push_back((MacNodeId)std::stoi(cellId));
//                 }
            }
            else
            {
                if(filterCriteria["ecgi"].contains("cellId") && filterCriteria["ecgi"].contains("plmn"))
                {
                    std::string cellId = filterCriteria["ecgi"]["cellId"];
                    filterCriteria_.ecgi.setCellId((MacNodeId)std::stoi(cellId));
                    mec::Plmn plmn;
                    plmn.mcc = filterCriteria["ecgi"]["plmn"]["mcc"];
                    plmn.mnc = filterCriteria["ecgi"]["plmn"]["mnc"];
                    filterCriteria_.ecgi.setPlmn(plmn);
                }
                else
                {
                    std::cout << "5"  << std::endl;

                    Http::send400Response(socket_); // a user must be indicated
                    return false;
                }

            }
        }

        //check trigger filter
        if(filterCriteria.contains("trigger"))
        {
            //check if it is event trigger and notify, based on the state of the ues e cells
        }

        if(filterCriteria_.ecgi.getCellId() != 0)
        {
            cells_.insert(filterCriteria_.ecgi.getCellId());
        }

        resourceURL = baseResLocation_ + "layer2_meas/" + std::to_string(subscriptionId_);
        links_ = resourceURL;

        nlohmann::ordered_json response = body;
        response[subscriptionType_]["callbackReference"] = callbackReference_;
        response[subscriptionType_]["_links"]["self"] = links_;

        std::pair<std::string, std::string> p("Location: ", links_);
        Http::send201Response(socket_, response.dump(2).c_str(), p );
        return true;
    }

    return false;
}

void L2MeasSubscription::sendSubscriptionResponse()
{
        nlohmann::ordered_json val;
        val[subscriptionType_]["callbackReference"] = callbackReference_;
        val[subscriptionType_]["_links"]["self"] = links_;
        val[subscriptionType_]["filterCriteria"] = filterCriteria_.associteId_.toJson();
        val[subscriptionType_]["filterCriteria"] = filterCriteria_.ecgi.toJson();
}

nlohmann::ordered_json L2MeasSubscription::collectCellInfo()
{
    nlohmann::ordered_json cellArray = nlohmann::json::array();

    // Collect cell-level measurements for monitored cells
    for(auto cellId : cells_)
    {
        auto it = statsCollectors_.find(cellId);
        if(it != statsCollectors_.end())
        {
            BaseStationStatsCollector* collector = it->second;

            // Use RNICellInfo helper to format (like L2Meas.cc line 72-73)
            RNICellInfo cellInfo(collector);
            cellArray.push_back(cellInfo.toJson());
        }
    }

    return cellArray;
}

nlohmann::ordered_json L2MeasSubscription::collectUEInfo()
{
    nlohmann::ordered_json ueArray = nlohmann::json::array();

    // Collect ALL UE measurements in monitored cells
    for(auto cellId : cells_)
    {
        auto it = statsCollectors_.find(cellId);
        if(it != statsCollectors_.end())
        {
            BaseStationStatsCollector* collector = it->second;

            // Get all UEs in this cell (like L2Meas.cc lines 64-71)
            UeStatsCollectorMap* ueMap = collector->getCollectorMap();
            UeStatsCollectorMap::const_iterator uit = ueMap->begin();
            UeStatsCollectorMap::const_iterator end = ueMap->end();

            for(; uit != end; ++uit)
            {
                // Use CellUEInfo helper to format
                CellUEInfo cellUeInfo(uit->second, collector->getEcgi());
                ueArray.push_back(cellUeInfo.toJson());
            }
        }
    }

    return ueArray;
}

EventNotification* L2MeasSubscription::handleSubscription()
{
    EV << simTime() << " - L2MeasSubscription::handleSubscription - start" << endl;

    if(cells_.empty())
    {
        EV << "L2MeasSubscription::handleSubscription - no cells to monitor" << endl;
        return nullptr;
    }

    // Check if any monitored cell has data
    bool hasData = false;
    for(auto cellId : cells_)
    {
        auto it = statsCollectors_.find(cellId);
        if(it != statsCollectors_.end())
        {
            BaseStationStatsCollector* collector = it->second;
            UeStatsCollectorMap* ueMap = collector->getCollectorMap();

            // If cell has any UEs, we have data
            if(!ueMap->empty())
            {
                hasData = true;
                break;
            }
        }
    }

    if(!hasData)
    {
        EV << "L2MeasSubscription::handleSubscription - no UEs in monitored cells" << endl;
        return nullptr;
    }

    // Create event to trigger notification
    L2MeasNotificationEvent* event = new L2MeasNotificationEvent(
        subscriptionType_,
        subscriptionId_,
        cells_
    );

    return event;
}

void L2MeasSubscription::sendNotification(EventNotification *event)
{
    EV << "L2MeasSubscription::sendNotification - start" << endl;

    // Frequency throttling
    if(firstNotificationSent && (simTime() - lastNotification_) <= frequency_)
    {
        EV << "L2MeasSubscription - too soon, frequency=" << frequency_ << "s" << endl;
        return;
    }

    // Build notification structure
    nlohmann::ordered_json notification;
    nlohmann::ordered_json val;

    val["isFinalNotification"] = "false";
    val["link"]["href"] = resourceURL;
    val["link"]["rel"] = subscriptionType_;
    val["timeStamp"] = simTime().str();

    // Collect ALL cell-level measurements
    nlohmann::ordered_json cellArray = collectCellInfo();

    // Collect ALL UE-level measurements
    nlohmann::ordered_json ueArray = collectUEInfo();

    // Add data to notification (handle single vs array formatting)
    if(cellArray.size() > 1)
        val["cellInfo"] = cellArray;
    else if(cellArray.size() == 1)
        val["cellInfo"] = cellArray[0];

    if(ueArray.size() > 1)
        val["cellUEInfo"] = ueArray;
    else if(ueArray.size() == 1)
        val["cellUEInfo"] = ueArray[0];

    // Check we have data
    if(cellArray.empty() && ueArray.empty())
    {
        EV << "L2MeasSubscription - no data available" << endl;
        return;
    }

    notification["subscriptionNotification"] = val;

    // Send notification to subscriber
    EV << "L2MeasSubscription - sending notification" << endl;
    Http::send200Response(socket_, notification.dump(2).c_str());

    // Update state
    lastNotification_ = simTime();
    firstNotificationSent = true;
}

} //namespace

