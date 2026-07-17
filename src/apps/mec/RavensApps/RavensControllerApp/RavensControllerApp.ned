// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");
        // Run profile — selects the active outputs and the mode pushed to Agents:
        //   "History"    = HistoryOutput (CSVs), Agents in FULL mode, MEO-silent
        //   "Prediction" = HistoryOutput + MeoOutput + PredictionOutput, FULL mode
        //   "Reaction"   = MeoOutput only, Agents in EVENT_ONLY mode
        string profile = default("History");
        int staleWarningThreshold = default(10); // s of telemetry silence before a stale-user warning (diagnostic only, FULL mode)
        int snapshot_frequency = default(2);
        int snapshot_starting_time = default(2);
        string path = default("../tust_1to1/results/"); // base dir for CSVs; HistoryOutput appends <profile>/run_<N>/

        double frameInterval = default(3);         // frame interval pushed to Agents via ACK (s);
                                                   // also the Controller's F2 exit-hold window

        string localAddress = default(""); // local address; may be left empty ("")
        int dataPort = default(5001);      // UDP — event + data frames
        int mgmtPort = default(5000);      // TCP signaling — config handshake (JOIN, INFRASTRUCTURE_DETAILS), then EVENT_FRAMEs

        
    gates:
        output socketOut;
        input socketIn;

       	output outGate;
        input inGate;
}