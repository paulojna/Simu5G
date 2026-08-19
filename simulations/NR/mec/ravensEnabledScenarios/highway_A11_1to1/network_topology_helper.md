# Network Topology for highway_A11_1to1 Scenario

Companion to `tust_1to1/network_topology_helper.md`, for the `highwayA11_1to1_v2`
network. Same architecture, 6 edge sites instead of 10, and the base stations are
a chain rather than a mesh.

Addresses and routes live in `manual_config_v2.xml`. `manual_config.xml` is the
old auto-generated dump — it is kept only for reference and must not be used; see
"Why the dump does not work" at the end.

## Reading the interface names

Interface names come from the order gates are connected in
`highwayA11_1to1_v2.ned`, not from the module names. Change the connection order
and every name shifts.

| module | interface | goes to |
|---|---|---|
| `gnbN` | `cellular` | the air interface (UEs) |
| `gnbN` | `pppIf` | its local `iUpfN` |
| `gnbN` | `x2ppp0`, `x2ppp1` | X2 peers, in connection order |
| `gnbN` | `pppMEHostIf` | nothing — unconnected, still addressed |
| `iUpfN` | `ppp0` | `mecHostN` |
| `iUpfN` | `ppp1` | `core_router` |
| `iUpfN` | `ppp2` | `gnbN` |
| `iUpfN` | `pppIf` | nothing — unconnected, still addressed |
| `upf` | `pppIf` | `router` (this is `filterGate`) |
| `upf` | `ppp0` | `core_router` |
| `upf` | `ppp1` | `mecHost7` (the cloud host) |
| `core_router` | `ppp0`..`ppp5` | `iUpf1`..`iUpf6` |
| `core_router` | `ppp6` | `upf` |
| `router` | `ppp0`, `ppp1`, `ppp2` | `upf`, `ualcmp`, `server` |

Note `upf.ppp0`/`upf.ppp1` are the **opposite way round** from `tust_1to1`, where
`ppp0` is the cloud host and `ppp1` is the `core_router`. That is purely because
the two connection lines appear in the other order in the NED file.

## Core Router Connections

*   `core_router` <-> `iUpf1`: `192.168.20.0/24`
*   `core_router` <-> `iUpf2`: `192.168.21.0/24`
*   `core_router` <-> `iUpf3`: `192.168.22.0/24`
*   `core_router` <-> `iUpf4`: `192.168.23.0/24`
*   `core_router` <-> `iUpf5`: `192.168.24.0/24`
*   `core_router` <-> `iUpf6`: `192.168.25.0/24`
*   `core_router` <-> `upf`: `192.168.26.0/24`

## Other Key Networks

*   **Central Services Network**: a central `router` connects the main `upf`, the
    `ualcmp` and the `server` (the RAVENS Controller).
    *   `router` <-> `upf`: `192.168.17.0/24`
    *   `router` <-> `ualcmp`: `192.168.18.0/24`
    *   `router` <-> `server`: `192.168.19.0/24`

*   **Cloud MEC Host**: the main `upf` connects to `mecHost7`, the fallback host
    that stands in for the cloud. It is the equivalent of `mecHost11` in
    `tust_1to1`.
    *   `upf` <-> `mecHost7`: `192.168.47.0/24`
    *   internal to `mecHost7`: `192.168.45.0/24`

*   **Cellular Network (`10.0.0.0/8`)**: `gnbN` has `10.0.0.N`. This is the air
    interface. Nothing but UE traffic is ever routed over it.

*   `192.168.46.0/24` is unused — a gap in the plan, harmless.

---

## Edge Site Details

Each site N has `gnbN`, `iUpfN`, `mecHostN`.

| site | gNB <-> iUpf | iUpf <-> core | iUpf <-> mecHost | inside mecHost | X2 link it owns | unused |
|---|---|---|---|---|---|---|
| 1 | `192.168.0.0/24` | `192.168.20.0/24` | `192.168.27.0/24` | `192.168.39.0/24` | gnb1<->gnb2 `192.168.1.0/24` | `2.0`, `28.0` |
| 2 | `192.168.3.0/24` | `192.168.21.0/24` | `192.168.29.0/24` | `192.168.40.0/24` | gnb2<->gnb3 `192.168.4.0/24` | `5.0`, `30.0` |
| 3 | `192.168.6.0/24` | `192.168.22.0/24` | `192.168.31.0/24` | `192.168.41.0/24` | gnb3<->gnb4 `192.168.7.0/24` | `8.0`, `32.0` |
| 4 | `192.168.9.0/24` | `192.168.23.0/24` | `192.168.33.0/24` | `192.168.42.0/24` | gnb4<->gnb5 `192.168.10.0/24` | `11.0`, `34.0` |
| 5 | `192.168.12.0/24` | `192.168.24.0/24` | `192.168.35.0/24` | `192.168.43.0/24` | gnb5<->gnb6 `192.168.13.0/24` | `14.0`, `36.0` |
| 6 | `192.168.15.0/24` | `192.168.25.0/24` | `192.168.37.0/24` | `192.168.44.0/24` | — | `16.0`, `38.0` |

"Unused" are `gnbN.pppMEHostIf` and `iUpfN.pppIf`: interfaces the node types
create but this topology never connects. They still need addresses, or the
configurator assigns its own and the plan drifts.

"X2 link it owns" is the link to the *next* base station. The lower-numbered end
takes `.1`, the higher-numbered end `.2`. So `gnb3` is `192.168.7.1` on
`x2ppp1` and `gnb4` is `192.168.7.2` on `x2ppp0`.

---

## The routing rules

Five rules produce every route in the file. They are the same rules as
`tust_1to1`, which is the point — the two scenarios must behave the same way.

**Base station `gnbN`** (7 routes)
1. One `/32` per X2 neighbour, out the matching `x2pppN`, gateway = the peer's
   X2 address. Direct link, no detour.
2. One `/32` per *non*-neighbour base station, out `pppIf` to the local `iUpfN`.
3. `192.168.0.0` netmask `255.255.128.0` out `pppIf` — everything else in the
   core goes up to the local `iUpf`. (Covers `192.168.0.0`-`192.168.127.255`;
   the highest subnet in use is `47`.)
4. `10.0.0.0/8` out `cellular`, metric 1 — its own UEs, and only its own UEs.

**`iUpfN`** (10-11 routes)
1. Local MEC host subnets out `ppp0`.
2. One `/32` per *other* base station up to `core_router` on `ppp1`, metric 1.
3. `10.0.0.0/8` down to `gnbN` on `ppp2`, metric 5 — the fallback for its own UEs.
4. The X2 subnets `gnbN` sits on, down on `ppp2`.
5. Default up to `core_router` on `ppp1`, metric 10.

**`core_router`** (42 routes)
1. Default via `upf` on `ppp6`, metric 100.
2. `10.0.0.0/8` to `lo0`, metric 99 — a blackhole. Without it, a packet for a
   `10.x` address with no more specific route bounces between `core_router` and
   `upf` forever.
3. Per site: the base station's `/32`, its access subnet, the X2 subnet it owns,
   its `pppMEHostIf` subnet, the iUpf<->mecHost subnet and the mecHost internal
   subnet — all via that site's `iUpf`.
4. The five subnets behind `upf` via `ppp6`.

Each X2 subnet is listed exactly once, under the iUpf of the lower-numbered end.
Listing it twice gives `core_router` two equal-cost paths and the choice becomes
an ordering accident.

**`upf`** (11 routes) — connected subnets, `mecHost7`'s internal subnet via
`mecHost7`, `ualcmp`+`server` as one `/23` via `router`, `10.0.0.0/8` back down
via `core_router`, default via `core_router`.

**`router` / `server` / `ualcmp` / MEC hosts** — connected subnets plus a
default. Nothing routes through them.

### Metrics

`Ipv4RoutingTable` sorts by netmask length descending, then metric ascending, and
takes the first match. So longest-prefix always wins and the metrics only break
ties between routes of equal prefix length. They are set to make the intent
readable, not to change the outcome.

### Connected routes are automatic

`Ipv4RoutingTable.netmaskRoutes` defaults to `"*"`, so every host installs a
route for each of its own interface subnets, at that interface's metric (`1`
here, from the `metric="1"` on every `<interface>` line). This is why the
`255.255.128.0` catch-all on a base station does **not** swallow its X2 traffic:
the automatic `/24` for `x2ppp0` is a longer prefix and wins.

Two consequences worth remembering:
* `metric="1"` on the `<interface>` entries is load-bearing. Change it and you
  change how connected routes rank against the manual ones.
* A few manual routes in this file duplicate an automatic one at metric 0. They
  are redundant, they point the same way, and they are kept because `tust_1to1`
  has them.

---

## Why the dump does not work

`manual_config.xml` was produced with `*.configurator.dumpConfig`. It cannot be
used, for two reasons.

**It routes the core network over the radio.** All six `cellular` interfaces are
one `10.0.0.0/8` wireless group, so the configurator treated them as a shared LAN
and computed shortest paths straight through. 128 of its routes look like:

```xml
<route hosts="highwayA11_1to1_v2.gnb1" destination="192.168.42.0"
       netmask="255.255.255.0" gateway="10.0.0.4" interface="cellular" metric="0"/>
```

"To reach a core subnet, transmit over the air with `gnb4`'s radio address as the
next hop." In Simu5G that interface is the 5G air link; it cannot carry backhaul.

**It loops to the cloud host.** Nothing in it reaches `mecHost7` — traffic for
`192.168.45.0/24` ping-pongs `upf -> core_router -> upf`. Every migration to the
fallback host would have been dropped.

It also carries 103 aggregated supernets left over from `optimizeRoutes`, several
of them contradictory (`gnb1` has `192.168.0.0/255.255.192.0` twice, with
different gateways).

Because the ini sets `optimizeRoutes`, `addStaticRoutes`, `addDefaultRoutes` and
`addSubnetRoutes` all to `false`, the XML *is* the routing table. There is no
fallback quietly fixing any of this.

## Checking a change

The address plan and the routes are generated, not typed. After editing the NED
connection order or adding a site, regenerate and re-check rather than patching
the XML by hand:

* the `<interface>` block must stay consistent with the NED connection order —
  the interface-name table at the top of this file is the mapping;
* no route may ever have `interface="cellular"` with a `10.0.0.x` gateway;
* every mecHost must be reachable from `server`, from `ualcmp`, and from every
  other mecHost (that last one is the migration path);
* X2 neighbours must be one hop apart.

## Differences from tust_1to1

| | tust_1to1 | highway_A11_1to1 |
|---|---|---|
| edge sites | 10 | 6 |
| X2 topology | mesh, up to 6 peers | chain, at most 2 peers |
| cloud host | `mecHost11` | `mecHost7` |
| `mecOrchestrator.mecHostIndex` | 10 | 6 |
| carrier | 6 GHz, 100 RB | 2 GHz — sites are ~2 km apart |
| scenario | `URBAN_MACROCELL` | `RURAL_MACROCELL` |
| routes in config | 489 | 243 |
