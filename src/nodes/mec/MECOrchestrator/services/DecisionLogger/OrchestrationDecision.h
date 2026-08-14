#ifndef __ORCHESTRATIONDECISION_H_
#define __ORCHESTRATIONDECISION_H_

#include <string>
#include <sstream>
#include "omnetpp.h"

namespace simu5g {

// One record of the orchestrator deciding something, and of how that decision
// turned out. Written by DecisionLogger, read offline.
//
// The record has two halves that differ in how settled they are. What triggered a
// decision — which device, which hosts, and the times behind it — is fixed by the
// Controller -> orchestrator message shapes. What was decided and why is thinner on
// purpose: it grows when a caller shows a field is missing, because a field that has
// already been written to file and analysed is expensive to correct.

// Where the decision came from. AppRequest is the device asking for an application
// directly rather than RAVENS reporting movement, and it is the one trigger with no
// observation behind it.
enum class DecisionTrigger {
    ConfirmedEvent,
    Prediction,
    AppRequest,
    // A move asked for while another was already in flight for the same user,
    // held until that one finished and started then. Not ConfirmedEvent: the
    // move it carries out was asked for earlier and counting it as a fresh
    // observation would overstate what the event stream asked for.
    PendingRequest,
    // An action returned by the learning engine. Distinct from ConfirmedEvent
    // and Prediction because it rests on neither: the engine decides from a
    // whole telemetry window, so its observedAt is the end of that window
    // rather than the moment of any one observation.
    LearningAction
};

// What was chosen. None is a decision in its own right and the most important one to
// record: a mode whose model never fires takes no action at all, and without these
// rows an empty file cannot be told apart from a logger that never ran.
enum class DecisionKind {
    Place,
    Migrate,
    Remove,
    None
};

// How it turned out. A migration is Initiated in one record and concluded in a
// later one carrying Success or Failed, joined on requestNumber. NotNeeded is not a
// failure and is expected to be common — a confirmed event arriving after the same
// move was already made proactively finds the application where it should be.
enum class DecisionOutcome {
    Initiated,
    Success,
    Failed,
    NotNeeded
};

// Times default to -1 rather than zero because zero is a valid simulation time. A
// field that was never set is then recognisable as unset instead of reading as
// "at the start of the run".
struct OrchestrationDecision {
    omnetpp::simtime_t decidedAt = -1;

    std::string ueAddress;

    DecisionTrigger trigger = DecisionTrigger::ConfirmedEvent;

    // The newest observation the trigger rests on: the instant beyond which the
    // information behind this decision knows nothing. decidedAt minus this is how
    // stale the input already was, measured rather than inferred from the configured
    // intervals.
    omnetpp::simtime_t observedAt = -1;

    // Predictions only. The moment the change was expected. Against observedAt it
    // gives the horizon the model actually delivered; against decidedAt it shows
    // whether enough of that horizon survived the pipeline to act on.
    omnetpp::simtime_t expectedAt = -1;

    // Predictions only, and for comparison and grouping alone. Nothing may branch on
    // which model spoke.
    std::string modelId;

    DecisionKind kind = DecisionKind::None;

    std::string fromMEHId;
    std::string toMEHId;

    DecisionOutcome outcome = DecisionOutcome::NotNeeded;

    // Joins an Initiated record to the Success or Failed record that concludes it.
    // Zero means nothing further is coming.
    unsigned int requestNumber = 0;

    // Why this decision, in the terms of whoever made it: the reason for declining,
    // or the error behind a failure.
    std::string reason;
};

inline const char *decisionTriggerName(DecisionTrigger trigger)
{
    switch (trigger) {
        case DecisionTrigger::ConfirmedEvent: return "ConfirmedEvent";
        case DecisionTrigger::Prediction:     return "Prediction";
        case DecisionTrigger::AppRequest:     return "AppRequest";
        case DecisionTrigger::PendingRequest: return "PendingRequest";
        case DecisionTrigger::LearningAction: return "LearningAction";
    }
    return "Unknown";
}

inline const char *decisionKindName(DecisionKind kind)
{
    switch (kind) {
        case DecisionKind::Place:   return "Place";
        case DecisionKind::Migrate: return "Migrate";
        case DecisionKind::Remove:  return "Remove";
        case DecisionKind::None:    return "None";
    }
    return "Unknown";
}

inline const char *decisionOutcomeName(DecisionOutcome outcome)
{
    switch (outcome) {
        case DecisionOutcome::Initiated: return "Initiated";
        case DecisionOutcome::Success:   return "Success";
        case DecisionOutcome::Failed:    return "Failed";
        case DecisionOutcome::NotNeeded: return "NotNeeded";
    }
    return "Unknown";
}

// An unset time is written as an empty field, so a column that was never measured
// stays visibly empty instead of appearing as a reading taken at time zero.
inline std::string decisionCsvTime(omnetpp::simtime_t time)
{
    if (time < SIMTIME_ZERO)
        return "";
    std::ostringstream out;
    out << time;
    return out.str();
}

// The reason is free text written by the caller, so it cannot be trusted to keep out
// of the delimiter. Quoting it, and doubling any quote inside it, keeps a row from
// silently gaining a column.
inline std::string decisionCsvQuoted(const std::string& text)
{
    std::string quoted = "\"";
    for (char character : text) {
        if (character == '"')
            quoted += "\"\"";
        else
            quoted += character;
    }
    quoted += "\"";
    return quoted;
}

// Header and row are written next to each other, and in the same field order, so a
// column cannot end up under the wrong name.
inline std::string decisionCsvHeader()
{
    return "decidedAt,ueAddress,trigger,observedAt,expectedAt,modelId,"
           "kind,fromMEH,toMEH,outcome,requestNumber,reason";
}

inline std::string decisionCsvRow(const OrchestrationDecision& decision)
{
    std::ostringstream row;
    row << decisionCsvTime(decision.decidedAt)   << ","
        << decision.ueAddress                    << ","
        << decisionTriggerName(decision.trigger) << ","
        << decisionCsvTime(decision.observedAt)  << ","
        << decisionCsvTime(decision.expectedAt)  << ","
        << decision.modelId                      << ","
        << decisionKindName(decision.kind)       << ","
        << decision.fromMEHId                    << ","
        << decision.toMEHId                      << ","
        << decisionOutcomeName(decision.outcome) << ","
        << decision.requestNumber                << ","
        << decisionCsvQuoted(decision.reason);
    return row.str();
}

} // namespace simu5g

#endif
