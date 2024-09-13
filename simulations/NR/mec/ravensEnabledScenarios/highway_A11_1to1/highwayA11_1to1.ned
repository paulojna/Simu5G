package simu5g.simulations.NR.mec.ravensEnabledScenarios.highway_A11_1to1;

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

network highwayA11_1to1
{
    parameters:
        int numUes = default(1);
        int numBgCells = default(1);

    submodules:
        // Simu5G modules

        channelControl: LteChannelControl {
            @display("p=200,1000;is=s");
        }
        routingRecorder: RoutingTableRecorder {
            @display("p=200,1500;is=s");
        }
        configurator: Ipv4NetworkConfigurator {
            @display("p=200,2000");
        }
        binder: Binder {
            @display("p=200,2500;is=s");
        }
        veinsManager: VeinsInetManager {
            @display("p=200,3000;is=s");
        }
        carrierAggregation: CarrierAggregation {
            @display("p=200,3500;is=s");
        }

        // Configuration for 6 gNodeBs
        gnb0: gNodeB {
            @display("p=307.88,214.09");
        }
        gnb1: gNodeB {
            @display("p=1949.89,1355.93");
        }
        gnb2: gNodeB {
            @display("p=3591.91,2497.77");
        }
        gnb3: gNodeB {
            @display("p=5233.92,3639.60");
        }
        gnb4: gNodeB {
            @display("p=6875.93,4781.44");
        }
        gnb5: gNodeB {
            @display("p=8517.95,5923.28");
        }

        // Router and Host (for RAVENS Controller)
        router: Router {
            @display("p=6299.95,997.7013;is=s");
        }
        server: RavensController {
            @display("p=7467.0723,865.9294;is=s");
        }

        // UPF and iUPFs
        upf: Upf {
            @display("p=5853.6826,1261.9352;is=s");
        }
        iUpf0: Upf {
            @display("p=307.88,244.09");
        }
        iUpf1: Upf {
            @display("p=1949.89,1405.93");
        }
        iUpf2: Upf {
            @display("p=3591.91,2547.77");
        }
        iUpf3: Upf {
            @display("p=5233.92,3689.60");
        }
        iUpf4: Upf {
            @display("p=6875.93,4831.44");
        }
        iUpf5: Upf {
            @display("p=8517.95,5973.28");
        }

        // MEC Hosts
        mecHost0: MECHost {
            @display("p=357.88,214.09");
        }
        mecHost1: MECHost {
            @display("p=1999.89,1355.93");
        }
        mecHost2: MECHost {
            @display("p=3641.91,2497.77");
        }
        mecHost3: MECHost {
            @display("p=5283.92,3639.60");
        }
        mecHost4: MECHost {
            @display("p=6925.93,4781.44");
        }
        mecHost5: MECHost {
            @display("p=8567.95,5923.28");
        }


        // MEC Orchestrator and UALCMP
        mecOrchestrator: MecOrchestrator {
            @display("p=7887.4873,1844.8062;is=s");
        }
        ualcmp: UALCMP {
            @display("p=6908.611,2045.6013;is=s");
        }

        car[0]: NRCar {
            @display("p=4326.635,1831.441;is=s");
        }

    connections:

        // 5G Core Network connections
        //iUpf0.pppg++ <--> Eth10G <--> iUpf1.pppg++;
        //mecHost0.ppp++ <--> Eth10G <--> centralUpf.pppg++;
        //mecHost1.ppp++ <--> Eth10G <--> centralUpf.pppg++;

        mecHost0.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf0.pppg++;
        mecHost1.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf1.pppg++;
        mecHost2.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf2.pppg++;
        mecHost3.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf3.pppg++;
        mecHost4.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf4.pppg++;
        mecHost5.ppp++ <--> Eth10G { delay=0.002s; } <--> iUpf5.pppg++;

        //mecHost0.ppp++ <--> Eth10G {  delay=0.002s; } <--> upf.pppg++;
        iUpf0.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;
        iUpf1.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;
        iUpf2.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;
        iUpf3.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;
        iUpf4.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;
        iUpf5.pppg++ <--> Eth10G { delay=0.002s; } <--> upf.pppg++;

        iUpf0.pppg++ <--> Eth10G <--> gnb0.ppp;
        iUpf1.pppg++ <--> Eth10G <--> gnb1.ppp;
        iUpf2.pppg++ <--> Eth10G <--> gnb2.ppp;
        iUpf3.pppg++ <--> Eth10G <--> gnb3.ppp;
        iUpf4.pppg++ <--> Eth10G <--> gnb4.ppp;
        iUpf5.pppg++ <--> Eth10G <--> gnb5.ppp;


        //mecHost0.ppp++ <--> Eth10G <--> gnb0.ppp;
        //mecHost0.ppp++ <--> Eth10G <--> gnb1.ppp;
        //mecHost0.ppp++ <--> Eth10G <--> gnb2.ppp;

        //mecHost1.ppp++ <--> Eth10G <--> gnb3.ppp;
        //mecHost1.ppp++ <--> Eth10G <--> gnb4.ppp;
        //mecHost1.ppp++ <--> Eth10G <--> gnb5.ppp;

        //mecHost0.ppp++ <--> Eth10G <--> upf.pppg++;
        //mecHost1.ppp++ <--> Eth10G <--> upf.pppg++;

        //mecHost0.ppp++ <--> Eth10G <--> mecHost1.ppp++;

        // MEC-related connections
        router.pppg++ <--> Eth10G <--> upf.filterGate;

        ualcmp.ppp++ <--> Eth10G <--> router.pppg++;

        ualcmp.toMecOrchestrator --> mecOrchestrator.fromUALCMP;
        ualcmp.fromMecOrchestrator <-- mecOrchestrator.toUALCMP;

        // RAVENS
        server.ppp++ <--> Eth10G <--> router.pppg++;

        server.hostOutGate --> mecOrchestrator.fromRavensController;
        server.hostInGate <-- mecOrchestrator.toRavensController;

        // X2 connections
        gnb0.x2++ <--> Eth10G <--> gnb1.x2++;
        gnb1.x2++ <--> Eth10G <--> gnb2.x2++;
        gnb2.x2++ <--> Eth10G <--> gnb3.x2++;
        gnb3.x2++ <--> Eth10G <--> gnb4.x2++;
        gnb4.x2++ <--> Eth10G <--> gnb5.x2++;
}
