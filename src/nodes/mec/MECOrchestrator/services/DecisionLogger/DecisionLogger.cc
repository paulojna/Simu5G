#include "DecisionLogger.h"

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

namespace simu5g {

using namespace omnetpp;

DecisionLogger::DecisionLogger(const std::string& basePath)
{
    std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());
    std::string dirPath = basePath + "run_" + runNumber + "/";

    // Shared with the Controller's output when both are given the same base path, so
    // an existing directory is the normal case rather than an error.
    if (mkdir(dirPath.c_str(), 0775) == -1 && errno != EEXIST) {
        EV_WARN << "DecisionLogger - could not create directory " << dirPath
                << ": " << strerror(errno) << endl;
    }

    fileName_ = dirPath + "run_" + runNumber + "_decisions.csv";
    file_.open(fileName_, std::ios::out | std::ios::trunc);

    if (file_.is_open()) {
        file_ << decisionCsvHeader() << endl;
    }
    else {
        // Losing the record is not worth aborting a run over, but it must not pass
        // unnoticed either: an empty decision file is otherwise indistinguishable
        // from a run in which nothing was decided.
        EV_WARN << "DecisionLogger - could not open " << fileName_
                << "; decisions will not be recorded" << endl;
    }
}

DecisionLogger::~DecisionLogger()
{
    if (file_.is_open())
        file_.close();
}

void DecisionLogger::record(const OrchestrationDecision& decision)
{
    if (!file_.is_open())
        return;

    file_ << decisionCsvRow(decision) << std::endl;
}

} // namespace simu5g
