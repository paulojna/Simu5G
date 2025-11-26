# Network Topology for tust_1to1 Scenario

This document outlines the network connections for the `tust_1to1` simulation scenario, based on the `manual_config.xml` file.

## Core Router Connections

The `core_router` connects to all the `iUpf`s and the main `upf`.

*   `core_router` <-> `iUpf1`: `192.168.42.0/24`
*   `core_router` <-> `iUpf2`: `192.168.43.0/24`
*   `core_router` <-> `iUpf3`: `192.168.44.0/24`
*   `core_router` <-> `iUpf4`: `192.168.45.0/24`
*   `core_router` <-> `iUpf5`: `192.168.46.0/24`
*   `core_router` <-> `iUpf6`: `192.168.47.0/24`
*   `core_router` <-> `iUpf7`: `192.168.48.0/24`
*   `core_router` <-> `iUpf8`: `192.168.49.0/24`
*   `core_router` <-> `iUpf9`: `192.168.50.0/24`
*   `core_router` <-> `iUpf10`: `192.168.51.0/24`
*   `core_router` <-> `upf`: `192.168.52.0/24`

## Other Key Networks

*   **Central Services Network**: A central `router` connects to the main `upf`, the `ualcmp`, and the main `server`.
    *   `router` <-> `upf`: `192.168.39.0/24`
    *   `router` <-> `ualcmp`: `192.168.40.0/24`
    *   `router` <-> `server`: `192.168.41.0/24`

*   **Central MEC Host Network**: The main `upf` connects to a central MEC host, `mecHost11`.
    *   `upf` <-> `mecHost11`: `192.168.53.0/24`

*   **Cellular Network (`10.0.0.0/8`)**: This is the main network for communication between the gNodeBs themselves and for the UEs.

---

## Edge Site Details

### Edge Site 1
*   **Components**: `gNodeB1`, `iUpf1`, `mecHost1`
*   **Networks**:
    *   `192.168.0.0/24`: Connects `gNodeB1` and `iUpf1`.
    *   `192.168.42.0/24`: Connects `iUpf1` to the `core_router`.
    *   `192.168.54.0/24`: Connects `iUpf1` to `mecHost1`'s `upf_mec`.
    *   `192.168.74.0/24`: Internal network within `mecHost1` between `upf_mec` and `virtualisationInfrastructure`.
*   **gNodeB Connections**:
    *   `gNodeB1` <-> `gNodeB2`: `192.168.1.0/24`
    *   `gNodeB1` <-> `gNodeB4`: `192.168.2.0/24`
    *   `gNodeB1` <-> `gNodeB5`: `192.168.3.0/24`

### Edge Site 2
*   **Components**: `gNodeB2`, `iUpf2`, `mecHost2`
*   **Networks**:
    *   `192.168.5.0/24`: Connects `gNodeB2` and `iUpf2`.
    *   `192.168.43.0/24`: Connects `iUpf2` to the `core_router`.
    *   `192.168.56.0/24`: Connects `iUpf2` to `mecHost2`'s `upf_mec`.
    *   `192.168.75.0/24`: Internal network within `mecHost2`.
*   **gNodeB Connections**:
    *   `gNodeB2` <-> `gNodeB1`: `192.168.1.0/24`
    *   `gNodeB2` <-> `gNodeB3`: `192.168.6.0/24`
    *   `gNodeB2` <-> `gNodeB6`: `192.168.7.0/24`
    *   `gNodeB2` <-> `gNodeB5`: `192.168.8.0/24`

### Edge Site 3
*   **Components**: `gNodeB3`, `iUpf3`, `mecHost3`
*   **Networks**:
    *   `192.168.10.0/24`: Connects `gNodeB3` and `iUpf3`.
    *   `192.168.44.0/24`: Connects `iUpf3` to the `core_router`.
    *   `192.168.58.0/24`: Connects `iUpf3` to `mecHost3`'s `upf_mec`.
    *   `192.168.76.0/24`: Internal network within `mecHost3`.
*   **gNodeB Connections**:
    *   `gNodeB3` <-> `gNodeB2`: `192.168.6.0/24`
    *   `gNodeB3` <-> `gNodeB6`: `192.168.11.0/24`
    *   `gNodeB3` <-> `gNodeB7`: `192.168.12.0/24`

### Edge Site 4
*   **Components**: `gNodeB4`, `iUpf4`, `mecHost4`
*   **Networks**:
    *   `192.168.14.0/24`: Connects `gNodeB4` and `iUpf4`.
    *   `192.168.45.0/24`: Connects `iUpf4` to the `core_router`.
    *   `192.168.60.0/24`: Connects `iUpf4` to `mecHost4`'s `upf_mec`.
    *   `192.168.77.0/24`: Internal network within `mecHost4`.
*   **gNodeB Connections**:
    *   `gNodeB4` <-> `gNodeB1`: `192.168.2.0/24`
    *   `gNodeB4` <-> `gNodeB8`: `192.168.15.0/24`
    *   `gNodeB4` <-> `gNodeB5`: `192.168.16.0/24`

### Edge Site 5
*   **Components**: `gNodeB5`, `iUpf5`, `mecHost5`
*   **Networks**:
    *   `192.168.18.0/24`: Connects `gNodeB5` and `iUpf5`.
    *   `192.168.46.0/24`: Connects `iUpf5` to the `core_router`.
    *   `192.168.62.0/24`: Connects `iUpf5` to `mecHost5`'s `upf_mec`.
    *   `192.168.78.0/24`: Internal network within `mecHost5`.
*   **gNodeB Connections**:
    *   `gNodeB5` <-> `gNodeB1`: `192.168.3.0/24`
    *   `gNodeB5` <-> `gNodeB2`: `192.168.8.0/24`
    *   `gNodeB5` <-> `gNodeB4`: `192.168.16.0/24`
    *   `gNodeB5` <-> `gNodeB6`: `192.168.19.0/24`
    *   `gNodeB5` <-> `gNodeB8`: `192.168.20.0/24`
    *   `gNodeB5` <-> `gNodeB9`: `192.168.21.0/24`

### Edge Site 6
*   **Components**: `gNodeB6`, `iUpf6`, `mecHost6`
*   **Networks**:
    *   `192.168.23.0/24`: Connects `gNodeB6` and `iUpf6`.
    *   `192.168.47.0/24`: Connects `iUpf6` to the `core_router`.
    *   `192.168.64.0/24`: Connects `iUpf6` to `mecHost6`'s `upf_mec`.
    *   `192.168.79.0/24`: Internal network within `mecHost6`.
*   **gNodeB Connections**:
    *   `gNodeB6` <-> `gNodeB2`: `192.168.7.0/24`
    *   `gNodeB6` <-> `gNodeB3`: `192.168.11.0/24`
    *   `gNodeB6` <-> `gNodeB5`: `192.168.19.0/24`
    *   `gNodeB6` <-> `gNodeB7`: `192.168.24.0/24`
    *   `gNodeB6` <-> `gNodeB9`: `192.168.25.0/24`
    *   `gNodeB6` <-> `gNodeB10`: `192.168.26.0/24`

### Edge Site 7
*   **Components**: `gNodeB7`, `iUpf7`, `mecHost7`
*   **Networks**:
    *   `192.168.28.0/24`: Connects `gNodeB7` and `iUpf7`.
    *   `192.168.48.0/24`: Connects `iUpf7` to the `core_router`.
    *   `192.168.66.0/24`: Connects `iUpf7` to `mecHost7`'s `upf_mec`.
    *   `192.168.80.0/24`: Internal network within `mecHost7`.
*   **gNodeB Connections**:
    *   `gNodeB7` <-> `gNodeB3`: `192.168.12.0/24`
    *   `gNodeB7` <-> `gNodeB6`: `192.168.24.0/24`
    *   `gNodeB7` <-> `gNodeB10`: `192.168.29.0/24`

### Edge Site 8
*   **Components**: `gNodeB8`, `iUpf8`, `mecHost8`
*   **Networks**:
    *   `192.168.31.0/24`: Connects `gNodeB8` and `iUpf8`.
    *   `192.168.49.0/24`: Connects `iUpf8` to the `core_router`.
    *   `192.168.68.0/24`: Connects `iUpf8` to `mecHost8`'s `upf_mec`.
    *   `192.168.81.0/24`: Internal network within `mecHost8`.
*   **gNodeB Connections**:
    *   `gNodeB8` <-> `gNodeB4`: `192.168.15.0/24`
    *   `gNodeB8` <-> `gNodeB5`: `192.168.20.0/24`
    *   `gNodeB8` <-> `gNodeB9`: `192.168.32.0/24`

### Edge Site 9
*   **Components**: `gNodeB9`, `iUpf9`, `mecHost9`
*   **Networks**:
    *   `192.168.34.0/24`: Connects `gNodeB9` and `iUpf9`.
    *   `192.168.50.0/24`: Connects `iUpf9` to the `core_router`.
    *   `192.168.70.0/24`: Connects `iUpf9` to `mecHost9`'s `upf_mec`.
    *   `192.168.82.0/24`: Internal network within `mecHost9`.
*   **gNodeB Connections**:
    *   `gNodeB9` <-> `gNodeB5`: `192.168.21.0/24`
    *   `gNodeB9` <-> `gNodeB6`: `192.168.25.0/24`
    *   `gNodeB9` <-> `gNodeB8`: `192.168.32.0/24`
    *   `gNodeB9` <-> `gNodeB10`: `192.168.35.0/24`

### Edge Site 10
*   **Components**: `gNodeB10`, `iUpf10`, `mecHost10`
*   **Networks**:
    *   `192.168.37.0/24`: Connects `gNodeB10` and `iUpf10`.
    *   `192.168.51.0/24`: Connects `iUpf10` to the `core_router`.
    *   `192.168.72.0/24`: Connects `iUpf10` to `mecHost10`'s `upf_mec`.
    *   `192.168.83.0/24`: Internal network within `mecHost10`.
*   **gNodeB Connections**:
    *   `gNodeB10` <-> `gNodeB6`: `192.168.26.0/24`
    *   `gNodeB10` <-> `gNodeB7`: `192.168.29.0/24`
    *   `gNodeB10` <-> `gNodeB9`: `192.168.35.0/24`