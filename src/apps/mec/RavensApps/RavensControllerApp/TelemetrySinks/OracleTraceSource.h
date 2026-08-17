#ifndef RAVENS_CONTROLLER_APP_ORACLETRACESOURCE_H_
#define RAVENS_CONTROLLER_APP_ORACLETRACESOURCE_H_

#include "TelemetrySink.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/MigrationPrediction.h"

#include <string>
#include <vector>

namespace simu5g {

// The oracle arm's prediction stream: the moves that actually happened, read
// from a file and delivered before they happen.
//
// It stands exactly where PredictionServerClient stands and produces exactly
// what PredictionServerClient produces. The orchestrator runs
// MigrateOnPrediction either way and has no way to tell which one spoke, which
// is the whole point — the arm differs from Proactive in the quality of the
// predictions and in nothing else, so the difference between the two results is
// attributable to that alone.
//
// WHAT IT IS FOR
// An achievable ceiling. Every other arm is bounded below by what it can find
// out and when; this one is told each move with just enough warning to finish
// migrating as the user arrives. Oracle - Proactive is then the price of model
// error and Oracle - Reactive the price of the whole information plane.
//
// WHY A RECORDED TRACE IS VALID HERE
// Mobility is exogenous: cars drive their routes whatever the orchestrator does,
// and handovers follow from radio and position rather than from where an
// application happens to run. A trace taken under CollectHistory therefore
// describes every arm on the same route file and repetition. Not across
// repetitions — mobility is reproduced by seed — which is why the file path is
// built from ${repetition} rather than named by hand.
//
// WHY THE TRACE NAMES CARS AND NOT ADDRESSES
// A UE's address is addressBase + its OMNeT++ module id
// (HostAutoConfigurator.cc:52), and module ids come from one monotonic counter
// over every module ever created, MEC applications included. The orchestrator
// creates and deletes those as the run goes on, at a rate that is precisely what
// differs between arms, so the same car has different addresses in the run that
// recorded the trace and the run that replays it. The Veins car index comes from
// SUMO insertion order (nextNodeVectorIndex++) and is identical in both. So the
// file carries indices and each one is resolved to an address inside the run
// using it — late, because at initialize() no car exists yet.
//
// WHAT IT DELIBERATELY DOES NOT DO
// Entries and exits. Those still come from RAVENS, through the same Agents and
// the same confirmation window as in every other arm: this arm idealises *when
// the orchestrator learns a user is about to move*, not the whole lifecycle.
// Only cell-to-cell transitions are in the file, and the loader rejects rows
// that are not.
class OracleTraceSource : public TelemetrySink
{
    private:
        // One handover, as recorded. Held raw rather than as a
        // MigrationPrediction because the address is not yet knowable: the car
        // this refers to may not have been created at the time the trace is
        // loaded.
        struct TraceRow
        {
            int carIndex = -1;
            omnetpp::simtime_t expectedAt;   // when the handover actually happened
            omnetpp::simtime_t sendAt;       // expectedAt - leadTime
            std::string fromMEHId;
            std::string toMEHId;
        };

        // Sorted by sendAt, and walked once. An index rather than erasing from
        // the front: the whole trace is known at startup and nothing is ever
        // added, so there is no reason to move elements around.
        std::vector<TraceRow> rows_;
        size_t next_ = 0;

        double leadTime_;

        // The orchestrator's own migrationTime, read from it at startup rather
        // than configured twice. Used only to check leadTime against the trace:
        // a prediction fires leadTime - migrationTime before its move, and that
        // difference is what a following move has to stay clear of.
        double migrationTime_;

        // Counted for the run's own record; see onRunFinished() for why each one
        // has to be separable from the others.
        long replayed_ = 0;
        long unresolved_ = 0;

        // Reads the CSV into rows_ and sorts it. Every failure here is fatal:
        // a missing or malformed trace leaves the arm silently equal to a
        // reactive run, which is the one outcome that would be mistaken for a
        // result.
        void loadTrace(const std::string& traceFile);

        // The car's address in *this* run. Empty if the car does not exist yet,
        // or no longer does.
        std::string resolveCarAddress(int carIndex) const;

    protected:
        virtual void onRunFinished() override;

    public:
        OracleTraceSource(RavensControllerApp* controllerApp, const std::string& traceFile,
                          double leadTime, double migrationTime);

        // When the next row is due, or -1 when the trace is spent. The Controller
        // owns the timer — scheduling belongs to a module and a sink is not one —
        // and asks this after every release to know when to wake up again.
        omnetpp::simtime_t nextDueTime() const;

        // Publishes every row now due, one message per row.
        //
        // One per row and not one batch: MigrateOnPrediction keeps a single
        // pending entry per user, so two moves for the same car in one message
        // would leave only the second. Separate messages at separate times is
        // also what the model server produces, so the orchestrator sees the same
        // shape of traffic in both arms.
        void releaseDue();

        // The shortest gap between two consecutive moves of one car, or -1 when
        // no car moves twice. With migrationTime it sets the ceiling on leadTime:
        // a prediction fires leadTime - migrationTime before its move, so once
        // that exceeds this gap the following move arrives while the first is
        // still pending and cancels it. Checked at startup rather than left to
        // the trace script, because the value that matters is the one in the file
        // actually being replayed.
        omnetpp::simtime_t shortestDwell() const;
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_ORACLETRACESOURCE_H_ */
