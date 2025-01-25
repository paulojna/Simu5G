package simu5g.simulations.NR.mec.ravensEnabledScenarios.tust_1to1;

import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.networklayer.ipv4.RoutingTableRecorder;
import inet.node.ethernet.Eth10G;
import inet.node.inet.Router;
import inet.node.inet.StandardHost;
import simu5g.nodes.mec.RAVENSController.RavensController;
import simu5g.common.binder.Binder;
import simu5g.common.carrierAggregation.CarrierAggregation;
import simu5g.nodes.Upf;
import simu5g.nodes.mec.MECHost;
import simu5g.nodes.mec.MECOrchestrator.MecOrchestrator;
import simu5g.nodes.mec.UALCMP.UALCMP;
import simu5g.nodes.NR.gNodeB;
import simu5g.nodes.NR.NRUe;
import simu5g.nodes.cars.NRCar;
import simu5g.nodes.backgroundCell.BackgroundCell;
import simu5g.world.radio.LteChannelControl;

import org.car2x.veins.subprojects.veins_inet.VeinsInetManager;

network tust_1to1
{
    parameters:
        int numUes = default(1);
        int numBgCells = default(1);

    submodules:
        // Simu5G modules

        channelControl: LteChannelControl {
            @display("p=200,100;is=s");
        }
        routingRecorder: RoutingTableRecorder {
            @display("p=200,110;is=s");
        }
        configurator: Ipv4NetworkConfigurator {
            @display("p=200,120;is=s");
        }
        binder: Binder {
            @display("p=200,130;is=s");
        }
        veinsManager: VeinsInetManager {
            @display("p=200,140;is=s");
        }
        carrierAggregation: CarrierAggregation {
            @display("p=200,150;is=s");
        }

        // Configuration for 6 gNodeBs
        gnb1: gNodeB {
            @display("p=212,118");
        }
        gnb2: gNodeB {
            @display("p=638,118");
        }
        gnb3: gNodeB {
            @display("p=1063,118");
        }
        gnb4: gNodeB {
            @display("p=10,586");
        }
        gnb5: gNodeB {
            @display("p=425,586");
        }
        gnb6: gNodeB {
            @display("p=850,586");
        }
        gnb7: gNodeB {
            @display("p=1266,586");
        }
        gnb8: gNodeB {
            @display("p=212,1055");
        }
        gnb9: gNodeB {
            @display("p=638,1055");
        }
        gnb10: gNodeB {
            @display("p=1063,1055");
        }

        // Router and Host (for RAVENS Controller)
        router: Router {
            @display("p=550,550");
        }
        server: RavensController {
            @display("p=550,600;is=s");
        }

        upf: Upf {
            @display("p=500,500;is=s");
        }
        iUpf1: Upf {
            @display("p=212,118;is=s");
        }
        iUpf2: Upf {
            @display("p=638,118;is=s");
        }
        iUpf3: Upf {
            @display("p=1063,118;is=s");
        }
        iUpf4: Upf {
            @display("p=10,586;is=s");
        }
        iUpf5: Upf {
            @display("p=425,586;is=s");
        }
        iUpf6: Upf {
            @display("p=850,586;is=s");
        }
        iUpf7: Upf {
            @display("p=1266,586;is=s");
        }
        iUpf8: Upf {
            @display("p=212,1055;is=s");
        }
        iUpf9: Upf {
            @display("p=638,1055;is=s");
        }
        iUpf10: Upf {
            @display("p=1063,1055;is=s");
        }     

        // MEC Hosts
        mecHost1: MECHost {
            @display("p=212,118;is=s");
        }
        mecHost2: MECHost {
            @display("p=638,118;is=s");
        }
        mecHost3: MECHost {
            @display("p=1063,118;is=s");
        }
        mecHost4: MECHost {
            @display("p=10,586;is=s");
        }
        mecHost5: MECHost {
            @display("p=425,586;is=s");
        }
        mecHost6: MECHost {
            @display("p=850,586;is=s");
        }
        mecHost7: MECHost {
            @display("p=1266,586;is=s");
        }
        mecHost8: MECHost {
            @display("p=212,1055;is=s");
        }
        mecHost9: MECHost {
            @display("p=638,1055;is=s");
        }
        mecHost10: MECHost {
            @display("p=1063,1055;is=s");
        }

        mecOrchestrator: MecOrchestrator {
            @display("p=550,720;is=s");
        }
        ualcmp: UALCMP {
            @display("p=550,740;is=s");
        }

        car[0]: NRCar {
            @display("p=20,20;is=s");
        }
        
	connections: 

        mecHost1.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf1.pppg++;
        mecHost2.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf2.pppg++;
        mecHost3.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf3.pppg++;
        mecHost4.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf4.pppg++;
        mecHost5.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf5.pppg++;
        mecHost6.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf6.pppg++;
        mecHost7.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf7.pppg++;
        mecHost8.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf8.pppg++;
        mecHost9.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf9.pppg++;
        mecHost10.ppp++ <--> Eth10G {  delay = 0.001s; } <--> iUpf10.pppg++;

        iUpf1.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf2.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf3.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf4.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf5.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf6.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf7.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf8.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf9.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;
        iUpf10.pppg++ <--> Eth10G {  delay = 0.003s; } <--> upf.pppg++;

        iUpf1.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb1.ppp;
        iUpf2.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb2.ppp;
        iUpf3.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb3.ppp;
        iUpf4.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb4.ppp;
        iUpf5.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb5.ppp;
        iUpf6.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb6.ppp;
        iUpf7.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb7.ppp;
        iUpf8.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb8.ppp;
        iUpf9.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb9.ppp;
        iUpf10.pppg++ <--> Eth10G {  delay = 0.001s; } <--> gnb10.ppp;

        mecHost1.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost2.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost3.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost4.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost5.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost6.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost7.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost8.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost9.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;
        mecHost10.ppp++ <--> Eth10G {  delay = 0.004s; } <--> upf.pppg++;

        router.pppg++ <--> Eth10G <--> upf.filterGate;

        ualcmp.ppp++ <--> Eth10G <--> router.pppg++;

        ualcmp.toMecOrchestrator --> mecOrchestrator.fromUALCMP;
        ualcmp.fromMecOrchestrator <-- mecOrchestrator.toUALCMP;

        // RAVENS
        server.ppp++ <--> Eth10G <--> router.pppg++;

        server.hostOutGate --> mecOrchestrator.fromRavensController;
        server.hostInGate <-- mecOrchestrator.toRavensController;

        // X2 connections
        gnb1.x2++ <--> Eth10G <--> gnb2.x2++;
        gnb2.x2++ <--> Eth10G <--> gnb4.x2++;
        gnb4.x2++ <--> Eth10G <--> gnb5.x2++;

        gnb2.x2++ <--> Eth10G <--> gnb3.x2++;
        gnb2.x2++ <--> Eth10G <--> gnb6.x2++;
        gnb2.x2++ <--> Eth10G <--> gnb5.x2++;

        gnb3.x2++ <--> Eth10G <--> gnb6.x2++;
        gnb3.x2++ <--> Eth10G <--> gnb7.x2++;

        gnb5.x2++ <--> Eth10G <--> gnb6.x2++;

        gnb4.x2++ <--> Eth10G <--> gnb8.x2++;
        gnb4.x2++ <--> Eth10G <--> gnb5.x2++;

        gnb7.x2++ <--> Eth10G <--> gnb6.x2++;
        gnb7.x2++ <--> Eth10G <--> gnb10.x2++;

        gnb8.x2++ <--> Eth10G <--> gnb9.x2++;
        gnb8.x2++ <--> Eth10G <--> gnb5.x2++;

        gnb9.x2++ <--> Eth10G <--> gnb5.x2++;
        gnb9.x2++ <--> Eth10G <--> gnb10.x2++;
        gnb9.x2++ <--> Eth10G <--> gnb6.x2++;

        gnb10.x2++ <--> Eth10G <--> gnb6.x2++;
}