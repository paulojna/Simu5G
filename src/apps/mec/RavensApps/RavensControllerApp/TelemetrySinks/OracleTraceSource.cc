#include "OracleTraceSource.h"

#include "apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.h"

#include <inet/networklayer/common/L3AddressResolver.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace simu5g {

using namespace omnetpp;

OracleTraceSource::OracleTraceSource(RavensControllerApp* controllerApp,
                                     const std::string& traceFile, double leadTime,
                                     double migrationTime)
    : TelemetrySink(controllerApp), leadTime_(leadTime), migrationTime_(migrationTime)
{
    loadTrace(traceFile);

    // Both ends of the window leadTime has to sit in, checked against the trace
    // that is actually going to be replayed rather than against an assumption
    // about it.
    //
    // Too little lead is visible in the results — the orchestrator counts it as
    // latePredictions — so it is only reported here. Too much is not visible
    // anywhere: the second prediction cancels the first before it fires, the car
    // never migrates to the host in between, and the run records a clean
    // sequence of migrations that simply omits one. Warned rather than refused
    // because a run deliberately probing that boundary is a legitimate thing to
    // want; silent is the only thing it must not be.
    if (leadTime_ < migrationTime_) {
        EV_WARN << "OracleTraceSource - leadTime " << leadTime_ << "s is below the orchestrator's "
                << "migrationTime " << migrationTime_ << "s, so no migration can complete before "
                << "its user arrives. Every move will count as a late prediction." << endl;
    }

    simtime_t dwell = shortestDwell();
    if (dwell >= SIMTIME_ZERO && leadTime_ - migrationTime_ > dwell.dbl()) {
        EV_WARN << "OracleTraceSource - leadTime " << leadTime_ << "s leaves "
                << (leadTime_ - migrationTime_) << "s between a prediction firing and its move, "
                << "which is more than the shortest gap between two moves of one car ("
                << dwell << "s). Predictions will overtake each other and a car will skip a host "
                << "it should have visited." << endl;
    }

    EV << "OracleTraceSource - " << rows_.size() << " moves loaded from " << traceFile
       << ", delivered " << leadTime_ << "s ahead; shortest dwell " << dwell << "s" << endl;
}

/*
    Reads the trace.

    Columns: carIndex,expectedAt,fromCell,toCell. A header line and blank or
    '#'-prefixed lines are skipped, so the file the analysis script writes can
    carry both without a second format for this to parse.

    Every failure is fatal, deliberately. A trace that does not load leaves the
    Controller with nothing to deliver, and the orchestrator then behaves exactly
    as it would under Proactive with an unreachable server: it falls back on
    confirmed handovers and produces a result that looks like a poor oracle
    rather than like a broken run. That is the single most expensive way this
    could go wrong, so it stops here instead.
*/
void OracleTraceSource::loadTrace(const std::string& traceFile)
{
    if (traceFile.empty())
        throw cRuntimeError("OracleTraceSource - no trace file given");

    std::ifstream file(traceFile);
    if (!file.is_open())
        throw cRuntimeError("OracleTraceSource - could not open %s. The oracle arm replays a "
                            "trace recorded by an earlier CollectHistory run of the same "
                            "repetition; check that it was produced and that the path is "
                            "relative to the simulation's working directory.",
                            traceFile.c_str());

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;

        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream fields(line);
        std::string carIndexField, expectedAtField, fromCellField, toCellField;
        if (!std::getline(fields, carIndexField, ',') ||
            !std::getline(fields, expectedAtField, ',') ||
            !std::getline(fields, fromCellField, ',') ||
            !std::getline(fields, toCellField, ','))
        {
            throw cRuntimeError("OracleTraceSource - %s line %d has fewer than four fields: %s",
                                traceFile.c_str(), lineNumber, line.c_str());
        }

        // A column header, skipped by shape rather than by position: car indices
        // are non-negative, so a first field that does not start with a digit is
        // not a row. Testing the shape instead of assuming the header is line 1
        // means the file may carry comments above it.
        if (carIndexField.empty() || !std::isdigit(static_cast<unsigned char>(carIndexField[0])))
            continue;

        TraceRow row;
        int fromCell = 0;
        int toCell = 0;
        try {
            row.carIndex = std::stoi(carIndexField);
            row.expectedAt = std::stod(expectedAtField);
            fromCell = std::stoi(fromCellField);
            toCell = std::stoi(toCellField);
        }
        catch (const std::exception&) {
            throw cRuntimeError("OracleTraceSource - %s line %d is not readable as "
                                "carIndex,expectedAt,fromCell,toCell: %s",
                                traceFile.c_str(), lineNumber, line.c_str());
        }

        // Cell 0 is "attached to nothing", which is an entry or an exit rather
        // than a move. Those keep coming from RAVENS in this arm, so a row
        // carrying one means the trace script kept something it should have
        // filtered and the two sides disagree about what the file contains.
        if (fromCell <= 0 || toCell <= 0)
            throw cRuntimeError("OracleTraceSource - %s line %d is not a cell-to-cell move "
                                "(%d -> %d). Attach and detach belong to the event stream, not "
                                "to the trace.", traceFile.c_str(), lineNumber, fromCell, toCell);

        // The scenario is one MEC host per base station, wired that way in the
        // network: mecHostN hangs off iUpfN which serves gnbN.
        row.fromMEHId = "mecHost" + std::to_string(fromCell);
        row.toMEHId = "mecHost" + std::to_string(toCell);

        // Clamped rather than allowed to go negative. A move inside the first
        // leadTime seconds of the run cannot be given its full warning, and
        // starting as early as possible is the best available — the shortfall
        // then shows up as latePredictions on the orchestrator, which is where
        // lateness is already counted for the proactive arm.
        row.sendAt = std::max(row.expectedAt - leadTime_, simTime());

        rows_.push_back(row);
    }

    if (rows_.empty())
        throw cRuntimeError("OracleTraceSource - %s holds no moves. An oracle run with an empty "
                            "trace is a reactive run wearing the wrong name.", traceFile.c_str());

    std::sort(rows_.begin(), rows_.end(),
              [](const TraceRow& a, const TraceRow& b) { return a.sendAt < b.sendAt; });
}

simtime_t OracleTraceSource::nextDueTime() const
{
    return next_ < rows_.size() ? rows_[next_].sendAt : SimTime(-1);
}

/*
    Hands over every move now due.

    observedAt is the moment of delivery rather than the moment of the handover,
    and that is the honest value: it is what the Controller knew and when it knew
    it. Read against expectedAt in the decision log it gives the horizon this arm
    delivered — leadTime, by construction — which is the same quantity the
    proactive arm's rows carry, so the two sit on one axis without either being
    special-cased.
*/
void OracleTraceSource::releaseDue()
{
    while (next_ < rows_.size() && rows_[next_].sendAt <= simTime()) {
        const TraceRow& row = rows_[next_];
        next_++;

        std::string ueAddress = resolveCarAddress(row.carIndex);
        if (ueAddress.empty()) {
            // The car this move belongs to is not in the network right now. With
            // the same repetition driving the same SUMO run this should not
            // happen, so it is counted rather than passed over: a non-zero total
            // means the trace and the run have come apart, and everything else
            // this arm reports has to be read in that light.
            unresolved_++;
            EV_WARN << "OracleTraceSource::releaseDue - car[" << row.carIndex << "] does not "
                    << "exist; its move to " << row.toMEHId << " expected at " << row.expectedAt
                    << " cannot be replayed (" << unresolved_ << " so far)" << endl;
            continue;
        }

        MigrationPrediction prediction;
        prediction.ueAddress = ueAddress;
        prediction.fromMEHId = row.fromMEHId;
        prediction.toMEHId = row.toMEHId;
        prediction.observedAt = simTime();
        prediction.expectedAt = row.expectedAt;
        prediction.confidence = 1.0;
        prediction.modelId = "oracle";

        // One message per move. MigrateOnPrediction keeps a single pending entry
        // per user, so batching two moves of one car would leave only the
        // second — and it would arrive with the intervening host never visited.
        controllerApp_->publishPredictions({prediction});
        replayed_++;
    }
}

/*
    The car's address in this run.

    Late by necessity: cars are created by Veins as SUMO introduces them, so at
    the moment the trace is loaded almost none of them exist. Resolving here
    also means the address is this run's, which is the entire reason the trace
    carries indices — see the class comment.
*/
std::string OracleTraceSource::resolveCarAddress(int carIndex) const
{
    cModule* network = getSimulation()->getSystemModule();
    cModule* car = network->getSubmodule("car", carIndex);
    if (car == nullptr)
        return "";

    inet::L3Address address = inet::L3AddressResolver().addressOf(car);
    if (address.isUnspecified())
        return "";

    // The bare address. The orchestrator strips an "acr:" prefix wherever one
    // reaches it, so either form would work; the canonical one is what the
    // decision log and the registry are keyed by.
    return address.str();
}

/*
    The shortest gap between two consecutive moves of any one car.

    This is what bounds leadTime from above: deliver a prediction further ahead
    than this and the following move for the same car arrives while the first is
    still pending, cancelling it. The car then never migrates to the host in
    between, and the run records a migration that simply did not happen rather
    than an error.
*/
simtime_t OracleTraceSource::shortestDwell() const
{
    std::map<int, simtime_t> lastMove;
    simtime_t shortest = -1;

    // Over expectedAt order, not sendAt order — they differ only by a constant
    // today, but the quantity being measured is the gap between the moves
    // themselves.
    std::vector<const TraceRow*> byTime;
    byTime.reserve(rows_.size());
    for (const auto& row : rows_)
        byTime.push_back(&row);
    std::sort(byTime.begin(), byTime.end(),
              [](const TraceRow* a, const TraceRow* b) { return a->expectedAt < b->expectedAt; });

    for (const TraceRow* row : byTime) {
        auto previous = lastMove.find(row->carIndex);
        if (previous != lastMove.end()) {
            simtime_t gap = row->expectedAt - previous->second;
            if (shortest < SIMTIME_ZERO || gap < shortest)
                shortest = gap;
        }
        lastMove[row->carIndex] = row->expectedAt;
    }

    return shortest;
}

// What the oracle arm actually delivered.
//
// The two are separate because they fail differently. Moves replayed against the
// number of rows in the trace says whether the run got through it — a run cut
// short by sim-time-limit legitimately leaves rows undelivered. Unresolved moves
// says whether the trace still described this run at all, and is the one that
// must be zero: anything else means the recorded mobility and the replayed
// mobility have diverged, which invalidates the arm rather than degrading it.
void OracleTraceSource::onRunFinished()
{
    controllerApp_->recordScalar("oracleMovesInTrace", (long)rows_.size());
    controllerApp_->recordScalar("oracleMovesReplayed", replayed_);
    controllerApp_->recordScalar("oracleMovesUnresolved", unresolved_);
}

} // namespace simu5g
