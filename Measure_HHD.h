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
    uint32_t status;       // status word — signal quality flags (bytes 14-17)
    uint8_t  ledId;        // LED marker ID, 1-64 (byte 18, bits 6-0)
    uint8_t  tcmId;        // TCM module ID, 1-8  (byte 19, bits 3-0)
};

// Opaque handle for an active measurement session.
// Allocated by StartMeasurement, freed by StopMeasurement.
struct HHD_MeasurementSession;

// Start a measurement session on an already-open COM port.
//
// Sends the full configuration sequence observed in the IRP capture:
//   &` (status query) -> &v (timing) -> &L (SQR) -> &O (MSR) -> &Y (gain)
//   -> &U (SOT) -> &^ (tether) -> &Q (single sampling) -> &p (clear + program TFS)
//   -> &o (sync EOF) -> &X (multi-rate) -> &r (upload TFS) -> &: (refraction off)
//   -> &S (internal trigger) -> &3 (START)
//
// Parameters:
//   hPort       — open COM port handle (caller retains ownership)
//   frequencyHz — desired measurement rate in Hz (1–4600, clamped)
//   markers     — TFS entries defining which markers on which TCMs to sample.
//                 Each entry maps to one &p append command.
//                 The total number of entries (sum of flashCounts) determines
//                 the frame duration together with the sampling period.
//
// Returns a session handle on success, or nullptr on failure.
HHD_MeasurementSession *StartMeasurement(HANDLE hPort, int frequencyHz,
                                          const std::vector<HHD_MarkerEntry> &markers);

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
