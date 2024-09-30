package simu5g.nodes.mec.VirtualisationInfrastructure;

module PacketCollector
{
    parameters:
        @display("i=block/cogwheel");
        string logFilePath = default("trash_packets.log");

    gates:  
        input in[];
}