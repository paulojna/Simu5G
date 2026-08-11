// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");

        // --- What the Agents are asked for -----------------------------------
        //
        // The expensive switch, and the only one that changes the simulated
        // network rather than just what the Controller does afterwards: with
        // this off the Agents send no telemetry frames at all, so the run
        // genuinely carries less signaling traffic. Events are always sent.
        //
        // These three settings replace a single `mode` string that decided all
        // of them at once, and so could not express combinations that are
        // perfectly reasonable — recording a run *and* letting a model drive
        // migrations being the obvious one.
        bool telemetry = default(true);

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

        // Empty means no model server.
        string predictionServerUrl = default("");

        // Note: recordTelemetry or predictionServerUrl with telemetry = false
        // refuses to start. Both consume telemetry, so in a run that collects
        // none they would never be called, leaving a dead setting and an empty
        // file with nothing to explain either.

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
