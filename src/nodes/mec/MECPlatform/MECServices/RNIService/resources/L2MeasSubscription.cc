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

    if(jsonBody.contains("cells"))
    {
        std::vector<int> cellsJson = jsonBody["cells"];

        //in this case 0 means all cells
        if(cellsJson.size() == 1 && cellsJson[0] == 0)
        {
            // if the app asks for them all, add all the eNodeBs to the set
            for(auto it = eNodeBs_.begin(); it != eNodeBs_.end(); ++it)
            {
                cells_.insert(it->first);
                std::cout << "THIS MECHOST HAS THE FOLLOWING CELLS: " << it->first << std::endl;
            }
        }
        else
        {
            // if not, add only the eNodeBs specified in the cells list - if does not exist, send 400
            for(auto it = cellsJson.begin(); it != cellsJson.end(); ++it)
            {
                MacNodeId cellId = (MacNodeId) *it;
                if(eNodeBs_.find(cellId) == eNodeBs_.end())
                {
                    EV << "UsersListNotificationSubscription::fromJson - cellId " << cellId << " not found" << endl;
                    Http::send400Response(socket_, "cellId in cells not found");
                    return false;
                }
                else
                {
                    cells_.insert(cellId);
                    EV << "UsersListNotificationSubscription::fromJson - cellId " << cellId << " added" << endl;
                }
            }
        }
    }
    else
    {
        EV << "UsersListNotificationSubscription::fromJson - cells not found and it is mandatory" << endl;
        Http::send400Response(socket_, "cells JSON name is mandatory");
        return false;
    }


    callbackReference_ += "notifications/"+ std::to_string(subscriptionId_);
    resourceURL = baseResLocation_ + "layer2_meas/" + std::to_string(subscriptionId_);
    links_ = resourceURL;

    nlohmann::ordered_json response = body;
    response[subscriptionType_]["callbackReference"] = callbackReference_;
    response[subscriptionType_]["_links"]["self"] = links_;

    std::pair<std::string, std::string> p("Location: ", links_);
    Http::send201Response(socket_, response.dump(2).c_str(), p );
    return true;
}

void L2MeasSubscription::sendSubscriptionResponse()
{
        nlohmann::ordered_json val;
        val[subscriptionType_]["callbackReference"] = callbackReference_;
        val[subscriptionType_]["_links"]["self"] = links_;
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

