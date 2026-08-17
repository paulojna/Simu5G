// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");

        // --- What the Agents are asked for -----------------------------------
        //
        // Not configured here. Whether Agents send telemetry frames is derived
        // at startup: they send exactly when something downstream consumes
        // telemetry — recordTelemetry is on, or the orchestrator's strategy
        // consumes predictions (the model server predicts from telemetry) or
        // consumes telemetry itself. Otherwise Agents run in event-only mode.
        // Events are always sent.
        //
        // Derived so that a nonsensical run — a proactive strategy whose Agents
        // send nothing for the model to predict from — cannot be written at
        // all. The `telemetry` switch that used to live here allowed exactly
        // that, and a startup check had to catch it; both are gone together.

        // --- What is recorded ------------------------------------------------
        //
        // Two switches, because the files record different kinds of thing and
        // are wanted in different runs. Both write under path/run_N/.
        //
        //   recordEvents    -> lifecycle.csv: every confirmed change in where a
        //                      user is. What a run is judged by. A handful of
        //                      rows per user for a whole run, so leaving it on
        //                      costs nothing.
        //   recordTelemetry -> users.csv and radio_stats.csv: the raw
        //                      observations, which is what models are trained
        //                      on. One row per user per second — worth
        //                      collecting once, and worth leaving off in an
        //                      experiment that would only duplicate it.
        bool recordEvents = default(true);
        bool recordTelemetry = default(true);
        string path = default("../tust_1to1/results/");

        // Where the model server lives. Only read when the orchestrator's
        // strategy consumes predictions — that is the one case with a server
        // to call, and being empty then refuses to start, since no derivation
        // can invent an address. Set with any other strategy it is ignored:
        // nothing would consume the predictions.
        string predictionServerUrl = default("");

        // How long the model server has to answer before the request is
        // abandoned (s).
        //
        // A policy, not a constant. It is the point at which a run decides a
        // slow server is a failed one, and that trade is real: too short and a
        // model that thinks hard is recorded as unreachable, too long and every
        // stall costs wall-clock on a call that blocks the whole simulation.
        // Abandoning serves this window no predictions and the run continues —
        // counted, so a run's own output shows how often it happened.
        double predictionServerTimeout = default(5);

        // How long the model takes to answer (s), as simulated time.
        //
        // The request to the model server costs no simulated time: the call
        // blocks the process, so it costs wall-clock while a run executes, but
        // the simulation clock does not move. Without this, inference is free in
        // the only clock the results are measured in — in exactly the arm whose
        // argument is that information costs something to acquire.
        //
        // Measured wall-clock is deliberately not used instead: it depends on
        // the machine and on whether the server was warm, and a run would not
        // reproduce elsewhere. Declared here, it is part of the configuration a
        // result is read against.
        //
        // Volatile, so it can be a constant or a distribution — exponential(0.08)
        // for a server under varying load — without a code change. A drawn value
        // may overtake an earlier one, and the delivery path expects that.
        //
        // Defaults to 0, which is what every run before this parameter existed
        // did. It is a value to measure and set, not one to leave.
        volatile double inferenceTime = default(0);

        // --- The oracle arm --------------------------------------------------
        //
        // A CSV of the moves that actually happened, recorded by an earlier
        // CollectHistory run and replayed here as predictions. Set, it takes the
        // place of the model server: the orchestrator consumes predictions
        // exactly as it does under Proactive, and cannot tell where they came
        // from. Empty — the normal case — and none of this exists.
        //
        // Columns: carIndex,expectedAt,fromCell,toCell. One row per handover,
        // both cells non-zero; attach and detach are left to RAVENS, which still
        // reports entries and exits in this arm as in every other.
        //
        // Keyed by car index rather than by address, and this is not a
        // convenience. A UE's IP is addressBase + its OMNeT++ module id, and
        // module ids count every module ever created — including the MEC
        // applications the orchestrator creates and deletes as the run goes on.
        // Two arms therefore give the same car different addresses. The Veins car
        // index comes from SUMO insertion order and is identical across arms, so
        // it is the only identity that survives the trip between runs.
        //
        // The file must come from the same repetition as the run replaying it;
        // build the path from ${repetition} and the pairing cannot drift.
        string oracleTraceFile = default("");

        // How far ahead of each move its prediction is delivered (s).
        //
        // Bounded on both sides. Below the orchestrator's migrationTime the
        // migration cannot finish before the user arrives, so the arm stops being
        // an upper bound and latePredictions counts every row. Above
        // migrationTime plus the shortest gap between two moves of one car, the
        // second prediction reaches MigrateOnPrediction while the first is still
        // pending and cancels it — one entry per user is all that strategy keeps
        // — and the car skips a host it should have visited.
        //
        // The default sits just above the 12s default migrationTime, which is the
        // safe end: it cannot trip the ceiling whatever the trace looks like. Read
        // the shortest dwell off the trace before raising it.
        double oracleLeadTime @unit("s") = default(13s);

        // --- Timing ----------------------------------------------------------
        //
        // How often an Agent sends telemetry frames (s). Pushed to Agents in the
        // handshake ACK — this is the only one of these the Agent ever sees.
        double telemetryInterval = default(3);

        // How much telemetry goes into one report to the orchestrator (s).
        //
        // Defaults to telemetryInterval, and that is not a coincidence: each
        // Agent sends exactly one frame per interval, so a window of the same
        // length holds one complete round of observations however the Agents'
        // schedules are offset from each other. Widen it if a consumer wants a
        // slower step; there is no reason to narrow it.
        //
        // This is the only thing on this side that waits for a tick. Confirmed
        // user events and predictions are sent the moment they exist.
        double telemetryWindow = default(telemetryInterval);

        // How long the Controller waits, after an Agent reports a UE leaving,
        // before declaring it gone from the system (s). Never transmitted.
        // Sized by how far apart two hosts notice the same handover, so that an
        // EXIT from the old host and an ENTRY from the new one are recognised as
        // one movement rather than a departure. Unrelated to telemetryInterval.
        double exitConfirmationWindow = default(3);

        // How long telemetry may say nothing about a user before that silence is
        // reported (s). Diagnostic only: nothing is removed on this timer, since
        // users leave the Controller's model through the event channel alone.
        // Not applied when telemetry is off, where silence is the normal state.
        int silenceWarningThreshold = default(60);

        // --- Transport -------------------------------------------------------
        string localAddress = default(""); // local address; may be left empty ("")
        int dataPort = default(5001);      // UDP — telemetry frames
        int mgmtPort = default(5000);      // TCP signaling — config handshake (JOIN, INFRASTRUCTURE_DETAILS), then EVENT_FRAMEs

    gates:
        output socketOut;
        input socketIn;

        // One way. The orchestrator has never sent anything back, and the return
        // gate that used to be declared here had no branch in handleMessageWhenUp
        // to receive it — anything arriving would have fallen through to the drop
        // case.
        output outGate;
}
