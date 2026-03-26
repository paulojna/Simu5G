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

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"
#include "inet/transportlayer/contract/tcp/TcpCommand_m.h"
#include "inet/applications/tcpapp/GenericAppMsg_m.h"
#include <iostream>
#include "nodes/mec/MECPlatform/MECServices/RNIService/RNIService.h"

#include <string>
#include <vector>
//#include "apps/mec/MECServices/packets/HttpResponsePacket.h"
#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "common/utils/utils.h"
#include "inet/networklayer/contract/ipv4/Ipv4Address.h"

#include "nodes/mec/MECPlatform/MECServices/Resources/SubscriptionBase.h"
#include "resources/L2MeasSubscription.h"

namespace simu5g {

Define_Module(RNIService);


RNIService::RNIService():L2MeasResource_(){
    baseUriQueries_ = "/example/rni/v2/queries";
    baseUriSubscriptions_ = "/example/rni/v2/subscriptions";

    subscriptionId_ = 0;
    subscriptions_.clear();
    supportedQueryParams_.insert("cell_id");
    supportedQueryParams_.insert("ue_ipv4_address");
    // supportedQueryParams_s_.insert("ue_ipv6_address");
}

void RNIService::initialize(int stage)
{
    MecServiceBase::initialize(stage);

    if (stage == inet::INITSTAGE_APPLICATION_LAYER) {
        L2MeasResource_.addEnodeB(eNodeB_);
        baseSubscriptionLocation_ = host_+ baseUriSubscriptions_ + "/";

        subscriptionTimer_ = new AperiodicSubscriptionTimer("subscriptionTimer", 1);
    }
}

bool RNIService::manageSubscription()
{
    int subId = currentSubscriptionServed_->getSubId();
    if(subscriptions_.find(subId) != subscriptions_.end())
    {
        EV << "RNIService::manageSubscription() - subscription with id: " << subId << " found" << endl;
        SubscriptionBase * sub = subscriptions_[subId]; //upcasting (getSubscriptionType is in Subscriptionbase)
        sub->sendNotification(currentSubscriptionServed_);
        if(currentSubscriptionServed_!= nullptr)
            delete currentSubscriptionServed_;
        currentSubscriptionServed_ = nullptr;
        return true;
    }
    else{
        EV << "RNIService::manageSubscription() - subscription with id: " << subId << " not found. Removing from subscriptionTimer.." << endl;
        // the subscription has been deleted, e.g. due to closing socket
        // remove subId from AperiodicSubscription timer
        subscriptionTimer_->removeSubId(subId);
        if(subscriptionTimer_->getSubIdSetSize() == 0)
            cancelEvent(subscriptionTimer_);
        if(currentSubscriptionServed_!= nullptr)
            delete currentSubscriptionServed_;
        currentSubscriptionServed_ = nullptr;
        return false;
    }
}

void RNIService::handleMessage(cMessage *msg)
{
    if(msg->isSelfMessage())
    {
        if(msg->isName("subscriptionTimer"))
        {
            EV << "subscriptionTimer" << endl;
            AperiodicSubscriptionTimer *subTimer = check_and_cast<AperiodicSubscriptionTimer*>(msg);
            std::set<int> subIds = subTimer->getSubIdSet(); // TODO pass it as reference
            for(auto sub : subIds)
            {
                if(subscriptions_.find(sub) != subscriptions_.end())
                {
                    EV << "subscriptionTimer for subscription: " << sub << endl;
                    SubscriptionBase * subscription = subscriptions_[sub]; //upcasting (getSubscriptionType is in Subscriptionbase)
                    EventNotification *event = subscription->handleSubscription();
                    if(event != nullptr)
                        newSubscriptionEvent(event);
                }
                else
                {
                    EV << "remove subId " << sub << " from aperiodic trimer" << endl;
                    subTimer->removeSubId(sub);
                }
            }
            if(subTimer->getSubIdSetSize() > 0)
                scheduleAt(simTime()+subTimer->getPeriod(), msg);
            return;
        }

    }
    MecServiceBase::handleMessage(msg);
}

void RNIService::socketClosed(inet::TcpSocket* socket)
{
    std::cout << "RNIService::socketClosed" << std::endl;
    MecServiceBase::socketClosed(socket);
    // remove subscription from the timer
    if(currentSubscriptionServed_ != nullptr)
    {
        int subId = currentSubscriptionServed_->getSubId();
        if(subscriptions_.find(subId) != subscriptions_.end())
        {
            subscriptions_.erase(subId);
            subscriptionTimer_->removeSubId(subId);
            if(subscriptionTimer_->getSubIdSetSize() == 0 && subscriptionTimer_->isScheduled())
                cancelEvent(subscriptionTimer_);
        }
        delete currentSubscriptionServed_;
        currentSubscriptionServed_ = nullptr;
    }
}

void RNIService::handleGETRequest(const HttpRequestMessage *currentRequestMessageServed, inet::TcpSocket* socket)
{
    std::string uri = currentRequestMessageServed->getUri();
    // std::vector<std::string> splittedUri = simu5g::utils::splitString(uri, "?");
    // // uri must be in form example/v1/rni/queries/resource
    // std::size_t lastPart = splittedUri[0].find_last_of("/");
    // if(lastPart == std::string::npos)
    // {
    //     Http::send404Response(socket); //it is not a correct uri
    //     return;
    // }
    // // find_last_of does not take in to account if the uri has a last /
    // // in this case resourceType would be empty and the baseUri == uri
    // // by the way the next if statement solve this problem
    // std::string baseUri = splittedUri[0].substr(0,lastPart);
    // std::string resourceType =  splittedUri[0].substr(lastPart+1);

    // check it is a GET for a query or a subscription
    if(uri.compare(baseUriQueries_ + "/layer2_meas") == 0 ) //queries
    {
        L2MeasResource_.updateTimestamp();
        std::string params = currentRequestMessageServed->getParameters();
        //look for query parameters
        if(!params.empty())
        {
            std::vector<std::string> queryParameters = simu5g::utils::splitString(params, "&");
            /*
            * supported paramater:
            * - cell_id
            * - ue_ipv4_address
            * - ue_ipv6_address // not implemented yet
            */

            std::vector<MacNodeId> cellIds;
            std::vector<inet::Ipv4Address> ues;

            typedef std::map<std::string, std::vector<std::string>> queryMap;
            queryMap queryParamsMap; // e.g cell_id -> [0, 1]

            std::vector<std::string>::iterator it  = queryParameters.begin();
            std::vector<std::string>::iterator end = queryParameters.end();
            std::vector<std::string> params;
            std::vector<std::string> splittedParams;
            for(; it != end; ++it){
                if(it->rfind("cell_id", 0) == 0) // cell_id=par1,par2
                {
                    params = simu5g::utils::splitString(*it, "=");
                    if(params.size()!= 2) //must be param=values
                    {
                        Http::send400Response(socket);
                        return;
                    }
                    splittedParams = simu5g::utils::splitString(params[1], ",");
                    std::vector<std::string>::iterator pit  = splittedParams.begin();
                    std::vector<std::string>::iterator pend = splittedParams.end();
                    for(; pit != pend; ++pit){
                        cellIds.push_back((MacNodeId)std::stoi(*pit));
                    }
                }
                else if(it->rfind("ue_ipv4_address", 0) == 0)
                {
                    // TO DO manage acr:10.12
                    params = simu5g::utils::splitString(*it, "=");
                    if(params.size()!= 2) //must be param=values
                    {
                        Http::send400Response(socket);
                        return;
                    }
                    splittedParams = simu5g::utils::splitString(params[1], ",");
                    std::vector<std::string>::iterator pit  = splittedParams.begin();
                    std::vector<std::string>::iterator pend = splittedParams.end();
                    for(; pit != pend; ++pit)
                       ues.push_back(inet::Ipv4Address((*pit).c_str()));
                }
                else // bad parameters
                {
                    Http::send400Response(socket);
                    return;
                }

            }

            //send response
            if(!ues.empty() && !cellIds.empty())
            {
                Http::send200Response(socket, L2MeasResource_.toJson(cellIds, ues).dump().c_str());
            }
            else if(ues.empty() && !cellIds.empty())
            {
                Http::send200Response(socket, L2MeasResource_.toJsonCell(cellIds).dump().c_str());
            }
            else if(!ues.empty() && cellIds.empty())
           {
              Http::send200Response(socket, L2MeasResource_.toJsonUe(ues).dump().c_str());
           }
           else
           {
               Http::send400Response(socket);
           }

        }
        else{
            //no query params
            Http::send200Response(socket,L2MeasResource_.toJson().dump().c_str());
            return;
        }
    }

    else if (uri.compare(baseUriSubscriptions_) == 0) //subs
    {
        // TODO implement subscription?
        Http::send404Response(socket);
    }
    else // not found
    {
        Http::send404Response(socket);
    }

}

void RNIService::handlePOSTRequest(const HttpRequestMessage *currentRequestMessageServed, inet::TcpSocket* socket)
{
    EV << "RNIService::handlePOSRequest" << endl;
    std::string uri = currentRequestMessageServed->getUri();
    std::string body = currentRequestMessageServed->getBody();

    if(uri.compare(baseUriSubscriptions_+"/layer2_meas") == 0)
    {
        nlohmann::json jsonBody;
        try
        {
            jsonBody = nlohmann::json::parse(body); // get the JSON structure
        }
        catch(nlohmann::detail::parse_error e)
        {
            //std::cout << "RNIService::handlePOSTRequest" << e.what() << "\n" << body << std::endl;
            // body is not correctly formatted in JSON, manage it
            Http::send400Response(socket); // bad body JSON
            return;
        }

        L2MeasSubscription* newSubscription = new L2MeasSubscription(subscriptionId_, socket , baseSubscriptionLocation_,  eNodeB_);
        bool res  = newSubscription->fromJson(jsonBody);

        if (res)
        {
            EV << serviceName_ << " - correct subscription created!" << endl;
            // add resource url and send back the response
            nlohmann::ordered_json response = jsonBody;
            std::string resourceUrl = newSubscription->getResourceUrl();
            response["L2MeasurementSubscription"]["resourceURL"] = resourceUrl;
            std::pair<std::string, std::string> p("Location: ", resourceUrl);
            Http::send201Response(socket, response.dump(2).c_str(), p);

            subscriptions_[subscriptionId_] = newSubscription;

            if(newSubscription->getCheckImmediate())
            {
                EventNotification *event = newSubscription->handleSubscription();
                if(event != nullptr)
                    newSubscriptionEvent(event);
            }

            //start timer
            subscriptionTimer_->insertSubId(subscriptionId_);
            if(!subscriptionTimer_->isScheduled())
                scheduleAt(simTime() + subscriptionTimer_->getPeriod(), subscriptionTimer_);
            subscriptionId_ ++;
        }
        else
        {
            delete newSubscription;
            return;
        }

    }
    else
    {
        Http::send404Response(socket);
    }
}

void RNIService::handlePUTRequest(const HttpRequestMessage *currentRequestMessageServed, inet::TcpSocket* socket){}

void RNIService::handleDELETERequest(const HttpRequestMessage *currentRequestMessageServed, inet::TcpSocket* socket)
{
}

void RNIService::finish()
{
// TODO
    return;
}

RNIService::~RNIService(){

}

} //namespace

