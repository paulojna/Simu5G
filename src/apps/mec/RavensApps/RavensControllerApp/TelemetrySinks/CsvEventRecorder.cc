#include "CsvEventRecorder.h"

namespace simu5g {

CsvEventRecorder::CsvEventRecorder(RavensControllerApp* controllerApp, std::string path)
    : TelemetrySink(controllerApp)
{
    std::string dirPath = ensureRunDirectory(path);
    std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());

    // firstDetectedAt is when the Agent saw the change; the leading timestamp is
    // when the Controller concluded it. The gap between them is the reporting
    // delay, and for an exit it also contains the whole confirmation window.
    std::string lifecycleName = dirPath + "run_" + runNumber + "_lifecycle.csv";
    lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    lifecycleFile << "timestamp,eventType,userId,fromMEH,toMEH,samplesSinceChange,firstDetectedAt" << endl;

    EV << "CsvEventRecorder initialized: " << lifecycleName << endl;
}

// Flushed on every row. Events are rare — a handful per user for a whole run —
// so there is nothing to gain by holding them, and a run that ends badly still
// leaves a complete account of what happened.
void CsvEventRecorder::onUserEvent(const UserEvent& event, int samplesSinceChange)
{
    lifecycleFile << simTime() << ","
                  << userEventTypeName(event.eventType) << ","
                  << event.ueAddress << ","
                  << event.fromMEHId << ","
                  << event.toMEHId << ","
                  << samplesSinceChange << ","
                  << event.observedAt << "\n";
    lifecycleFile.flush();
}

CsvEventRecorder::~CsvEventRecorder()
{
    lifecycleFile.close();
}

} // namespace simu5g
