#!/bin/bash
#
# Run one evaluation arm. (P1_CollectHistory has its own script.)
#
# Usage:
#   ./run_arms.sh P2_Reactive               # runs 0-49
#   ./run_arms.sh P6_ReactiveDeterministic
#   ./run_arms.sh P5_Oracle
#   ./run_arms.sh P3_Proactive
#   ./run_arms.sh P4_ProactiveNoFallback
#   ./run_arms.sh P2_Reactive 0 9           # override the range
#
# Order matters: P6 before P5 (P5 needs oracleTraces built from P6).
# Run it inside tmux. Re-running skips repetitions that already finished.

CONFIG=$1
START_RUN=${2:-0}
END_RUN=${3:-49}

MAX_JOBS=6

if [ -z "$CONFIG" ]; then
    echo "usage: $0 <config> [start] [end]"
    exit 1
fi

mkdir -p logs

# veins_launchd must be up; it forks per client and gives each run its own SUMO.
if ! pgrep -f veins_launchd > /dev/null; then
    echo "ERROR: veins_launchd is not running. Start it first:"
    echo "  ../../../../../../veins/bin/veins_launchd -c sumo -vv &"
    exit 1
fi

# A proactive run with no model server does not fail - it quietly becomes
# reactive and writes plausible, wrong results. Refuse to start instead.
case "$CONFIG" in *Proactive*)
    if ! (echo > /dev/tcp/localhost/5001) 2>/dev/null; then
        echo "ERROR: $CONFIG needs the prediction server on localhost:5001."
        echo "Nothing is listening. Without it the run silently goes reactive."
        exit 1
    fi
esac

# The oracle reads one trace per repetition, built from P6 at the same seed.
case "$CONFIG" in *Oracle*)
    for r in $(seq $START_RUN $END_RUN); do
        if [ ! -f "oracleTraces/$r.csv" ]; then
            echo "ERROR: missing oracleTraces/$r.csv"
            echo "Run P6_ReactiveDeterministic first, then build the traces."
            exit 1
        fi
    done
esac

echo "Config: $CONFIG   runs $START_RUN-$END_RUN   $MAX_JOBS at a time"

for r in $(seq $START_RUN $END_RUN); do
    # Already done? skip. Lets you re-run after an interruption.
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
