package simu5g.apps.mec.EdgePerfApp;

import inet.applications.contract.IApp;

//
//  This UE App continuously measures the RTT between the UE and a MEC App. 
//  It also supports the Migration between MEH hosts. The goal is to get the following values:
//  - RTT between the UE and the MEC App (+ Processing Time, Service Response Time, Uplink Time, Downlink Time)
//  - Number of lost packets
//  - Number of migrations
//  - Path between MEHs (MEH ID through time)
//  - Migration time (in collaboration with the MEO - that is the entity that starts the process)
//

simple UEPerfApp like IApp
{
    parameters:       
         @display("i=block/source");    
        
        //connection infos
        int localPort = default(4000);
        int deviceAppPort = default(4000); // ipAddress of the DeviceApp
        string deviceAppAddress; // port of the DeviceApp
                
        int requestPacketSize = default(10);
        
        int tos = default(-1); // if not -1, set the Type Of Service (IPv4) / Traffic Class (IPv6) field of sent packets to this value
        
        //autoscheduling infos
        double period @unit("s") = default(0.5s);
        double timeout @unit("s") = default(1s);
        double startTime @unit("s") = default(0s); 
        double stopTime @unit("s") = default(120s);
                
        string mecAppName = default("MECPerfApp");

        string controllerAddress = default("");
        int controllerPort = default(5001);
        
        string interfaceTableModule;
        
        // stats
                     
        @signal[responseTime];
        @statistic[responseTime](title="responseTime"; unit="s"; source="responseTime"; record=stats, mean, vector); 
             
        @signal[processingTime];
        @statistic[processingTime](title="processingTime"; unit="s"; source="processingTime"; record=stats, mean, vector); 
       
        @signal[serviceResponseTime];
        @statistic[serviceResponseTime](title="serviceResponseTime"; unit="s"; source="serviceResponseTime"; record=stats, mean, vector); 
        
        @signal[upLinkTime];
        @statistic[upLinkTime](title="upLinkTime"; unit="s"; source="upLinkTime"; record=stats, mean, vector); 
        
        @signal[downLinkTime];
        @statistic[downLinkTime](title="downLinkTime"; unit="s"; source="downLinkTime"; record=stats, mean, vector); 
              
        @signal[mecHostId];
        @statistic[mecHostId](title="mecHostId"; source="mecHostId"; record=stats, vector);

        @signal[lostMessages];
        @statistic[lostMessages](title="lostMessages"; source="lostMessages"; record=stats, mean, vector);
             
    gates:
        output socketOut;
        input socketIn;
}