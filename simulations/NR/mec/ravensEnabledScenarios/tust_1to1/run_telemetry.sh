#!/bin/bash
#
# Usage:
#   ./run_telemetry.sh                          # P1_CollectHistory, runs 0-199
#   ./run_telemetry.sh T1_CollectTelemetry 0 0  # smoke test: one run first
#   ./run_telemetry.sh P1_CollectHistory 0 199
#
# Run it inside tmux. Re-running skips repetitions that already finished.

CONFIG=${1:-P1_CollectHistory}
START_RUN=${2:-0}
END_RUN=${3:-199}

# Each run spawns its own SUMO through veins_launchd, so one job costs
# roughly two cores. 6 jobs on 16 cores. Raise it only after watching `top`.
MAX_JOBS=6

mkdir -p logs

# veins_launchd must be up; it forks per client and gives each run its own SUMO.
if ! pgrep -f veins_launchd > /dev/null; then
    echo "ERROR: veins_launchd is not running. Start it first:"
    echo "  ../../../../../../veins/bin/veins_launchd -c sumo -vv &"
    exit 1
fi

echo "Config: $CONFIG   runs $START_RUN-$END_RUN   $MAX_JOBS at a time"

for r in $(seq $START_RUN $END_RUN); do
    # Already done? skip. Lets you re-run after an interruption.
    # omnetpp.ini writes results/<config>/<iterationvars>-<rep>.sca, and
    # iterationvars is literally $0="fair" - hence the wildcard.
    if ls results/"$CONFIG"/*-"$r".sca > /dev/null 2>&1; then
        echo "Run $r already complete, skipping"
        continue
    fi

    while [ $(jobs -r | wc -l) -ge $MAX_JOBS ]; do
        sleep 5
    done

    echo "Starting run $r"
    opp_run -r $r -c $CONFIG -m -u Cmdenv \
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
        omnetpp.ini > logs/${CONFIG}_$r.log 2>&1 &
done

wait

DONE=$(ls results/"$CONFIG"/*.sca 2>/dev/null | wc -l)
echo "Finished: $DONE runs have results in results/$CONFIG/"
