#!/bin/bash

# this script uses ./processDataFile.pl

SNDINT=$1
MAXMESSAGES=$2
PHYTYPE=$3
RBS=100

for NUM_BG_CELLS in 3 6 9;
do

    RTT_OUTPUT_FILE="rtt-multicell-backgroundUEs-gnbs=${NUM_BG_CELLS}-rbs=${RBS}-dynamicPhy=true.csv"
    
    # --- print header --- #
    printf "Background_UEs\Numerology" > $RTT_OUTPUT_FILE
    for U in 0 2 4;
    do
        printf "\t${U}\t${U}-conf" >> $RTT_OUTPUT_FILE
    done
    printf "\n" >> $RTT_OUTPUT_FILE


    for BKUES in `seq 0 3 18` ;
    do
        # --- print row name --- "
        printf "$BKUES\t" >> $RTT_OUTPUT_FILE
        
        for U in 0 2 4;
        do
            ORIG=`pwd`
            
            SUBDIR="gnbs=${NUM_BG_CELLS}-u=${U}-rbs=${RBS}-sndInt=${SNDINT}-bkUEs=${BKUES}-dynamicPhy=true-#0"
            cd $SUBDIR

            ../../scripts/processDataFile.pl -n $MAXMESSAGES -r >> $ORIG/$RTT_OUTPUT_FILE
            printf "\t" >> $ORIG/$RTT_OUTPUT_FILE 
            cd $ORIG
            
        done

        printf "\n" >> $RTT_OUTPUT_FILE
    done

done
  