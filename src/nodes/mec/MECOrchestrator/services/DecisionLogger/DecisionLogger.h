#ifndef __DECISIONLOGGER_H_
#define __DECISIONLOGGER_H_

#include <fstream>
#include <string>

#include "nodes/mec/MECOrchestrator/services/DecisionLogger/OrchestrationDecision.h"

namespace simu5g {

// Writes one row per orchestration decision, for offline comparison between runs.
//
// The Controller already records what happened in the network. This records what the
// orchestrator did about it, which exists nowhere else — and since every mode is
// judged by whether it decided better, the record has to be produced identically by
// all of them rather than by whichever mode happens to write its own log.
//
// Rows only. Summary counters belong with the call sites that can count them, and
// recording them as scalars needs the scenario's blanket scalar filtering opened up
// per name, or an unrecorded counter reads as a counter that stayed at zero.
//
// Directory layout follows the Controller's: given the same base path, both sides
// write into the same run_<N>/ directory and share the run number.
class DecisionLogger {
  public:
    // Opens run_<N>/run_<N>_decisions.csv beneath basePath and writes the header.
    explicit DecisionLogger(const std::string& basePath);
    ~DecisionLogger();

    DecisionLogger(const DecisionLogger&) = delete;
    DecisionLogger& operator=(const DecisionLogger&) = delete;

    // Appends one record. Flushes as it goes: a run that ends abruptly is exactly the
    // run whose last decisions matter, and holding them in a buffer would lose them.
    void record(const OrchestrationDecision& decision);

    bool isOpen() const { return file_.is_open(); }

    // Where rows are being written, for reporting at startup.
    const std::string& getFileName() const { return fileName_; }

  private:
    std::ofstream file_;
    std::string fileName_;
};

} // namespace simu5g

#endif
