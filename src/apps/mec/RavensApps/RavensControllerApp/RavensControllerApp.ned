// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");
        string mode = default("SaveDataHistory"); // available modes: "SaveDataHistory", "NotifyOnDataChange" and "SendToExternalServer"
        int threshold = default(10);
        int snapshot_frequency = default(2);  
        int snapshot_starting_time = default(2);
        string path = default("../tust_1to1/results/"); // path to save the data

        double frameInterval = default(3);         // frame interval pushed to Agents via ACK (s);
                                                   // also the Controller's F2 exit-hold window

        string localAddress = default(""); // local address; may be left empty ("")
        int dataPort = default(5001);      // UDP — event + data frames
        int mgmtPort = default(5000);      // TCP — config handshake (JOIN, INFRAESTRUCTURE_DETAILS)

        
    gates:
        output socketOut;
        input socketIn;

       	output outGate;
        input inGate;
}