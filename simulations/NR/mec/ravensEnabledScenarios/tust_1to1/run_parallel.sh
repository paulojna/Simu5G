#!/bin/bash

MAX_JOBS=3
START_RUN=0
END_RUN=29

for r in $(seq $START_RUN $END_RUN); do
    # Wait if we already have MAX_JOBS running
    while [ $(jobs -r | wc -l) -ge $MAX_JOBS ]; do
        sleep 5
    done

    echo "Starting run $r"
    opp_run -r $r -m -u Cmdenv \
        -n ../../../../../emulation:../../../..:../../../../../src:../../../../../../inet/examples:../../../../../../inet/showcases:../../../../../../inet/src:../../../../../../inet/tests/validation:../../../../../../inet/tests/networks:../../../../../../inet/tutorials:../../../../../../veins/examples/veins:../../../../../../veins/src/veins:../../../../../../veins_inet/src/veins_inet:../../../../../../veins_inet/examples/veins_inet \
        -x 'inet.applications.voipstream;inet.common.selfdoc;inet.emulation;inet.examples.emulation;inet.examples.voipstream;inet.linklayer.configurator.gatescheduling.z3;inet.showcases.emulation;inet.showcases.visualizer.osg;inet.transportlayer.tcp_lwip;inet.visualizer.osg' \
        --image-path=../../../../../images:../../../../../../inet/images:../../../../../../veins/images:../../../../../../veins_inet/images \
        -l ../../../../../src/simu5g \
        -l ../../../../../../inet/src/INET \
        -l ../../../../../../veins/src/veins \
        -l ../../../../../../veins_inet/src/veins_inet \
        -s \
        --cmdenv-redirect-output=false \
        --record-eventlog=false \
        --cmdenv-express-mode=true \
        omnetpp.ini &
done

wait
echo "All runs completed"
