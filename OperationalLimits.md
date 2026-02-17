# ValidateMeasurementSetup — Checked Limits

The function `ValidateMeasurementSetup(frequencyHz, markers, sot, doubleSampling, tetherless, exposureGain)` in `Detect/Measure_HHD.cpp` checks the following operational limits before a measurement is started. Each check is classified as **Error** (measurement will fail or produce incorrect data) or **Warning** (degraded performance or hardware risk).

## Errors

| Check | Description |
|---|---|
| No markers specified | The marker list is empty. At least one `HHD_MarkerEntry` must be provided. |
| Frequency out of range | `frequencyHz` is below 1 Hz or above 4600 Hz. The tracker hardware does not support rates outside this range. |
| SOT out of range | The Sample Operation Time must be between 2 and 15 inclusive. Values outside this range are not accepted by the tracker. |
| TCM ID out of range | A marker entry has a `tcmId` outside 1–8. The system addresses up to 8 Target Control Modules. |
| LED ID out of range | A marker entry has a `ledId` outside 1–64. Each TCM supports up to 64 individually addressable LEDs. |
| Flash count is zero | A marker entry has `flashCount` < 1. Each marker must flash at least once per cycle. |
| Total marker count exceeds 512 | The system can track a maximum of 512 individually identifiable markers (8 groups of 64). |
| TFS pairs per TCM exceed 64 | A single TCM has more than 64 (LEDID, #flash) pairs in the Target Flashing Sequence. This exceeds the TFS memory allocated per TCMID. |
| TFS TCM ID transitions exceed 64 | The total number of TCMID changes while traversing the TFS (including the restart wrap-around) exceeds 64. This is a hard TFS memory constraint. |
| Sampling period too fast | The total active sampling time (`totalFlashes * 115 us`) exceeds the frame period (`1000000 / frequencyHz`). There is physically not enough time to sample all markers at the requested rate. The maximum achievable frame rate is reported. |

## Warnings

| Check | Description |
|---|---|
| LED overheating risk (>= 120 Hz) | Capture frequencies of 120 Hz or higher risk overheating the LED markers during extended captures. PTI recommends staying below 120 fps. |
| SOT-bounded effective rate exceeded | Based on the SOT setting, the per-target sampling frequency has a theoretical maximum (e.g. SOT=2 -> 13020 Hz, SOT=15 -> 2441 Hz). The effective frame rate is `maxTargetHz / totalFlashes`. If the requested frequency exceeds this, samples may be silently skipped. |
| Zero intermission time | The requested frequency results in a frame period exactly equal to the active sampling time, leaving zero intermission. The system is at its absolute timing limit and capture may be unreliable. |
| LED ID gaps within a TCM | LED IDs under a TCM are not contiguous from 1. SIK and Octopus markers require all IDs below a target ID to be enabled — gaps cause higher IDs to not be captured. Reported as a warning because Standard markers are not affected. |
| High flash count (> 10) | A marker has a `flashCount` above 10. Higher flash counts increase the LED duty cycle and heat load, and reduce the effective frame rate. |
| Double sampling penalty | When double sampling (background subtraction) is enabled, the effective SOT is doubled. This halves the maximum per-target sampling rate and may cause the SOT-bounded effective rate to be exceeded at the requested frequency. |
| Tetherless mode interference risk | In tetherless mode, radio interference may scramble LED_ID and TCM_ID data. While coordinates may remain accurate, frame identification is unreliable. |
| Exposure gain too high (> 10) | An auto-exposure feedback gain above 10 may cause the system to overshoot when a marker moves abruptly, degrading signal quality and position accuracy. |

---

