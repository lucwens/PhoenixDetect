#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// A single marker entry for the Target Flashing Sequence (TFS).
// Maps directly to a &p append command (PTI manual Section 4.7.8, page 32).
struct HHD_MarkerEntry
{
    uint8_t tcmId;      // TCM module ID (1-8)
    uint8_t ledId;      // LED marker ID (1-64)
    uint8_t flashCount; // flashes per cycle (1-255, typically 1)
};

// A single parsed measurement sample (PTI manual Section 4.3, page 17).
// Each sample is decoded from a 19-byte Data Set record.
struct HHD_MeasurementSample
{
    uint32_t timestamp_us; // microseconds since tracker boot (bytes 1-4)
    double   x_mm;         // X coordinate in millimeters (bytes 5-7, signed 24-bit / 100)
    double   y_mm;         // Y coordinate in millimeters (bytes 8-10)
    double   z_mm;         // Z coordinate in millimeters (bytes 11-13)
    uint32_t status;       // raw status word (bytes 14-17)
    uint8_t  ledId;        // LED marker ID, 1-64 (byte 18, bits 6-0)
    uint8_t  tcmId;        // TCM module ID, 1-8  (byte 19, bits 3-0)

    // Decoded status fields (status word bytes 14-17)
    // Byte 14: E|HHH|mmmm   Byte 15: ???|La|AAAA
    // Byte 16: TTT|Lb|BBBB  Byte 17: TTT|Lc|CCCC
    bool    endOfFrame;       // E bit: last sample in frame
    uint8_t coordStatus;      // HHH: 0 = no error
    uint8_t ambientLight;     // mmmm: max ambient light level (0-15)
    uint8_t triggerIndex;     // 6-bit trigger index (TTT:TTT from bytes 16-17)
    uint8_t rightEyeSignal;   // La: 1 = signal low
    uint8_t rightEyeStatus;   // AAAA: 0 = no anomaly
    uint8_t centerEyeSignal;  // Lb: 1 = signal low
    uint8_t centerEyeStatus;  // BBBB: 0 = no anomaly
    uint8_t leftEyeSignal;    // Lc: 1 = signal low
    uint8_t leftEyeStatus;    // CCCC: 0 = no anomaly
};

// ---------------------------------------------------------------------------
// Measurement Setup Validation
// ---------------------------------------------------------------------------

enum class HHD_IssueSeverity { Error, Warning };

struct HHD_ValidationIssue
{
    HHD_IssueSeverity severity;
    std::string       message;
};

// Validate measurement parameters against hardware operational limits
// before calling StartMeasurement.
//
// Checks the requested frequency, marker configuration, and TFS structure
// against the limits documented in OperationalLimits.md.  Issues classified
// as Error will cause the measurement to fail or produce incorrect data.
// Issues classified as Warning indicate degraded performance or hardware risk.
//
// Parameters:
//   frequencyHz — desired measurement rate in Hz
//   markers     — TFS entries (same as StartMeasurement)
//   sot         — Sample Operation Time (2–15), default 3 (matches StartMeasurement)
//
// Returns a (possibly empty) list of issues found.
std::vector<HHD_ValidationIssue> ValidateMeasurementSetup(int frequencyHz, const std::vector<HHD_MarkerEntry> &markers, int sot = 3,
                                                          bool doubleSampling = false, bool tetherless = false, int exposureGain = 0);

// Opaque handle for an active measurement session.
// Allocated by StartMeasurement, freed by StopMeasurement.
struct HHD_MeasurementSession;

// Start a measurement session on an already-open COM port.
//
// Sends the full configuration sequence observed in the IRP capture:
//   &` (software reset) -> &v (timing) -> &L (SQR) -> &O (MSR) -> &Y (gain)
//   -> &U (SOT) -> &^ (tether) -> &Q (single sampling) -> &p (clear + program TFS)
//   -> &o (sync EOF) -> &X (multi-rate) -> &r (upload TFS) -> &: (refraction off)
//   -> &S (internal trigger) -> &3 (START)
//
// Parameters:
//   hPort          — open COM port handle (caller retains ownership)
//   frequencyHz    — desired measurement rate in Hz (1–4600, clamped)
//   markers        — TFS entries defining which markers on which TCMs to sample.
//                    Each entry maps to one &p append command.
//                    The total number of entries (sum of flashCounts) determines
//                    the frame duration together with the sampling period.
//   resetTimeoutMs — max time (ms) to wait for the device to become ready after
//                    software reset. VZSoft waits ~1.7s; default 3000ms.
//
// Returns a session handle on success, or nullptr on failure.
HHD_MeasurementSession *StartMeasurement(HANDLE hPort, int frequencyHz, const std::vector<HHD_MarkerEntry> &markers, int resetTimeoutMs = 3000);

// Fetch available measurement samples from the serial buffer.
//
// Designed for use in a run loop: reads all available bytes from the port,
// parses complete 19-byte records, buffers any residual partial record for
// the next call.  Non-blocking when no data is available.
//
// Parameters:
//   session — active measurement session
//   samples — output vector; new samples are appended
//
// Returns the number of new samples appended (0 if none available).
int FetchMeasurements(HHD_MeasurementSession *session, std::vector<HHD_MeasurementSample> &samples);

// Stop the measurement and free the session.
//
// Sends &5 (stop) twice with a ~1.5s gap (matching the IRP capture),
// drains the RX buffer, and frees the session struct.
// The COM port handle is NOT closed (caller manages its lifetime).
//
// Parameters:
//   session — active measurement session (invalid after this call)
//
// Returns true if the stop was acknowledged by the tracker.
bool StopMeasurement(HHD_MeasurementSession *session);

// ---------------------------------------------------------------------------
// Configuration Detection — automatic marker & TCM discovery
// ---------------------------------------------------------------------------

// Result for a single detected marker
struct HHD_DetectedMarker
{
    uint8_t tcmId;          // TCM module ID (1-8)
    uint8_t ledId;          // LED marker ID (1-64)
    int     framesDetected; // frames where coordStatus == 0
    int     framesTotal;    // total frames captured (after warm-up)
    double  detectionRate;  // framesDetected / framesTotal (0.0–1.0)
};

// Result for a detected TCM
struct HHD_DetectedTCM
{
    uint8_t                         tcmId;   // TCM module ID (1-8)
    std::vector<HHD_DetectedMarker> markers; // active LEDs on this TCM
};

// Full configuration detection result
struct HHD_ConfigDetectResult
{
    bool                             success;
    std::vector<HHD_DetectedTCM>     tcms;       // connected TCMs with their markers
    std::vector<HHD_MarkerEntry>     markerList;  // flattened list ready for StartMeasurement
    std::string                      summary;     // human-readable summary
};

// Options for the detection scan
struct HHD_ConfigDetectOptions
{
    int    maxTcmId            = 8;    // scan TCMs 1..maxTcmId
    int    maxLedId            = 16;   // scan LEDs 1..maxLedId per TCM
    int    probeFreqHz         = 10;   // measurement frequency during probe
    int    warmupMs            = 2000; // discard data for this long at the start (tracker settling)
    int    evalMs              = 1500; // collect evaluation data for this long after warm-up
    int    minFrames           = 3;    // minimum eval frames required for a decision
    double detectionThreshold  = 0.5;  // fraction of eval frames with coordStatus==0 to consider present
};

// Detect connected TCMs and active LED markers by running a probe measurement.
//
// Programs all candidate markers (TCMs 1..maxTcmId, LEDs 1..maxLedId) into a
// single TFS, starts a measurement, discards the first warmupMs of data (the
// tracker needs time to adjust auto-exposure), then evaluates evalMs of data
// to classify each marker as present or absent.
//
// The caller must NOT have an active measurement session on the same port.
//
// Parameters:
//   hPort   — open COM port handle (caller retains ownership)
//   options — scan configuration (optional, sensible defaults)
//
// Returns:
//   HHD_ConfigDetectResult with connected TCMs and their active markers.
//   result.markerList is ready to pass directly to StartMeasurement().
HHD_ConfigDetectResult ConfigDetect(HANDLE hPort,
                                     const HHD_ConfigDetectOptions &options = {});
