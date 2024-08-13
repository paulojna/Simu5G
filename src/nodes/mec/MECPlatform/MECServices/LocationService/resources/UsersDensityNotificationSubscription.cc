#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/UsersDensityNotificationSubscription.h"
#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/CurrentLocation.h"
#include "common/LteCommon.h"
#include "common/cellInfo/CellInfo.h"
#include "common/binder/Binder.h"
#include "inet/mobility/base/MovingMobilityBase.h"
#include "nodes/mec/MECPlatform/EventNotification/UsersDensityNotificationEvent.h"

namespace simu5g {

using namespace omnetpp;

UsersDensityNotificationSubscription::UsersDensityNotificationSubscription()
{
    binder = getBinder();
    lastNotification = 0;
    firstNotificationSent = false;
}

UsersDensityNotificationSubscription::UsersDensityNotificationSubscription(unsigned int subId, inet::TcpSocket *socket, const std::string& baseResLocation,  std::set<omnetpp::cModule*, simu5g::utils::cModule_LessId>& eNodeBs):
SubscriptionBase(subId,socket,baseResLocation, eNodeBs){
    binder = getBinder();
    baseResLocation_+= "users/density";
    firstNotificationSent = false;
};

UsersDensityNotificationSubscription::~UsersDensityNotificationSubscription() {}

void UsersDensityNotificationSubscription::sendSubscriptionResponse(){}

void UsersDensityNotificationSubscription::sendNotification(EventNotification *event)
{
    EV << "UsersDensityNotificationSubscription::sendNotification" << endl;

    EV << firstNotificationSent << " last " << lastNotification << " now " << simTime() << " frequency" << frequency << endl;
    if(firstNotificationSent && (simTime() - lastNotification) <= frequency)
    {
        EV <<"UsersDensityNotificationSubscription::sendNotification - notification event occured near the last one. Frequency for notifications is: " << frequency << endl;
        return;
    }

    UsersDensityNotificationEvent *userDensityEvent = check_and_cast<UsersDensityNotificationEvent*>(event);

    nlohmann::ordered_json val;
    nlohmann::ordered_json densityBS;
    nlohmann::ordered_json densityList;

    val["isFinalNotification"] = "false";
    val["link"]["href"] = resourceURL;
    val["link"]["rel"] = subscriptionType_;

    //get and add simulation time to val
    std::string simTimeStr = simTime().str();

    for(auto it : userDensityEvent->getDensity())
    {
        densityBS["cellId"] = it.first;
        densityBS["density"] = it.second;

        /*
            Workaround to send sim time as a string. 
            TODO: Future implementation should change density_ into a map<MacCellId, DensityInfo>, 
            which will need to define Class DensityInfo. This class will contain the MacCellId, 
            density and simTime as a string.
        */

        densityList.push_back(densityBS);
    }

    if(densityList.size() > 1)
        val["densityList"] = densityList;
    else
        val["densityList"] = densityList[0];

    val["simTime"] = simTimeStr;

    nlohmann::ordered_json notification;
    notification["subscriptionNotification"] = val;

    Http::send200Response(socket_, notification.dump(2).c_str());

    // update last notification sent
    lastNotification = simTime();
    if(firstNotificationSent == false)
        firstNotificationSent = true;
}

bool UsersDensityNotificationSubscription::fromJson(const nlohmann::ordered_json& body) {
    
    // subscription name and body
    if(body.contains("usersDensityNotificationSubscription")) 
    {

        subscriptionType_ = "usersDensityNotificationSubscription";
    }
    else
    {
        EV << "UsersDensityNotificationSubscription::fromJson - usersDensityNotificationSubscription JSON name not found and it is mandatory" << endl;
        Http::send400Response(socket_, "usersDensityNotificationSubscription JSON name is mandatory");
        return false;
    }

    nlohmann::ordered_json jsonBody = body["usersDensityNotificationSubscription"];


    // callbackReference to be able to send notification to the correct application instance
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
            EV << "UsersDensityNotificationSubscription::fromJson - notifyURL not found and it is mandatory" << endl;
            Http::send400Response(socket_); //notifyUrl is mandatory
            return false;
        }

    }
    else
    {
        EV << "UsersDensityNotificationSubscription::fromJson - callbackReference not found and it is mandatory" << endl;
        Http::send400Response(socket_, "callbackReference JSON name is mandatory");
        return false;
    }

    // checkImmediate
    if(jsonBody.contains("checkImmediate"))
    {
        std::string check = jsonBody["checkImmediate"];
        checkImmediate = check.compare("true") == 0 ? true : false;
    }
    else
    {
        // if not specified, set to false
        EV << "UsersDensityNotificationSubscription::fromJson - checkImmediate not found and it is mandatory" << endl;
        checkImmediate = false;
    }

    // frequency to check the users density
    if(jsonBody.contains("frequency"))
    {
        frequency = jsonBody["frequency"];
    }
    else
    {
        EV << "UsersDensityNotificationSubscription::fromJson - frequency not found and it is mandatory" << endl;
        Http::send400Response(socket_, "frequency JSON name is mandatory");
        return false;
    }

    // fulfill the cells set with the cells that have their id in the received POST cells list
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
                    EV << "UsersDensityNotificationSubscription::fromJson - cellId " << cellId << " not found" << endl;
                    Http::send400Response(socket_, "cellId in cells not found");
                    return false;
                }
                else
                {
                    cells_.insert(cellId);
                    EV << "UsersDensityNotificationSubscription::fromJson - cellId " << cellId << " added" << endl;
                }
            }
        }
    }
    else
    {
        EV << "UsersDensityNotificationSubscription::fromJson - cells not found and it is mandatory" << endl;
        Http::send400Response(socket_, "cells JSON name is mandatory");
        return false;
    }

    resourceURL = baseResLocation_+ "/" + std::to_string(subscriptionId_);
    return true;
}

EventNotification* UsersDensityNotificationSubscription::handleSubscription() {

    EV << "UsersDensityNotificationSubscription::handleSubscription()" << endl;

    std::map<MacCellId, int> density_updated;

    for (const auto& cellId : cells_) {
        CellInfo* cellInfo = eNodeBs_.at(cellId);
    
        // get a reference to uePositionList and calculate the number of users
        const std::map<MacNodeId, inet::Coord>& uePositionList = *(cellInfo->getUePositionList());
        int numUsers = uePositionList.size();

        density_updated.emplace(cellId, numUsers);
    }

    if(!density_updated.empty() && density_updated != density)
    {
        density = density_updated;
        UsersDensityNotificationEvent *notificationEvent = new UsersDensityNotificationEvent(subscriptionType_,subscriptionId_,density);
        return notificationEvent;
    }
    else
    {
        return nullptr;
    }


}

}
