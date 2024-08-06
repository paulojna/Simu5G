package simu5g.nodes.mec.RAVENSController;

import simu5g.apps.mec.RavensApps.RavensControllerApp.RavensControllerApp;
import inet.node.inet.StandardHost;

import inet.linklayer.ppp.PppInterface;
import inet.networklayer.common.InterfaceTable;
import inet.networklayer.ipv4.Ipv4RoutingTable;
import inet.networklayer.contract.IRoutingTable;
import inet.networklayer.contract.INetworkLayer;
import inet.applications.contract.IApp;
import inet.transportlayer.udp.Udp;
import inet.transportlayer.tcp.Tcp;
import inet.common.MessageDispatcher;
import inet.linklayer.contract.IEthernetInterface;
import inet.applications.contract.IApp;
import inet.node.ethernet.Eth10G;
import inet.networklayer.common.InterfaceTable;

module RavensController
{
    parameters:
        @networkNode();
        @display("i=device/server");

        bool hasIpv4 = default(true);
        bool hasIpv6 = default(false);
        *.interfaceTableModule = default(absPath(".interfaceTable"));

        int numExtEthInterfaces = default(0); //

    gates:
        inout ppp[];

        output hostOutGate;
        input hostInGate;

    submodules:

        interfaceTable: InterfaceTable {
            @display("p=61.344,74.976;is=s");
        }

        pppIf: PppInterface {
            @display("p=446.832,506.688");
        }

        ipv4: <default("Ipv4NetworkLayer")> like INetworkLayer if hasIpv4 {
            parameters:
                @display("p=420.28,372.88;q=queue");
        }
        ipv6: <default("Ipv6NetworkLayer")> like INetworkLayer if hasIpv6 {
            parameters:
                @display("p=657.28,374.46;q=queue");
        }

        ravensControllerApp : RavensControllerApp {
            @display("p=61.344,74.976;is=s");
        }
        
        udp: Udp {
            @display("p=459,217");
        }

        tcp: Tcp {
            @display("p=627,217");
        }

        // message dispatcher for SAP between application and transport layer
        at: MessageDispatcher {
            parameters:
                @display("p=550,146;b=600,5,,,,1");
        }
        // message dispatcher for SAP between transport and network layer
        tn: MessageDispatcher {
            parameters:
                @display("p=550,296;b=600,5,,,,1");
        }
        // message dispatcher for SAP to link layer
        nl: MessageDispatcher {
            parameters:
                @display("p=550,446;b=600,5,,,,1");
        }

    connections allowunconnected:

        ravensControllerApp.socketOut --> at.in++;
        ravensControllerApp.socketIn <-- at.out++;
        
        ravensControllerApp.outGate --> hostOutGate;
        ravensControllerApp.inGate <-- hostInGate;

        at.out++ --> udp.appIn;
        at.in++ <-- udp.appOut;

        at.out++ --> tcp.appIn;
        at.in++ <-- tcp.appOut;

        udp.ipOut --> tn.in++;
        udp.ipIn <-- tn.out++;

        tcp.ipOut --> tn.in++;
        tcp.ipIn <-- tn.out++;


        ipv4.ifIn <-- nl.out++ if hasIpv4;
        ipv4.ifOut --> nl.in++ if hasIpv4;

        ipv4.transportIn <-- tn.out++ if hasIpv4;
        ipv4.transportOut --> tn.in++ if hasIpv4;

        ipv6.ifIn <-- nl.out++ if hasIpv6;
        ipv6.ifOut --> nl.in++ if hasIpv6;

        ipv6.transportIn <-- tn.out++ if hasIpv6;
        ipv6.transportOut --> tn.in++ if hasIpv6;

        pppIf.upperLayerOut --> nl.in++;
        pppIf.upperLayerIn <-- nl.out++;
        pppIf.phys <--> ppp++;
}