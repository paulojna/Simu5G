// this will be a model that is similar to the UALCMP model but without the direct connection to the orchestrator

package simu5g.apps.mec.RavensApps.RavensControllerApp;

import inet.applications.contract.IApp;

simple RavensControllerApp like IApp
{
    parameters:
        @class("RavensControllerApp");
        string mode = default("SaveDataHistory"); // available modes: "NotifyOnDataChange" and "SaveDataHistory"
        int treshold = default(30);
        int snapshot_frequency = default(2); 
        int snapshot_starting_time = default(2);
        string path = default("../highway_A11_1to1/results/"); // path to save the data
        
        int bufferTime = default(3); // timeout for the handler to know if the data is to be sent or not 

        string localAddress = default(""); // local address; may be left empty ("")
        int localPort = default(5001);     // localPort number to listen on    

        
    gates:
        output socketOut;
        input socketIn;

       	output outGate;
        input inGate;
}