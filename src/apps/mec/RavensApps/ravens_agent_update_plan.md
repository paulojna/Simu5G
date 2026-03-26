# Phase 2: RavensAgentApp Update Implementation Plan

## 1. Objective
Enhance the `RavensAgentApp` (which runs on each MEC Host) to collect **Radio Network Information (RNIS)** from the local `RNIService` and bundle it with the location data it already collects. This enriched dataset will then be sent to the `RavensController`.

## 2. Data Structure Updates

### 2.1 Update `AccessPointData`
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/AccessPointData.h` & `.cc`
*   **Goal:** Store cell load metrics.
*   **Changes:**
    *   Add `double dl_total_prb_usage_cell` (Downlink PRB Usage).
    *   Add `double ul_total_prb_usage_cell` (Uplink PRB Usage).
    *   Update constructors and getters/setters.
    *   Initialize these to `-1` in the default constructor.

### 2.2 Update `UserData`
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/UserData.h` & `.cc`
*   **Goal:** Store user-specific radio quality metrics.
*   **Changes:**
    *   Add `double dl_nongbr_delay_ue` (Downlink Delay).
    *   Add `double dl_nongbr_throughput_ue` (Downlink Throughput).
    *   Add `double ul_nongbr_throughput_ue` (Uplink Throughput).
    *   Add `double dl_nongbr_pdr_ue` (Packet Drop Rate).
    *   Update constructors and getters/setters.
    *   Initialize these to `-1` in the default constructor.
    *   **Critical:** Ensure the `operator==` is updated (or explicitly ignored for these fields if they fluctuate too much, to avoid spamming updates if only radio stats change slightly). *Recommendation:* Include them in equality check but maybe use a threshold? For now, include exact match.

## 3. RavensAgentApp Logic Updates

### 3.1 Initialization & Connection
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.h` & `.cc`
*   **Current State:** Already has `rnisSocket_`, `rnisAddress`, and connection logic in `handleMp1Message` (service discovery) and `established`.
*   **Action:** No major changes needed here. The agent already connects to RNIS.

### 3.2 Data Request Logic
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`
*   **Method:** `sendUserListRequest` / `sendAPListRequest`
*   **New Method:** `sendRnisRequest()`
    *   Send a GET request to `/example/rni/v2/queries/layer2_meas`.
    *   This endpoint (in `RNIService.cc`) returns a JSON with `cellInfo` and `cellUEInfo`.

### 3.3 Message Handling (`handleRNISMessage`)
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`
*   **Method:** `handleRNISMessage(int connId)`
*   **Logic:**
    1.  Parse JSON response.
    2.  **Cell Stats:** Iterate through `cellInfo`.
        *   Extract `ecgi` (Cell ID) and `dl_total_prb_usage_cell`.
        *   Find corresponding `AccessPointData` in the `accessPoints` vector.
        *   Update the PRB usage fields.
    3.  **UE Stats:** Iterate through `cellUEInfo`.
        *   Extract IP address (from `associatedId`) and stats (`dl_nongbr_delay_ue`, etc.).
        *   Find corresponding `UserData` in the `users` map.
        *   Update the UE radio stats fields.

### 3.4 Sending Updates to Controller
*   **File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`
*   **Method:** `sendUsersInfoSnapshot()`
*   **Logic:**
    *   Currently, it triggers based on `users != last_users` or timeout.
    *   Since RNIS data (load/delay) fluctuates constantly, using strict equality (`!=`) might trigger an update *every single time* RNIS data arrives.
    *   **Strategy:**
        *   Keep the `forceUpdateInterval_` logic.
        *   Allow RNIS updates to trigger a snapshot, OR sync RNIS requests to happen just before the snapshot schedule.
        *   *Recommendation:* Schedule `sendRnisRequest()` slightly before `sendUsersInfoSnapshot()` (e.g., 0.1s before).
        *   The snapshot sending logic itself (`RavensLinkUsersInfoSnapshotMessage`) handles the `UserData` map, so updating the `UserData` objects in `handleRNISMessage` is sufficient.

## 4. Execution Flow

1.  **Step 1:** Update `AccessPointData` and `UserData` classes (Header & Source).
2.  **Step 2:** Implement `sendRnisRequest` in `RavensAgentApp`.
3.  **Step 3:** Implement `handleRNISMessage` logic to parse JSON and update the internal maps/vectors.
4.  **Step 4:** Schedule `sendRnisRequest` in the `handleSelfMessage` loop (e.g., alongside or interleaved with Location requests).

## 5. Verification
*   Check logs for "RNIS Message payload with code 200".
*   Verify `UserData` objects printed in logs contain non-default (-1) radio stats.
