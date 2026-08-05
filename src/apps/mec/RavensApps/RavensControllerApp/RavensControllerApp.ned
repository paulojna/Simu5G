// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");
        string mode = default("SaveDataHistory"); // available modes: "SaveDataHistory", "NotifyOnDataChange" and "SendToExternalServer"
        // How long telemetry may say nothing about a user before that silence is
        // reported (s). Diagnostic only: nothing is removed on this timer, since
        // users leave the Controller's model through the event channel alone.
        // Not applied in event-only mode, where silence is the normal state.
        int silenceWarningThreshold = default(60);
        int snapshot_frequency = default(2);  
        int snapshot_starting_time = default(2);
        string path = default("../tust_1to1/results/"); // path to save the data

        // How often an Agent sends telemetry frames (s). Pushed to Agents in the
        // handshake ACK — this is the only one of the two the Agent ever sees.
        double telemetryInterval = default(3);

        // How long the Controller waits, after an Agent reports a UE leaving,
        // before declaring it gone from the system (s). Never transmitted.
        // Sized by how far apart two hosts notice the same handover, so that an
        // EXIT from the old host and an ENTRY from the new one are recognised as
        // one movement rather than a departure. Unrelated to telemetryInterval.
        double exitConfirmationWindow = default(3);

        string localAddress = default(""); // local address; may be left empty ("")
        int dataPort = default(5001);      // UDP — event + data frames
        int mgmtPort = default(5000);      // TCP signaling — config handshake (JOIN, INFRASTRUCTURE_DETAILS), then EVENT_FRAMEs

        
    gates:
        output socketOut;
        input socketIn;

       	output outGate;
        input inGate;
}