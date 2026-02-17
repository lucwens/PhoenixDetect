# Configuration Detection — Automatic Marker & TCM Discovery

## 1. Problem Statement

The current measurement workflow requires the caller to supply a hardcoded list of
`HHD_MarkerEntry` structs (TCM ID + LED ID + flash count) before starting a session.
The user must know in advance which TCMs are physically connected and which LED markers
are wired to each TCM.  There is no automatic discovery step.

**Goal:** Implement a `ConfigDetect` function that probes the tracker hardware and
returns a list of connected TCMs and their active LED markers, so the caller can build
a valid TFS automatically without prior knowledge of the physical wiring.

---

## 2. Protocol-Level Analysis: Is There a Query Command?

### 2.1 Exhaustive Command Review

Every command in the PTI Low Level Control Kit manual (Section 4.7–4.8) and the
`Cheat sheet.md` was reviewed for enumeration or discovery capabilities:

| Command | Name | Discovery Potential |
|---------|------|---------------------|
| `&7` | Ping | Returns ACK confirming tracker is alive. No TCM/marker info. |
| `&?` | Query/Identify | Undocumented host acknowledgment. Returns no hardware topology. |
| `` &` `` | Software Reset | Reboots tracker. No enumeration. |
| `&p` | Program TFS | **Write-only** — appends markers to the flashing sequence. No readback. |
| `&r` | Upload TFS | Pushes TFS to TCMs. No feedback about which TCMs accepted it. |
| `&q` | Ready TCMs | Readies all TCMs. No per-TCM status. |
| `&]` | Reset TCMs | Resets all TCMs (flash once to confirm). No enumeration. |
| `&u` | Toggle Marker | Toggles a specific marker on/off for visual spotting. Requires knowing the marker ID already. |
| `&J` | Fetch Misalignment | Reads calibration parameters. Not related to TCM/marker topology. |
| `&L/O/Y/U` | Signal/Quality settings | Configuration only, no readback of connected hardware. |

### 2.2 Conclusion

**There is no protocol command to query which TCMs are connected or which LEDs are wired.**

The protocol is strictly command-and-stream: the host programs a TFS, starts sampling,
and the tracker returns 19-byte data records for every marker in the TFS regardless of
whether the hardware is physically present.  The only way to determine hardware presence
is to examine the **returned data** for each programmed marker.

---

## 3. Detection Strategy: Measurement-Based Probe Scan

Since there is no enumeration command, we must **activate candidate markers and observe
which ones return valid data**.

### 3.1 How the Tracker Reports Absent Markers

When a marker is programmed into the TFS but the hardware is not physically present
(TCM disconnected or LED not wired), the tracker still returns a 19-byte data record
for that slot.  However, the returned data will show detectable failure signatures:

| Field | Present Marker | Absent Marker |
|-------|---------------|---------------|
| `coordStatus` (HHH) | 0 (no error) | Non-zero (computation failure) |
| `rightEyeSignal` (La) | 0 (adequate) | 1 (signal low) |
| `centerEyeSignal` (Lb) | 0 (adequate) | 1 (signal low) |
| `leftEyeSignal` (Lc) | 0 (adequate) | 1 (signal low) |
| `rightEyeStatus` (AAAA) | 0 (no anomaly) | Non-zero (anomaly) |
| `centerEyeStatus` (BBBB) | 0 (no anomaly) | Non-zero (anomaly) |
| `leftEyeStatus` (CCCC) | 0 (no anomaly) | Non-zero (anomaly) |
| Coordinates (X/Y/Z) | Realistic values | Likely zeros or 0x7FFFFF (max signed 24-bit) |

**Key discrimination criteria** (a marker is considered "absent" if ANY of the following
hold consistently across multiple frames):

1. **All three eyes report signal low** (`La=1 AND Lb=1 AND Lc=1`)
2. **`coordStatus` != 0** (coordinate computation failed)
3. **All three eye status fields are non-zero** (all lenses report anomaly)

A marker that is present but temporarily occluded may fail on 1-2 eyes, so we require
**all three** to fail consistently across **multiple consecutive frames** to distinguish
hardware absence from transient occlusion.

### 3.2 Two-Phase Scan Approach

Rather than programming all 512 possible markers (8 TCMs × 64 LEDs) at once — which
would be slow and waste bandwidth — we use a two-phase approach:

#### Phase 1: TCM Discovery (fast)

Program 1 marker per TCM (LED 1 on TCMs 1–8) and run a short measurement burst.
TCMs that return valid data on LED 1 are considered "connected."

- **TFS:** 8 markers (TCM 1 LED 1, TCM 2 LED 1, ..., TCM 8 LED 1)
- **Frequency:** 10 Hz (plenty fast for a probe)
- **Duration:** ~1 second (10 frames)
- **Frame time:** 8 × 115μs + intermission = ~100,920μs ≈ 10 Hz
- **Decision:** A TCM is "present" if LED 1 returns `coordStatus=0` on ≥ 50% of frames

#### Phase 2: LED Enumeration (per connected TCM)

For each TCM found in Phase 1, program LEDs 1–16 (or 1–64 for full scan) and run
another short measurement burst.  LEDs that return valid data are considered "active."

- **TFS:** Up to 16 markers per scan (LED 1–16 on a single TCM)
- **Frequency:** 10 Hz
- **Duration:** ~1 second (10 frames)
- **Frame time:** 16 × 115μs + intermission = ~101,160μs ≈ 10 Hz
- **Decision:** An LED is "active" if it returns `coordStatus=0` on ≥ 50% of frames

### 3.3 Timing Budget

| Scenario | Phase 1 | Phase 2 | Total |
|----------|---------|---------|-------|
| 1 TCM, ≤16 LEDs | ~4s | ~4s | **~8s** |
| 2 TCMs, ≤16 LEDs each | ~4s | ~8s | **~12s** |
| 4 TCMs, ≤16 LEDs each | ~4s | ~16s | **~20s** |
| 8 TCMs, ≤64 LEDs each | ~4s | ~128s | **~132s** |

Each scan requires ~4 seconds: ~2s for software reset + device ready, ~1s measurement,
~1s for stop + cleanup.

For the common case (1–2 TCMs, ≤ 16 LEDs each), total scan time is **8–12 seconds**.

### 3.4 Alternative: Single-Pass Full Scan

If the two-phase approach is too slow or complex, a simpler single-pass approach is
viable for small configurations:

- Program all candidate markers (e.g., TCMs 1–2, LEDs 1–16 = 32 markers)
- 32 markers × 115μs = 3,680μs active time per frame → easily fits at 10 Hz
- Run for ~2 seconds, classify each marker

**Tradeoff:** Faster for small setups, but scales poorly for 8 TCMs × 64 LEDs.

---

## 4. Proposed API

### 4.1 Structures

```cpp
// Result for a single detected marker
struct HHD_DetectedMarker
{
    uint8_t tcmId;          // TCM module ID (1-8)
    uint8_t ledId;          // LED marker ID (1-64)
    int     framesDetected; // number of frames where coordStatus==0
    int     framesTotal;    // total frames captured
    double  detectionRate;  // framesDetected / framesTotal (0.0-1.0)
};

// Result for a detected TCM
struct HHD_DetectedTCM
{
    uint8_t                          tcmId;   // TCM module ID (1-8)
    std::vector<HHD_DetectedMarker>  markers; // active LEDs on this TCM
};

// Full configuration detection result
struct HHD_ConfigDetectResult
{
    bool                            success;
    std::vector<HHD_DetectedTCM>    tcms;       // connected TCMs with their markers
    std::vector<HHD_MarkerEntry>    markerList;  // flattened list ready for StartMeasurement
    std::string                     summary;     // human-readable summary
};

// Options for the detection scan
struct HHD_ConfigDetectOptions
{
    int  maxTcmId       = 8;    // scan TCMs 1..maxTcmId (default: 8)
    int  maxLedId       = 16;   // scan LEDs 1..maxLedId per TCM (default: 16)
    int  probeFreqHz    = 10;   // measurement frequency during probe
    int  probeDurationMs = 1000; // measurement duration per probe burst
    int  minFrames       = 3;   // minimum frames required for decision
    double detectionThreshold = 0.5; // fraction of frames with coordStatus==0
};
```

### 4.2 Function Signature

```cpp
// Detect connected TCMs and active LED markers by running probe measurements.
//
// Performs a two-phase scan:
//   Phase 1: Programs LED 1 on each TCM (1..maxTcmId) to discover connected TCMs.
//   Phase 2: For each connected TCM, programs LEDs 1..maxLedId to enumerate markers.
//
// The function opens/closes measurement sessions internally — the caller must NOT
// have an active measurement session on the same port.
//
// Parameters:
//   hPort   — open COM port handle (from detection phase, caller retains ownership)
//   options — scan configuration (optional, sensible defaults)
//
// Returns:
//   HHD_ConfigDetectResult with connected TCMs and their active markers.
//   result.markerList is ready to pass directly to StartMeasurement().
HHD_ConfigDetectResult ConfigDetect(HANDLE hPort,
                                     const HHD_ConfigDetectOptions& options = {});
```

### 4.3 Usage Example

```cpp
// After tracker detection:
HANDLE hPort = /* ... open COM port ... */;

// Auto-detect connected markers
HHD_ConfigDetectOptions opts;
opts.maxTcmId  = 2;   // only check TCMs 1-2 (faster)
opts.maxLedId  = 16;  // check LEDs 1-16 per TCM

auto config = ConfigDetect(hPort, opts);

if (config.success && !config.markerList.empty())
{
    std::cout << config.summary << std::endl;
    // Output: "Found 2 TCMs: TCM1 (LEDs 1-3), TCM2 (LEDs 1-3) — 6 markers total"

    // Start measurement with discovered markers
    auto* session = StartMeasurement(hPort, 10, config.markerList);
}
```

### 4.4 Integration in main.cpp

Add a new keyboard command to the interactive console:

```
  d - Detect marker configuration (auto-scan connected TCMs and LEDs)
```

When pressed:
1. Call `ConfigDetect(hPort)` with the detected tracker's port
2. Display the discovered configuration
3. Save to `Settings/MarkerConfig.json`
4. Use the discovered `markerList` for subsequent `s`/`c` measurement commands
   (instead of the current hardcoded TCM 1-2 / LED 1-3 list)

---

## 5. Implementation Plan

### 5.1 File Changes

| File | Change |
|------|--------|
| `Measure_HHD.h` | Add `HHD_DetectedMarker`, `HHD_DetectedTCM`, `HHD_ConfigDetectResult`, `HHD_ConfigDetectOptions` structs, and `ConfigDetect()` declaration |
| `Measure_HHD.cpp` | Implement `ConfigDetect()` using existing `StartMeasurement` / `FetchMeasurements` / `StopMeasurement` |
| `main.cpp` | Add `d` key handler, `SaveMarkerConfig()`, `LoadMarkerConfig()`, use discovered markers in measurement commands |

### 5.2 Internal Flow

```
ConfigDetect(hPort, options)
│
├── Phase 1: TCM Discovery
│   ├── Build TFS: LED 1 on TCMs 1..maxTcmId
│   ├── StartMeasurement(hPort, probeFreqHz, tcmProbeMarkers)
│   ├── Loop for probeDurationMs:
│   │   └── FetchMeasurements → classify each sample by tcmId
│   ├── StopMeasurement
│   └── Identify connected TCMs (detection rate ≥ threshold)
│
├── Phase 2: LED Enumeration (per connected TCM)
│   ├── For each connected TCM:
│   │   ├── Build TFS: LEDs 1..maxLedId on this TCM
│   │   ├── StartMeasurement(hPort, probeFreqHz, ledProbeMarkers)
│   │   ├── Loop for probeDurationMs:
│   │   │   └── FetchMeasurements → classify each sample by ledId
│   │   ├── StopMeasurement
│   │   └── Identify active LEDs (detection rate ≥ threshold)
│   └── Next TCM
│
└── Build result
    ├── Populate tcms[] with detected TCMs and their active markers
    ├── Flatten to markerList[] (ready for StartMeasurement)
    └── Generate summary string
```

### 5.3 Marker Classification Logic

```cpp
// Per-marker accumulator (used during a probe burst)
struct MarkerProbeStats
{
    uint8_t tcmId;
    uint8_t ledId;
    int     framesTotal    = 0;  // total records received for this marker
    int     framesValid    = 0;  // records where coordStatus == 0
    int     framesAllLow   = 0;  // records where all 3 eyes signalLow == 1
};

// After collecting samples for a probe burst:
bool IsMarkerPresent(const MarkerProbeStats& stats, const HHD_ConfigDetectOptions& opts)
{
    if (stats.framesTotal < opts.minFrames)
        return false;  // not enough data — inconclusive

    double rate = static_cast<double>(stats.framesValid) / stats.framesTotal;
    return rate >= opts.detectionThreshold;
}
```

### 5.4 Edge Cases & Robustness

| Edge Case | Handling |
|-----------|----------|
| No TCMs connected | Phase 1 returns empty → `success=true`, `tcms` empty, `markerList` empty |
| TCM present but all LEDs out of view | Phase 1 detects TCM (LED 1 may still fail). Phase 2 returns no LEDs → TCM omitted from final result |
| Marker intermittently occluded | Detection threshold (50%) tolerates occasional drops. Increase `probeDurationMs` or lower threshold if needed |
| Measurement start fails | `ConfigDetect` returns `success=false` with error in `summary` |
| Port in use by active session | Caller must stop existing session first (documented precondition) |

### 5.5 Configuration Persistence

The detected configuration is saved to `Settings/MarkerConfig.json`:

```json
{
  "detectedAt": "2026-02-17T14:30:00Z",
  "tcms": [
    {
      "tcmId": 1,
      "markers": [
        { "ledId": 1, "detectionRate": 1.0 },
        { "ledId": 2, "detectionRate": 0.9 },
        { "ledId": 3, "detectionRate": 1.0 }
      ]
    },
    {
      "tcmId": 2,
      "markers": [
        { "ledId": 1, "detectionRate": 1.0 },
        { "ledId": 2, "detectionRate": 0.8 },
        { "ledId": 3, "detectionRate": 1.0 }
      ]
    }
  ],
  "markerList": [
    { "tcmId": 1, "ledId": 1, "flashCount": 1 },
    { "tcmId": 1, "ledId": 2, "flashCount": 1 },
    { "tcmId": 1, "ledId": 3, "flashCount": 1 },
    { "tcmId": 2, "ledId": 1, "flashCount": 1 },
    { "tcmId": 2, "ledId": 2, "flashCount": 1 },
    { "tcmId": 2, "ledId": 3, "flashCount": 1 }
  ]
}
```

---

## 6. Summary

| Question | Answer |
|----------|--------|
| Can we query connected markers via a command? | **No.** The protocol has no enumeration or discovery command. |
| How do we detect connected hardware? | **Measurement-based probe scan.** Program candidate markers into the TFS, run a short burst, and classify each marker by its returned status fields. |
| How do we distinguish absent from occluded? | Absent markers fail on **all 3 eyes consistently** across multiple frames. Occluded markers may fail on 1-2 eyes or intermittently. |
| How long does the scan take? | **~8–12 seconds** for the common case (1-2 TCMs, ≤16 LEDs each). |
| What do we need to implement? | `ConfigDetect()` in `Measure_HHD.cpp`, plus a `d` key handler and config persistence in `main.cpp`. |
| Can we reuse existing infrastructure? | **Yes.** `ConfigDetect` calls `StartMeasurement` / `FetchMeasurements` / `StopMeasurement` internally — no new serial I/O code needed. |
