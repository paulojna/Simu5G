//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package simu5g.apps.mec.RavensApps.RavensAgentApp;

import inet.applications.contract.IApp;
import simu5g.nodes.mec.MECPlatform.IMECApp;

simple RavensAgentApp like IMECApp, IApp
{
	parameters:
	    @display("i=block/app;is=s");
  			
        int packetSize = default(10);
        string interfaceTableModule = default("");
        
        int mecAppIndex = default(0); 
        int mecAppId = default(0); // it will be override by getId() in initialize
        int localUePort = default(0); // not used 
        
        string mp1Address = default("");
        int mp1Port = default(10022);
        
        //resource required info
        double requiredRam @unit("B");
        double requiredDisk @unit("B");
        double requiredCpu;
        
        // IApp parameters
		int timeToLive = default(-1); // if not -1, set the TTL (IPv4) or Hop Limit (IPv6) field of sent packets to this value
        int dscp = default(-1); // if not -1, set the DSCP (IPv4/IPv6) field of sent packets to this value
        int tos = default(-1); // if not -1, set the Type Of Service (IPv4) / Traffic Class (IPv6) field of sent packets to this value
		
		bool logger = default(false);

        string controllerAddress = default("");
        int controllerDataPort = default(5001); // UDP — event + data frames
        int controllerMgmtPort = default(5000); // TCP signaling — config handshake (JOIN, INFRASTRUCTURE_DETAILS), then EVENT_FRAMEs

        int localPort = default(1993);

        // Pre-negotiation default only. The Controller sends the real value in the
        // handshake ACK, which always arrives before the first frame is sent, so
        // this value is never actually used to time anything. It exists so the
        // member is never uninitialised.
        double telemetryInterval @unit(s) = default(3s);

        // Consecutive present LS samples (1s each) required before an ENTRY is
        // reported to the Controller. Symmetric with exitConfirmSamples by design —
        // see ravens-plan.md, "Event reporting", for why an asymmetric threshold
        // is unsafe.
        int entryConfirmSamples = default(2);
        // Consecutive absent LS samples (1s each) required before an EXIT is
        // reported to the Controller.
        int exitConfirmSamples = default(2);

	gates:
	 	input mePlatformIn;
	  	output mePlatformOut;
	  
	  	input socketIn;
	  	output socketOut;
        
}
