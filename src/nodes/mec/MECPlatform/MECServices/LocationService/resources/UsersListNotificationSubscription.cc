#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/UsersListNotificationSubscription.h"
#include "common/LteCommon.h"
#include "common/cellInfo/CellInfo.h"
#include "common/binder/Binder.h"
#include "inet/mobility/base/MovingMobilityBase.h"
#include "nodes/mec/MECPlatform/EventNotification/UsersListNotificationEvent.h"
#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/LocationApiDefs.h"

namespace simu5g {

using namespace omnetpp;

UsersListNotificationSubscription::UsersListNotificationSubscription()
{
    binder = getBinder();
    lastNotification = 0;
    firstNotificationSent = false;
}

UsersListNotificationSubscription::UsersListNotificationSubscription(unsigned int subId, inet::TcpSocket *socket , const std::string& baseResLocation,  std::set<omnetpp::cModule*, simu5g::utils::cModule_LessId>& eNodeBs):
SubscriptionBase(subId,socket,baseResLocation, eNodeBs){
    binder = getBinder();
    baseResLocation_+= "users/list";
    firstNotificationSent = false;
};

UsersListNotificationSubscription::~UsersListNotificationSubscription() {}

void UsersListNotificationSubscription::sendSubscriptionResponse(){}

bool UsersListNotificationSubscription::fromJson(const nlohmann::ordered_json& body) {
    // subscription name and body
    if(body.contains("usersListNotificationSubscription"))
    {
        subscriptionType_ = "usersListNotificationSubscription";
    }
    else
    {
        EV << "UsersListNotificationSubscription::fromJson - usersListNotificationSubscription JSON name not found and it is mandatory" << endl;
        Http::send400Response(socket_, "usersListNotificationSubscription JSON name is mandatory");
        return false;
    }

    nlohmann::ordered_json jsonBody = body["usersListNotificationSubscription"];

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
        checkImmediate = check.compare("true") == 0 ? true : false;
    }
    else
    {
        // if not specified, set to false
        EV << "UsersListNotificationSubscription::fromJson - checkImmediate not found and it is mandatory" << endl;
        checkImmediate = false;
    }

    // frequency to check the users density
    if(jsonBody.contains("frequency"))
    {
        frequency = jsonBody["frequency"];
    }
    else
    {
        EV << "UsersListNotificationSubscription::fromJson - frequency not found and it is mandatory" << endl;
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

    resourceURL = baseResLocation_+ "/" + std::to_string(subscriptionId_);
    return true;

}

EventNotification* UsersListNotificationSubscription::handleSubscription(){
    
    EV << "UsersListNotificationSubscription::handleSubscription - start" << endl;

    userList.clear();

    // run through the cells set and retrieve the users in each cell
    for(auto it = cells_.begin(); it != cells_.end(); ++it)
    {
        MacNodeId cellId = *it;
        EV << "UsersListNotificationSubscription::handleSubscription - cellId " << cellId << endl;

        // retrieve cellInfo from the eNodeBs map
        CellInfo* cellInfo = eNodeBs_.at(cellId);
        if(cellInfo == nullptr)
        {
            EV << "UsersListNotificationSubscription::handleSubscription - cellInfo not found" << endl;
            Http::send400Response(socket_, "cellInfo not found");
            return nullptr;
        }

        std::set<inet::Ipv4Address> addressess;
        const std::map<MacNodeId, inet::Coord>* uePositionList;
        uePositionList = cellInfo->getUePositionList();

        //deal with the case in which the uePositionList is empty. Fill userList with that information
        if(!uePositionList->empty())
        {
            // std::cout << "UsersListNotificationSubscription::handleSubscription - uePositionList is not empty at cellId " << cellId << " at sim time " << simTime() << std::endl;

            std::map<MacNodeId, inet::Coord>::const_iterator pit = uePositionList->begin();
            std::map<MacNodeId, inet::Coord>::const_iterator end = uePositionList->end();
            for(; pit != end ; ++pit)
            {
                inet::Ipv4Address ipAddress = binder->getIPv4Address(pit->first);
                std::string refUrl = resourceURL + "?address=acr:" + ipAddress.str();
                inet::Coord  speed = LocationUtils::getSpeed(pit->first);
                inet::Coord  position = LocationUtils::getCoordinates(pit->first);
                if(addressess.find(ipAddress) != addressess.end())
                    continue;
                addressess.insert(ipAddress);
                // populate userList with UserInfo objects
                UserInfo userInfo = UserInfo(position, speed , ipAddress, cellInfo->getMacCellId(), refUrl);
                // std::cout << "UserInfo with ipAddress " << ipAddress.str() << " at time " << simTime() << " added to userList" << std::endl;
                userList.push_back(userInfo);
            }    
        }
        else
        {
            continue;
            // EV << "UsersListNotificationSubscription::handleSubscription - uePositionList is empty" << endl;
            //return nullptr;
        }
    }

    if(userList.empty())
    {
        EV << "UsersListNotificationSubscription::handleSubscription - userList is empty" << endl;
        return nullptr;
    }

    // return the notification event
    UsersListNotificationEvent *notificationEvent = new UsersListNotificationEvent(subscriptionType_,subscriptionId_,userList);
    return notificationEvent;
}

void UsersListNotificationSubscription::sendNotification(EventNotification *event)
{
    EV << "UsersListNotificationSubscription::sendNotification - start" << endl;

    EV << firstNotificationSent << " last " << lastNotification << " now " << simTime() << " frequency " << frequency << endl;
    if(firstNotificationSent && (simTime() - lastNotification) <= frequency)
    {
        EV <<"UsersListNotificationSubscription::sendNotification - notification event occured near the last one. Frequency for notifications is: " << frequency << endl;
        return;
    }

    UsersListNotificationEvent *notificationEvent = check_and_cast<UsersListNotificationEvent*>(event);
    
    //get and add simulation time to val
    std::string simTimeStr = simTime().str();

    nlohmann::ordered_json val;
    nlohmann::ordered_json ueInfo;
    nlohmann::ordered_json ueInfoList;

    val["isFinalNotification"] = "false";
    val["link"]["href"] = resourceURL;
    val["link"]["rel"] = subscriptionType_;

    for(auto it : notificationEvent->getUsersList())
    {
        ueInfo["userInfo"] = it.toJson();
        ueInfoList.push_back(ueInfo);
    }

    if(ueInfoList.size() >= 1)
        val["userInfoList"] = ueInfoList;
    else
        val["userInfo"] = ueInfoList[0];
    val["timeStamp"] = simTimeStr;

    nlohmann::ordered_json notification;
    notification["subscriptionNotification"] = val;

    Http::send200Response(socket_, notification.dump(2).c_str());

    // update last notification sent
    lastNotification = simTime();
    if(firstNotificationSent == false)
        firstNotificationSent = true;
}

}
