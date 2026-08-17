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

#include "nodes/mec/MECOrchestrator/MecOrchestrator.h"
#include <omnetpp/cmodule.h>

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

#include "nodes/mec/MECPlatform/ServiceRegistry/ServiceRegistry.h"

#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"

#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppMessage.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppAckMessage.h"

#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/MecServiceSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/AvailableResourcesSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/MecHostSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/LocationSelectionBased.h"

#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/RemoveOnExit.h"
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnChange.h"
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnPrediction.h"
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/LearningStrategy.h"

#include "apps/mec/RavensApps/RavensControlPacket_m.h"

#include "nodes/mec/MECOrchestrator/ApplicationDescriptor/ApplicationDescriptor.h"

// emulation debug
#include <iostream>
#include <curl/curl.h>

#include <set>
#include <sstream>

namespace simu5g
{

// Helper function to replace check_and_cast for IDE compatibility (it is not recognized by OMNET++ IDE)
template<typename T, typename U>
T* safe_check_and_cast(U* ptr) {
    T* result = dynamic_cast<T*>(ptr);
    if (result == nullptr) {
        const char* className = ptr ? (ptr->getClassName() ? ptr->getClassName() : "unknown") : nullptr;
        throw cRuntimeError("check_and_cast failed: cannot cast %s to %s", 
                          className, opp_typename(typeid(T)));
    }
    return result;
}

    Define_Module(MecOrchestrator);

    MecOrchestrator::MecOrchestrator()
    {
        mecHostSelectionPolicy_ = nullptr;
        userPresence_.clear();
        reactionOnUpdate_ = nullptr;
        learningStrategy_ = nullptr;
        // NEW
        mecAppRegistry_ = nullptr;
        mecAppLifecycleManager_ = nullptr;
        mecAppMigrationManager_ = nullptr;
    }

    MecOrchestrator::~MecOrchestrator()
    {
        delete mecHostSelectionPolicy_;
        delete reactionOnUpdate_;
    }

    void MecOrchestrator::initialize(int stage)
    {
        cSimpleModule::initialize(stage);
        // avoid multiple initializations
        if (stage != inet::INITSTAGE_LOCAL)
            return;
        EV << "MecOrchestrator::initialize - stage " << stage << endl;

        binder_ = getBinder();

        if (!strcmp(par("selectionPolicy"), "MecServiceBased"))
            mecHostSelectionPolicy_ = new MecServiceSelectionBased(this);
        else if (!strcmp(par("selectionPolicy"), "AvailableResourcesBased"))
            mecHostSelectionPolicy_ = new AvailableResourcesSelectionBased(this);
        else if (!strcmp(par("selectionPolicy"), "MecHostBased"))
            mecHostSelectionPolicy_ = new MecHostSelectionBased(this, par("mecHostIndex"));
        else if (!strcmp(par("selectionPolicy"), "LocationBased"))
            mecHostSelectionPolicy_ = new LocationSelectionBased(this);
        else
            throw cRuntimeError("MecOrchestrator::initialize - Selection policy %s not present!", par("selectionPolicy").stringValue());

        onboardingTime = par("onboardingTime").doubleValue();
        instantiationTime = par("instantiationTime").doubleValue();
        terminationTime = par("terminationTime").doubleValue();
        migrationTime_ = par("migrationTime").doubleValue();
        migrationTimeout_ = par("migrationTimeout").doubleValue();

        getConnectedMecHosts();
        //onboardApplicationPackages();

        // Before the strategies, which record through it from their first event.
        std::string decisionLogPath = par("decisionLogPath").stringValue();
        if (!decisionLogPath.empty()) {
            decisionLogger_ = std::make_unique<DecisionLogger>(decisionLogPath);
            EV << "MecOrchestrator::initialize - decisions recorded in "
               << decisionLogger_->getFileName() << endl;
        }
        else {
            EV_WARN << "MecOrchestrator::initialize - decisionLogPath is empty, "
                    << "this run will not record what it decided" << endl;
        }

        // NEW
        mecAppRegistry_ = std::make_unique<MecAppRegistry>();
        mecAppLifecycleManager_ = std::make_unique<MecAppLifecycleManager>(
            mecAppRegistry_.get(), mecHostSelectionPolicy_
        );
        mecAppLifecycleManager_->initialize(onboardingTime, instantiationTime, terminationTime);

        if (!strcmp(par("reactionStrategy"), "RemoveOnExit"))
        {
            reactionOnUpdate_ = new RemoveOnExit(static_cast<IOrchestratorApi *>(this));
        }
        else if (!strcmp(par("reactionStrategy"), "MigrateOnChange"))
        {
            mecAppMigrationManager_ = std::make_unique<MecAppMigrationManager>(
              mecAppRegistry_.get(),
              mecAppLifecycleManager_.get(),
              &mecHosts,
              &mecHostIndex_,
              this,
              decisionLogger_.get()
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            reactionOnUpdate_ = new MigrateOnChange(static_cast<IOrchestratorApi *>(this));
        }
        else if (!strcmp(par("reactionStrategy"), "MigrateOnPrediction"))
        {
            mecAppMigrationManager_ = std::make_unique<MecAppMigrationManager>(
              mecAppRegistry_.get(),
              mecAppLifecycleManager_.get(),
              &mecHosts,
              &mecHostIndex_,
              this,
              decisionLogger_.get()
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            reactionOnUpdate_ = new MigrateOnPrediction(
                static_cast<IOrchestratorApi *>(this), this, migrationTime_,
                par("reactiveFallback").boolValue());
        }
        else if (!strcmp(par("reactionStrategy"), "Learning"))
        {
            mecAppMigrationManager_ = std::make_unique<MecAppMigrationManager>(
              mecAppRegistry_.get(),
              mecAppLifecycleManager_.get(),
              &mecHosts,
              &mecHostIndex_,
              this,
              decisionLogger_.get()
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            std::string engineUrl = par("learningEngineUrl").stdstringValue();
            if (engineUrl.empty())
                throw cRuntimeError("MecOrchestrator::initialize - the Learning strategy has no "
                                    "policy without an engine; set learningEngineUrl");

            learningStrategy_ = new LearningStrategy(
                static_cast<IOrchestratorApi *>(this), engineUrl,
                par("learningEngineTimeout").doubleValue());
            reactionOnUpdate_ = learningStrategy_;
        }
        else
            throw cRuntimeError("MecOrchestrator::initialize - Reaction strategy %s not present!", par("reactionStrategy").stringValue());

        if (!reactionOnUpdate_)
            throw cRuntimeError("MecOrchestrator::initialize - reactionOnUpdate_ is null");

        mecAppLifecycleManager_->onboardApplicationPackages(par("mecApplicationPackageList").stringValue());

        // Last, because it reads both the MEC host list and the descriptors
        // onboarded just above.
        if (learningStrategy_ != nullptr)
            openLearningEpisode();
    }

    bool MecOrchestrator::consumesPredictions()
    {
        if (strcmp(par("reactionStrategy"), "MigrateOnPrediction") == 0)
            return true;

        // The learning ablation, and the only reason this is a question rather
        // than a property of the strategy name: the same strategy runs with the
        // prediction stream and without it, and this answer is what turns the
        // Controller's model server on for the runs that want it.
        return strcmp(par("reactionStrategy"), "Learning") == 0
               && par("learningUsesPredictions").boolValue();
    }

    bool MecOrchestrator::consumesTelemetry()
    {
        // The learning strategy is the only one that reads raw telemetry: its
        // decision step is the window, and the samples in it are most of what
        // the engine sees.
        return strcmp(par("reactionStrategy"), "Learning") == 0;
    }

    void MecOrchestrator::handleMessage(cMessage *msg)
    {
        if (msg->isSelfMessage())
        {
            if (strcmp(msg->getName(), "MECOrchestratorMessage") == 0)
            {
                EV << "MecOrchestrator::handleMessage - " << msg->getName() << endl;
                MECOrchestratorMessage *meoMsg = safe_check_and_cast<MECOrchestratorMessage>(msg);
                if (strcmp(meoMsg->getType(), CREATE_CONTEXT_APP) == 0)
                {
                    if (meoMsg->getSuccess())
                        sendCreateAppContextAck(true, meoMsg->getRequestId(), meoMsg->getContextId());
                    else
                        sendCreateAppContextAck(false, meoMsg->getRequestId());
                }
                else if (strcmp(meoMsg->getType(), DELETE_CONTEXT_APP) == 0)
                    sendDeleteAppContextAck(meoMsg->getSuccess(), meoMsg->getRequestId(), meoMsg->getContextId());
            }
            else if (strcmp(msg->getName(), "UpdateFromRAVENS") == 0)
            {
                EV << "I'm going to do something here" << endl;
            }
            else if (strcmp(msg->getName(), "MigrationTimeout") == 0)
            {
                MigrateTimeoutMessage* timeoutMsg = check_and_cast<MigrateTimeoutMessage*>(msg);
                mecAppMigrationManager_->handleMigrationTimeout(timeoutMsg->getRequestNumber());
            }
            else if (strcmp(msg->getName(), "ScheduledMigration") == 0)
            {
                reactionOnUpdate_->handleScheduledEvent(msg);
            }
        }
        // handle message from the LCM proxy
        else if (msg->arrivedOn("fromUALCMP"))
        {
            EV << "MecOrchestrator::handleMessage - " << msg->getName() << endl;
            handleUALCMPMessage(msg);
        }
        else if (msg->arrivedOn("fromRavensController"))
        {
            EV << "MecOrchestrator::handleMessage fromRavensController - " << msg->getName() << endl;

            inet::Packet *packet = safe_check_and_cast<inet::Packet>(msg);
            auto received_packet = packet->peekAtFront<RavensControlPacket>();

            switch (received_packet->getType())
            {
            case USER_EVENT: {
                // One confirmed change, and it arrived the moment RAVENS
                // concluded it. Which of fromMEHId / toMEHId are set follows from
                // the event type; nothing here infers the event from them.
                // The chunk is held in a named variable rather than chained,
                // so the reference below cannot outlive it.
                auto eventMessage = packet->peekAtFront<UserEventMessage>();
                const UserEvent& event = eventMessage->getEvent();
                EV << "MecOrchestrator::handleMessage - " << userEventTypeName(event.eventType)
                   << " " << event.ueAddress << " '" << event.fromMEHId << "' -> '"
                   << event.toMEHId << "'" << endl;

                // An exit leaves toMEHId empty, which is what should be recorded:
                // the user is no longer anywhere. The row stays either way.
                UserPresence& presence = userPresence_[canonicalUeAddress(event.ueAddress)];
                presence.currentMEH = event.toMEHId;
                presence.lastEventAt = event.observedAt;
                reactionOnUpdate_->reactOnUpdate(event);
                break;
            }

            case PREDICTION_REPORT: {
                auto report = packet->peekAtFront<PredictionReportMessage>();
                const std::vector<MigrationPrediction>& predictions = report->getPredictions();
                EV << "MecOrchestrator::handleMessage - " << predictions.size()
                   << " predictions received" << endl;
                reactionOnUpdate_->reactOnUpdate(predictions);
                break;
            }

            case TELEMETRY_REPORT: {
                // Forwarded on every run that collects telemetry, and consumed
                // only by a strategy that asked for it — the default
                // implementation of reactOnTelemetry ignores it. Being sent
                // regardless is what makes the learning mode a configuration
                // change rather than a different code path.
                auto report = packet->peekAtFront<TelemetryReportMessage>();

                // Enrich the user view. Telemetry writes only its own two
                // fields — it never touches currentMEH, which events own. A
                // user crossing hosts mid-window appears once per observing
                // host, so the newest sample by its own timestamp wins.
                for (const auto& sample : report->getUserSamples()) {
                    UserPresence& presence = userPresence_[canonicalUeAddress(sample.userId)];
                    if (sample.locationTimestamp >= presence.lastSampleAt) {
                        presence.lastObservedMEH = sample.observedMEH;
                        presence.lastSampleAt = sample.locationTimestamp;
                    }
                }

                reactionOnUpdate_->reactOnTelemetry(report->getWindowStart(),
                                                    report->getWindowEnd(),
                                                    report->getReportingMEHIds(),
                                                    report->getUserSamples(),
                                                    report->getCellSamples());
                break;
            }

            default:
                EV << "MecOrchestrator::handleMessage - unknown RAVENS stream type "
                   << received_packet->getType() << ", ignoring" << endl;
                break;
            }
        }

        delete msg;
        return;
    }

    void MecOrchestrator::handleUALCMPMessage(cMessage *msg)
    {
        UALCMPMessage *lcmMsg = safe_check_and_cast<UALCMPMessage>(msg);

        /* Handling CREATE_CONTEXT_APP */
        if (!strcmp(lcmMsg->getType(), CREATE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->startApplication(lcmMsg);
            if(result.success) {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling DELETE_CONTEXT_APP */
        else if (!strcmp(lcmMsg->getType(), DELETE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->stopApplication(lcmMsg);
            if(result.success) {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling confirmation of MEH change*/
        else if (!strcmp(lcmMsg->getType(), ACK_UPDATE_MEH_IP))
        {
            //std::cout << "ACK_UPDATE_MEH_IP RECEIVED!!" << endl;
            if (!mecAppMigrationManager_) 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration manager not initialized" << endl;
                //std::cout << "BIG PROBLEMS!!" << endl;
                return;
            }

            MigrationResult result = mecAppMigrationManager_->completeMigration(lcmMsg);
            if (!result.success) 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration completion failed: " << result.errorMessage << endl;
            } 
            else 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration completed successfully" << endl;
            }
        }
        else
            throw cRuntimeError("MecOrchestrator::handleUALCMPMessage - Message type %s not recognized", lcmMsg->getType());
    }

    void MecOrchestrator::sendMehChangeRequest(std::string ueAddress, std::string newMehId, int newPort, unsigned int requestNumber)
    {
        EV << "MecOrchestrator::sendMehChangeRequest - sending MEH change request to UALCMP" << endl;
        UpdateMEHMessage *mehChangeRequest = new UpdateMEHMessage();
        mehChangeRequest->setType(UPDATE_MEH_IP);
        mehChangeRequest->setUeIpAddress(ueAddress.c_str());
        mehChangeRequest->setNewMehIpAddress(newMehId.c_str());
        mehChangeRequest->setNewMehPort(newPort);
        mehChangeRequest->setRequestNumber(requestNumber);

        // send(mehChangeRequest, "toUALCMP");
        sendDelayed(mehChangeRequest, migrationTime_, "toUALCMP");
    }

    void MecOrchestrator::sendDeleteAppContextAck(bool result, unsigned int requestSno, int contextId)
    {
        EV << "MecOrchestrator::sendDeleteAppContextAck - result: " << result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
        DeleteContextAppAckMessage *ack = new DeleteContextAppAckMessage();
        ack->setType(ACK_DELETE_CONTEXT_APP);
        ack->setRequestId(requestSno);
        ack->setSuccess(result);

        send(ack, "toUALCMP");
    }

    void MecOrchestrator::sendCreateAppContextAck(bool result, unsigned int requestSno, int contextId)
    {
        EV << "MecOrchestrator::sendCreateAppContextAck - result: " << result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
        CreateContextAppAckMessage *ack = new CreateContextAppAckMessage();
        ack->setType(ACK_CREATE_CONTEXT_APP);

        if (result)
        {
            // NEW 
            auto result = mecAppRegistry_->findAppByContextId(contextId);
            if(!result.found)
            {
                EV << "MecOrchestrator::sendCreateAppContextAck - ERROR meApp[" << contextId << "] does not exist!" << endl;
                return;
            }

            const MecAppRegistry::AppEntry& mecAppStatus = *result.appEntry;

            ack->setSuccess(true);
            ack->setContextId(contextId);
            ack->setRequestId(requestSno);

            //NEW
            ack->setAppInstanceId(mecAppStatus.mecAppInstanceId.c_str());
            ack->setAppInstanceUri(mecAppStatus.getEndpointString().c_str());
        }
        else
        {
            ack->setRequestId(requestSno);
            ack->setSuccess(false);
        }
        send(ack, "toUALCMP");
    }

    cModule *MecOrchestrator::findBestMecHost(const ApplicationDescriptor &appDesc)
    {

        EV << "MecOrchestrator::findBestMecHost - finding best MecHost..." << endl;
        cModule *bestHost = nullptr;

        for (auto mecHost : mecHosts)
        {
            VirtualisationInfrastructureManager *vim = dynamic_cast<VirtualisationInfrastructureManager *>(mecHost->getSubmodule("vim"));
            if (vim == nullptr) {
                throw cRuntimeError("check_and_cast failed: cannot cast submodule 'vim' to VirtualisationInfrastructureManager");
            }
            ResourceDescriptor resources = appDesc.getVirtualResources();
            bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);
            if (!res)
            {
                EV << "MecOrchestrator::findBestMecHost - MEC host []" << mecHost << " has not got enough resources. Searching again..." << endl;
                continue;
            }

            // Temporally select this mec host as the best
            EV << "MecOrchestrator::findBestMecHost - MEC host []" << mecHost << " temporally chosen as bet MEC host, checking for the required MEC services.." << endl;
            bestHost = mecHost;

            MecPlatformManager *mecpm = dynamic_cast<MecPlatformManager *>(mecHost->getSubmodule("mecPlatformManager"));
            if (mecpm == nullptr) {
                throw cRuntimeError("check_and_cast failed: cannot cast submodule 'mecPlatformManager' to MecPlatformManager");
            }
            auto mecServices = mecpm->getAvailableMecServices();
            std::string serviceName;

            /* I assume the app requires only one mec service */
            if (appDesc.getAppServicesRequired().size() > 0)
            {
                serviceName = appDesc.getAppServicesRequired()[0];
            }
            else
            {
                break;
            }
            auto it = mecServices->begin();
            for (; it != mecServices->end(); ++it)
            {
                if (serviceName.compare(it->getName()) == 0)
                {
                    bestHost = mecHost;
                    break;
                }
            }
        }
        if (bestHost != nullptr)
            EV << "MecOrchestrator::findBestMecHost - best MEC host: " << bestHost << endl;
        else
            EV << "MecOrchestrator::findBestMecHost - no MEC host found" << endl;

        return bestHost;
    }

    void MecOrchestrator::openLearningEpisode()
    {
        std::vector<HostTopology> hosts;
        std::vector<CellTopology> cells;
        std::set<std::string> namedCells;

        for (cModule *mecHost : mecHosts)
        {
            HostTopology host;
            host.host = mecHost->getName();
            host.maxRam = mecHost->hasPar("maxRam") ? mecHost->par("maxRam").doubleValue() : 0;

            // A host with no base stations covers no cells of its own, which is
            // what the cloud fallback is: it is never the closest host, only the
            // one left when no edge host can allocate. MECHost.ned defaults
            // bsList to empty and MecServiceBase reads it the same way.
            std::string bsList = mecHost->hasPar("bsList") ? mecHost->par("bsList").stdstringValue() : "";
            host.isCloud = bsList.empty();

            std::stringstream names(bsList);
            std::string cellName;
            while (std::getline(names, cellName, ','))
            {
                // bsList is written by hand in the ini, so "gnb1, gnb2" is as
                // likely as "gnb1,gnb2".
                size_t first = cellName.find_first_not_of(" \t");
                if (first == std::string::npos)
                    continue;
                cellName = cellName.substr(first, cellName.find_last_not_of(" \t") - first + 1);

                host.cells.push_back(cellName);
                if (!namedCells.insert(cellName).second)
                    continue;

                cModule *cellModule = getSimulation()->getModuleByPath(cellName.c_str());
                if (cellModule == nullptr)
                    throw cRuntimeError("MecOrchestrator::openLearningEpisode - %s names cell %s, "
                                        "which is not a module", host.host.c_str(), cellName.c_str());

                // Where the physical layer itself reads a base station's
                // position from, so this is the same coordinate system the
                // Location Service reports users in — and the same metres. A
                // cell without one would put every distance the engine computes
                // at the origin, silently, so it is an error rather than a
                // default.
                const char *x = cellModule->getDisplayString().getTagArg("p", 0);
                const char *y = cellModule->getDisplayString().getTagArg("p", 1);
                if (x == nullptr || *x == '\0' || y == nullptr || *y == '\0')
                    throw cRuntimeError("MecOrchestrator::openLearningEpisode - cell %s has no "
                                        "position in its display string", cellName.c_str());

                CellTopology cell;
                cell.cell = cellName;
                cell.x = atof(x);
                cell.y = atof(y);
                cells.push_back(cell);
            }

            hosts.push_back(host);
        }

        // No telemetry interval here: it is the Controller's parameter, not this
        // module's, and every step already carries the window it covers — so the
        // engine reads the interval off windowEnd minus windowStart rather than
        // being told a second copy that could disagree.
        nlohmann::json config;
        config["migrationTime"]   = migrationTime_;
        config["usesPredictions"] = par("learningUsesPredictions").boolValue();

        // What one application costs, so the engine can work each host's
        // occupancy out of the application view it already receives instead of
        // being told a number that is a fixed multiple of one it has. Unset
        // when the descriptors do not name exactly one application: the
        // arithmetic only holds while every user runs the same thing.
        config["ramPerApp"] = -1.0;
        const std::map<std::string, ApplicationDescriptor> *descriptors = getAllApplicationDescriptors();
        if (descriptors != nullptr && descriptors->size() == 1)
            config["ramPerApp"] = descriptors->begin()->second.getVirtualResources().ram;
        else
            EV_WARN << "MecOrchestrator::openLearningEpisode - not exactly one application "
                    << "descriptor, so ramPerApp is unset" << endl;

        EV << "MecOrchestrator::openLearningEpisode - " << hosts.size() << " hosts, "
           << cells.size() << " cells" << endl;

        learningStrategy_->openEpisode(hosts, cells, config);
    }

    void MecOrchestrator::finish()
    {
        if (learningStrategy_ != nullptr)
            learningStrategy_->closeEpisode();

        // Whatever the strategy counted across the run, recorded while this
        // module is still whole enough to record it. A strategy's own destructor
        // runs after finish(), by which point recordScalar() writes nothing.
        if (reactionOnUpdate_ != nullptr)
            reactionOnUpdate_->onRunFinished();
    }

    void MecOrchestrator::getConnectedMecHosts()
    {
        // getting the list of mec hosts associated to this mec system from parameter
        if (this->hasPar("mecHostList") && strcmp(par("mecHostList").stringValue(), ""))
        {
            std::string mecHostList = par("mecHostList").stdstringValue();
            EV << "MecOrchestrator::getConnectedMecHosts - mecHostList: " << par("mecHostList").stringValue() << endl;
            char *token = strtok((char *)mecHostList.c_str(), ", "); // split by commas

            while (token != NULL)
            {
                EV << "MecOrchestrator::getConnectedMecHosts - mec host (from par): " << token << endl;
                cModule *mecHostModule = getSimulation()->getModuleByPath(token);
                mecHosts.push_back(mecHostModule);
                // PERFORMANCE IMPROVEMENT: Build index for O(1) lookup by name
                mecHostIndex_[mecHostModule->getName()] = mecHostModule;
                token = strtok(NULL, ", ");
            }
        }
        else
        {
            //        throw cRuntimeError ("MecOrchestrator::getConnectedMecHosts - No mecHostList found");
            EV << "MecOrchestrator::getConnectedMecHosts - No mecHostList found" << endl;
        }
    }

    void MecOrchestrator::registerMecService(ServiceDescriptor &serviceDescriptor) const
    {
        EV << "MecOrchestrator::registerMecService - Registering MEC service [" << serviceDescriptor.name << "]" << endl;
        for (auto mecHost : mecHosts)
        {
            cModule *module = mecHost->getSubmodule("mecPlatform")->getSubmodule("serviceRegistry");
            if (module != nullptr)
            {
                EV << "MecOrchestrator::registerMecService - Registering MEC service [" << serviceDescriptor.name << "] in MEC host [" << mecHost->getName() << "]" << endl;
                ServiceRegistry *serviceRegistry = check_and_cast<ServiceRegistry *>(module);
                serviceRegistry->registerMecService(serviceDescriptor);
            }
        }
    }

    void MecOrchestrator::removeAppFromSystem(std::string ueAddress, std::string oldMEHId)
    {
        std::string ueIp = ueAddress;
        if (ueAddress.find("acr:") == 0) {
            ueIp = ueAddress.substr(4);
        }
        //std::cout << simTime() << " [MEO] App REMOVE requested for UE: " << ueIp << std::endl;
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        // Deliberately no touch of userPresence_ here: where the user is, is
        // the events' fact; this function only removes the application.

        // requestId will be zero so the UALCMP will not try to send a response to the UE when receives the ack from the MEO
        int requestId = 0; // I've changed the starting value of the request counter to 1

        // NEW - Search with IP (without prefix)
        auto result = mecAppRegistry_->findAppByUeAddress(ueIp);
        if(!result.found)
        {
            //std::cout << "RemoveOnExit::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueIp << endl;
            return;
        }
        int contextId = result.contextId;


        if (contextId == -1)
        {
            std::cout << "RemoveOnExit::reactOnUpdate - ERROR: -1.. contextId not found for ueAddress " << ueIp << endl;
            return;
        }

        // create the message to stop the MEC app
        DeleteContextAppMessage *msg = new DeleteContextAppMessage("DeleteContextAppMessage");
        msg->setType(DELETE_CONTEXT_APP);
        msg->setRequestId(requestId);
        msg->setContextId(contextId);

        // invoke stopMECApp method from MecOrchestrator
        EV << "RemoveOnExit::reactOnUpdate - sending DeleteContextAppMessage to MecOrchestrator to stop MEC app with contextId " << contextId << endl;
        mecAppLifecycleManager_->stopApplication(msg);
        delete msg;
    }

    MigrationResult MecOrchestrator::migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId) {
        return mecAppMigrationManager_->migrateApp(ueAddress, newMEHId, oldMEHId);
    }

    void MecOrchestrator::recordDecision(const OrchestrationDecision& decision) {
        if (decisionLogger_)
            decisionLogger_->record(decision);
    }

    // The migration manager only exists under strategies that migrate; a
    // strategy reaching these without having built one is a wiring mistake,
    // and the guards turn it into an error naming the strategy instead of a
    // segfault.
    MigrationResult MecOrchestrator::checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) {
        if (!mecAppMigrationManager_)
            throw cRuntimeError("MecOrchestrator::checkIfMigrationIsNeeded - strategy %s builds no migration manager",
                                par("reactionStrategy").stringValue());
        return mecAppMigrationManager_->checkIfMigrationIsNeeded(ueAddress, newMEHId, oldMEHId);
    }

    MigrationResult MecOrchestrator::completeMigration(UALCMPMessage* ackMsg) {
        if (!mecAppMigrationManager_)
            throw cRuntimeError("MecOrchestrator::completeMigration - strategy %s builds no migration manager",
                                par("reactionStrategy").stringValue());
        return mecAppMigrationManager_->completeMigration(ackMsg);
    }

    std::string MecOrchestrator::getAppCurrentMEH(std::string ueAddress) {
        // An application-view read, answered by the registry, which exists
        // under every strategy — the migration manager does not (RemoveOnExit
        // builds none), so routing this read through it crashed collection
        // runs.
        std::string ueIp = ueAddress;
        if (ueAddress.find("acr:") == 0)
            ueIp = ueAddress.substr(4);
        auto result = mecAppRegistry_->findAppByUeAddress(ueIp);
        return result.found ? result.appEntry->mecHost->getName() : "";
    }

    std::vector<AppPlacement> MecOrchestrator::getAppPlacements() {
        // Every entry, Gone ones included — unlike the registry's lookups, which
        // treat Gone as not found because every one of their callers means a live
        // application. Here the deletion is itself part of what the caller is
        // reporting.
        std::vector<AppPlacement> placements;
        placements.reserve(mecAppRegistry_->getAppCount());

        for (const auto& pair : *mecAppRegistry_) {
            AppPlacement row;
            row.ueAddress = pair.second.ueAddress.str();
            row.mecHost   = pair.second.mecHost != nullptr ? pair.second.mecHost->getName() : "";
            row.state     = pair.second.state;
            row.contextId = pair.first;
            placements.push_back(row);
        }
        return placements;
    }

    std::vector<UserPresenceRow> MecOrchestrator::getUserPresence() {
        // One row per user ever observed, exited users included: their row holds
        // an empty currentMEH and the timestamp of the event that emptied it,
        // which is the fact that they left.
        std::vector<UserPresenceRow> rows;
        rows.reserve(userPresence_.size());

        for (const auto& pair : userPresence_) {
            UserPresenceRow row;
            row.ueAddress       = pair.first;
            row.currentMEH      = pair.second.currentMEH;
            row.lastEventAt     = pair.second.lastEventAt;
            row.lastObservedMEH = pair.second.lastObservedMEH;
            row.lastSampleAt    = pair.second.lastSampleAt;
            rows.push_back(row);
        }
        return rows;
    }

    const ApplicationDescriptor* MecOrchestrator::getApplicationDescriptorByAppName(std::string& appName) const
    {
        if(!mecAppLifecycleManager_)
        {
            EV << "MecOrchestrator::getApplicationDescriptorByAppName - ERROR: mecAppLifecycleManager_ not found" << endl;
            return nullptr;
        }
        const ApplicationDescriptor* appDescriptor = mecAppLifecycleManager_->getApplicationDescriptorByAppName(appName);
        return appDescriptor ? appDescriptor : nullptr;
    }

    const std::map<std::string, ApplicationDescriptor>* MecOrchestrator::getAllApplicationDescriptors() const
    {
        if(!mecAppLifecycleManager_)
        {
            EV << "MecOrchestrator::getAllApplicationDescriptors - ERROR: mecAppLifecycleManager_ not found" << endl;
            return nullptr;
        }
        return mecAppLifecycleManager_->getAllApplicationDescriptors();
    }
} // namespace
