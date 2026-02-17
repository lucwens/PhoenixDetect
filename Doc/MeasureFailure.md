# Measurement Failure Analysis

## Capture Comparison: VZSoft vs Detect

| Metric | VZSoft | Detect (before fix) | Detect (after fix) |
|---|---|---|---|
| dmslog8 size | 1,652,736 bytes | 97,316 bytes | 163,520 bytes |
| txt trace lines | 153,674 | 2,343 | 8,626 |
| Total frames (JSON) | 95 | 2 | 644 |
| TX commands | 22 | 1 | 23 |
| RX ACK messages | 18 | 0 | 21 |
| RX data sets | 55 | 1 (garbage) | 600 |

The VZSoft capture ran at 1 Hz with 5 markers for ~10s (55 data sets).
The fixed Detect capture ran at 10 Hz with 6 markers for ~10s (600 data sets).
Both sessions completed successfully with all configuration commands ACKed.

## Root Cause

**The `&`` (Software Reset) command does not generate an ACK, but the Detect code waited for one.**

In `Measure_HHD.cpp:224-229` (before fix):

```cpp
// 1. Status/version query: &` 000           <-- misleading comment: this is a Software Reset
auto cmdStatus = BuildCommand('`', '0', '0', '0');
if (!SendCommand(hPort, cmdStatus))           <-- sendOnly defaults to false; waits for 19-byte ACK
{
    std::cerr << "  [Measure] Status query failed" << std::endl;
    return nullptr;                            <-- aborts entire measurement sequence
}
```

`SendCommand()` with `sendOnly=false` polls `ClearCommError` for 500ms waiting for >= 19 bytes in the RX queue. The device is rebooting and cannot respond. The poll loop exhausts, prints "ACK timeout for command 0x60", and `StartMeasurement` returns `nullptr`.

### Evidence from the Detect IRP trace (before fix)

The Detect trace confirms the exact failure path:

```mermaid
sequenceDiagram
    participant D as Detect
    participant P as COM Port
    participant H as HHD Device

    D->>P: TX &`000 (Software Reset)
    Note over H: Device rebooting...

    loop 47 polls over ~737ms
        D->>P: GET_COMMSTATUS
        P-->>D: In queue = 0
    end

    Note over D: ACK timeout (500ms + overhead)
    Note over D,H: SendCommand returns false → StartMeasurement aborts
```

1. **50.286565s** -- Write `26 60 30 30 30 0d` (`&`000\r`, Software Reset)
2. **50.286621s - 51.023390s** -- 47 consecutive `GET_COMMSTATUS` polls, all returning `In queue = 0`
3. **No ACK ever arrives** -- the trace ends with continuous zero-byte polls

The polling spans ~737ms (500ms ACK timeout + ReadTotalTimeoutConstant overhead), after which `SendCommand` returns `false` and the measurement is aborted at the very first command.

### Evidence from the VZSoft IRP trace

VZSoft handles the Software Reset completely differently:

```mermaid
sequenceDiagram
    participant V as VZSoft
    participant P as COM Port
    participant H as HHD Device

    V->>P: TX &`000 (Software Reset)
    Note over V: No ACK expected
    Note over H: Device rebooting (~1.7s)

    V->>P: TX &v042 (Set Sampling Period)
    P-->>V: RX ACK for &v
    V->>P: TX &L0 (Set SQR)
    P-->>V: RX ACK for &L
    V->>P: TX &O0 (Set MSR)
    P-->>V: RX ACK for &O

    Note over V,H: ...continues with all config commands ACKed
```

1. **05.027206s** -- TX `&`000` (Software Reset) -- **no ACK expected or received**
2. **06.732695s** -- TX `&v042` (Set Sampling Period) -- **1.705 seconds later**
3. **06.748556s** -- RX ACK for `&v` -- first ACK in the entire session

VZSoft sends the reset as fire-and-forget, waits ~1.7 seconds for the device to reboot, then proceeds with configuration commands that DO generate ACKs.

## Full VZSoft Command Sequence (from JSON)

```mermaid
sequenceDiagram
    participant V as VZSoft
    participant H as HHD Device

    rect rgb(255, 230, 230)
        Note over V,H: Phase 1 — Reset (no ACK)
        V->>H: &`0 Software Reset
        Note over V: Wait ~1.7s for reboot
    end

    rect rgb(230, 255, 230)
        Note over V,H: Phase 2 — Configuration (all ACKed)
        V->>H: &v0 Set Sampling Period
        H-->>V: ACK
        V->>H: &L0 Set SQR
        H-->>V: ACK
        V->>H: &O0 Set MSR
        H-->>V: ACK
        V->>H: &Y Set Auto-Exposure Gain
        H-->>V: ACK
        V->>H: &U0 Set SOT
        H-->>V: ACK
        V->>H: &^ Enable Tether Mode
        H-->>V: ACK
        V->>H: &Q Enable Single Sampling
        H-->>V: ACK
        V->>H: &p0 Clear TFS
        H-->>V: ACK
        V->>H: &p1/&p2 Append TFS entries (x6)
        H-->>V: ACK (each)
        V->>H: &o0 TCM Sync on EOF
        H-->>V: ACK
        V->>H: &X0 Set Multi-Rate Sampling
        H-->>V: ACK
        V->>H: &r0 Program TFS Into TCMs
        H-->>V: ACK
        V->>H: &: Disable Refraction Compensation
        H-->>V: ACK
        V->>H: &S0 Enable Internal Triggering
        H-->>V: ACK
    end

    rect rgb(230, 230, 255)
        Note over V,H: Phase 3 — Start (no ACK)
        V->>H: &30 Start Periodic Sampling
        loop Data streaming
            H-->>V: Data set
        end
    end
```

**Key insight:** Only two commands produce no ACK: Software Reset (`&``) and Start (`&3`). The Detect code correctly uses `sendOnly=true` for `&3` (line 339) but incorrectly waited for an ACK on `&``.

## Applied Fix

Three changes to the measurement module:

### 1. `Measure_HHD.h` -- New `resetTimeoutMs` parameter

```cpp
HHD_MeasurementSession *StartMeasurement(HANDLE hPort, int frequencyHz,
    const std::vector<HHD_MarkerEntry> &markers, int resetTimeoutMs = 2000);
```

### 2. `Measure_HHD.cpp` -- New `WaitForDeviceReady` helper

```mermaid
flowchart TD
    A[WaitForDeviceReady called] --> B{elapsed < timeoutMs?}
    B -- Yes --> C[ClearCommError]
    C --> D{cbInQue > 0?}
    D -- Yes --> E[PurgeComm RX]
    E --> F[Return true — device alive]
    D -- No --> G[Sleep 10ms]
    G --> H[elapsed += 10ms]
    H --> B
    B -- No --> I[PurgeComm RX]
    I --> J[Return false — timeout, proceed anyway]
```

```cpp
bool WaitForDeviceReady(HANDLE hPort, int timeoutMs)
{
    DWORD   errors  = 0;
    COMSTAT comstat = {};
    int     elapsed = 0;
    while (elapsed < timeoutMs)
    {
        ClearCommError(hPort, &errors, &comstat);
        if (comstat.cbInQue > 0)
        {
            PurgeComm(hPort, PURGE_RXCLEAR);
            return true;   // device is alive -- proceed immediately
        }
        Sleep(RESET_POLL_MS);
        elapsed += RESET_POLL_MS;
    }
    PurgeComm(hPort, PURGE_RXCLEAR);
    return false;  // timeout -- proceed anyway
}
```

Polls the RX queue every 10ms. Returns early as soon as the device sends any data (indicating it has rebooted), or proceeds after the timeout expires. Drains received data in either case.

### 3. `Measure_HHD.cpp` -- Software Reset with `sendOnly=true`

```mermaid
sequenceDiagram
    participant D as Detect
    participant P as COM Port
    participant H as HHD Device

    rect rgb(255, 230, 230)
        Note over D,H: Old behavior (broken)
        D->>P: TX &`000 (Software Reset)
        D->>P: Poll for 19-byte ACK
        Note over D: Timeout → abort
    end

    rect rgb(230, 255, 230)
        Note over D,H: New behavior (fixed)
        D->>P: TX &`000 (sendOnly=true)
        Note over D: WaitForDeviceReady (up to 2000ms)
        loop Poll every 10ms
            D->>P: ClearCommError
            P-->>D: cbInQue
        end
        Note over D: Device ready or timeout → continue either way
        D->>P: TX &v (Set Sampling Period)
        P-->>D: RX ACK ✓
    end
```

```cpp
auto cmdReset = BuildCommand('`', '0', '0', '0');
if (!SendCommand(hPort, cmdReset, /*sendOnly=*/true))
    ...
WaitForDeviceReady(hPort, resetTimeoutMs);
```

Sends the reset without waiting for an ACK, then uses the polling wait instead of a blind `Sleep`.

### Result

The fix is confirmed working. The Detect capture after the fix shows:
- All 21 configuration commands sent and ACKed correctly
- 600 data sets received (10 Hz x 6 markers x 10 seconds)
- Complete measurement cycle including clean stop
