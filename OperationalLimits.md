# ValidateMeasurementSetup — Checked Limits

The function `ValidateMeasurementSetup(frequencyHz, markers, sot)` in `Detect/Measure_HHD.cpp` checks the following operational limits before a measurement is started. Each check is classified as **Error** (measurement will fail or produce incorrect data) or **Warning** (degraded performance or hardware risk).

## Errors

### No markers specified
The marker list is empty. At least one `HHD_MarkerEntry` must be provided.

### Frequency out of range
`frequencyHz` is below 1 Hz or above 4600 Hz. The tracker hardware does not support rates outside this range.

### SOT out of range
The Sample Operation Time must be between 2 and 15 inclusive. Values outside this range are not accepted by the tracker.

### TCM ID out of range
A marker entry has a `tcmId` outside 1–8. The system addresses up to 8 Target Control Modules.

### LED ID out of range
A marker entry has a `ledId` outside 1–64. Each TCM supports up to 64 individually addressable LEDs.

### Flash count is zero
A marker entry has `flashCount` < 1. Each marker must flash at least once per cycle.

### Total marker count exceeds 512
The system can track a maximum of 512 individually identifiable markers (8 groups of 64).

### TFS pairs per TCM exceed 64
A single TCM has more than 64 (LEDID, #flash) pairs in the Target Flashing Sequence. This exceeds the TFS memory allocated per TCMID.

### TFS TCM ID transitions exceed 64
The total number of TCMID changes while traversing the TFS (including the restart wrap-around) exceeds 64. This is a hard TFS memory constraint.

### Sampling period too fast
The total active sampling time (`totalFlashes * 115 us`) exceeds the frame period (`1000000 / frequencyHz`). There is physically not enough time to sample all markers at the requested rate. The maximum achievable frame rate is reported.

## Warnings

### LED overheating risk (>= 120 Hz)
Capture frequencies of 120 Hz or higher risk overheating the LED markers during extended captures. PTI recommends staying below 120 fps.

### SOT-bounded effective rate exceeded
Based on the SOT setting, the per-target sampling frequency has a theoretical maximum (e.g. SOT=2 -> 13020 Hz, SOT=15 -> 2441 Hz). The effective frame rate is `maxTargetHz / totalFlashes`. If the requested frequency exceeds this, samples may be silently skipped.

### Zero intermission time
The requested frequency results in a frame period exactly equal to the active sampling time, leaving zero intermission. The system is at its absolute timing limit and capture may be unreliable.

### LED ID gaps within a TCM
LED IDs under a TCM are not contiguous from 1. SIK and Octopus markers require all IDs below a target ID to be enabled — gaps cause higher IDs to not be captured. Reported as a warning because Standard markers are not affected.

### High flash count (> 10)
A marker has a `flashCount` above 10. Higher flash counts increase the LED duty cycle and heat load, and reduce the effective frame rate.

---

#Input from Gemini
Here is a breakdown of the operational limits for the Visualeyez tracker systems (VZ10K/10K5) and associated modules based on the provided documentation. I have classified these into Errors (hard limits, potential for hardware damage, or system failures) and Warnings (performance degradation, sub-optimal results, or strict operational constraints).

🚨 Errors (Strict Limits & Damage Risks)

LED Overheating Risk: It is highly recommended to keep the capture frequency under 120 fps to prevent the LED markers from getting too hot and sustaining damage during a capture.


External Trigger Damage: Applying any external signals to the tracker's EXT TRIG jack without using a PTI-supplied 'Ext Start Input' adaptor can cause permanent damage to the tracker and void the warranty.


Command Buffer Overflow: The tracker has no command buffer and processes commands immediately; sending a new serial command before the previous one has completed can cause unexpected system behavior or failures.


Sampling Period Failure: If the chosen combination of sampling settings is too fast, the "Minimum Required Sampling Period" indicator in VZSoft will turn red, meaning the capture may fail entirely.


Octopus/SIK Marker ID Gaps: When using Octopus or SIK markers, you cannot leave gaps in the enabled LED IDs. All IDs below a target ID under the same Target Control Module (TCM) must be enabled, otherwise, the markers will not be captured properly.
+4

Maximum Target Limits:

A single system can track a maximum of 512 individually identifiable markers at once, divided into 8 groups of 64.

For Octopus marker batteries, you can connect a maximum of 3 branches, with no more than 12 markers per branch, and a hard maximum of roughly 25 Octopus markers in total per battery.


Target Flashing Sequence (TFS) Memory: When programming the flashing sequence, the system only allows a maximum of 64 TCMID changes. Additionally, there is a maximum of 64 (LEDID, #flash) pairs allowed per TCMID.
+1


Analog Synchronization Rate Limitations: When syncing analog data (like force plates) via EOF pulse, the chosen analog frame rate must be divisible by the internal binary divider of the DAQ device's crystal (e.g., a 4MHz crystal cannot easily sync with 150 FPS).

⚠️ Warnings (Performance & Operational Constraints)

Frequency vs. Exposure Tradeoff: The maximum possible sampling frequency is strictly limited by the Sample Operation Time (SOT) you set. For example, setting SOT to 2 allows up to 13,020 Hz, but restricts max exposure time to 64.8 µs. Setting SOT to 15 allows up to 397.6 µs of exposure, but drops the maximum frequency to 2,441 Hz. Eye exposure time can never exceed the set SOT.
+1


Vibrator Queue Dropping: The Vibrator Activation Sequence (VAS) can only queue 64 vibrators. Because it operates on a first-come, first-served basis, if the first vibrator in the queue is not activated for more than one frame period, the entire queue is wiped clean to prevent the system from getting stuck.
+1


Multi-Rate Sampling Connector Limits: If a TCMK is set to multi-rate sampling mode SM2 (4 flashes per cycle), it will only use its lowest 16 connectors. If set to SM3 (8 flashes per cycle), it will only use its lowest 8 connectors. Markers on the remaining connectors will not flash.
+3


Environmental Lighting Fluctuations: When using manual exposure settings, time-varying ambient lights (such as standard 60Hz AC indoor lighting) may cause fluctuating, time-varying tracker performance.


Analog Data Lag: When triggering analog data collection using the end-of-frame (EOF) pulse from the motion system, there is an inherent one-motion-frame time lag between the analog data and the captured motion.


TCM Power Cycling: When turning off a Target Control Module (TCM), you must wait 30 seconds to let it reset properly before turning it back on.


Multi-Tracker Signal Bounce: When chaining 4 to 8 trackers together, a 75 ohm terminator must be used on the last slave tracker to suppress electronic signals from bouncing back and causing false triggers.
+1


VZDaq Signal Range: When configuring analog channels, setting an input range that is too small for the signal will cut off the data, while using a range that is too large for a small signal will degrade the resolution of the graph.

#Input from ChatGPT
Below is a consolidated list of operational limitations I can extract from your PTI/Visualeyez documentation, grouped and tagged as [ERROR] (will break/invalid/undefined behavior, or data not produced correctly) vs [WARNING] (operation continues but with risk of bad data, missed samples, hardware risk, or user-action constraints).

Sampling rate and “fps vs #markers” limits
Maximum per-target sampling frequency is bounded by SOT

Limit: SOT range = 2…15; this directly bounds max sampling frequency (per sensed target). Example table values:

SOT=2 → 13,020 Hz

SOT=15 → 2,441 Hz 

PTI LOW LEVEL CONTROL KIT FEB 2…

Consequence: You can’t exceed that sampling rate for acquiring target data, regardless of software “fps” settings.

Class: [ERROR] (hard cap)

Effective frame rate depends on how many target-samples you take per frame

Because Visualeyez senses only one target at a time, multiple targets require sequential flashing (TFS). 

PTI LOW LEVEL CONTROL KIT FEB 2…


A practical relationship is:

Let:

f_target_max = max per-target sampling frequency from SOT table

S = Σ(#flash) over the full Target Flashing Sequence (TFS) for Standard markers (and effectively 1 for SIK/Octopus entries)

Then max frame/cycle rate is approximately:

fps_max ≈ f_target_max / S

Implication: doubling the number of markers (or increasing #flash averaging) roughly halves the maximum achievable fps, all else equal. (This follows directly from “one target at a time” + sequential TFS.)

Class: [WARNING] (you can request more, but you won’t get what you think; capture may fail/skip)

“Too fast” sampling timing in VZSoft

Limit indicator: In VZSoft Sampling Timing, if “Minimum Required Sampling Period” is red, the settings combination is too fast and capture may not be successful. 

QuickStartGuide

Also: If “Dominate” is enabled and shown in red, capture may fail. 

QuickStartGuide

Class: [WARNING] (software warns; capture may fail)

Sampling period too short can cause automatic skipped sampling

In low-level periodic sampling:

Limit: If the sampling period is too short, the system may skip a sampling process automatically to allow time for switching between TCMIDs; this depends on SOT and the largest TCMID used in the TFS. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [WARNING] (you still run, but you silently miss samples)

Marker flashing / heat / hardware safety limits
LED overheating risk vs fps

Limit guidance: “Ensure the LEDs never get too hot during a capture to prevent damage (< 120 fps is highly recommended).” 

QuickStartGuide

Class: [WARNING] (hardware damage risk; capture can still run)

Standard marker #flash (averaging / repeated captures)

Limit: #flash per Standard marker is 1…255 consecutive flashes/samplings before advancing. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Consequence: Higher #flash reduces effective fps (see formula above) and increases LED duty/heat load.

Class: [WARNING] (data rate + heating risk)

Target capacity and sequence-structure limits (TFS / IDs)
Total identifiable targets

Limit: Up to 512 individually identifiable targets/markers, divided into 8 groups of 64 (TCMID 1..8, LEDID 1..64). 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [ERROR] (hard system addressing limit)

Only one target can be sensed at any moment

Limit: “Only the position of one single target can be sensed at any one time… targets must be flashed one at a time, sequentially (TFS).” 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [ERROR] (fundamental operating constraint)

TFS memory / size constraints

Limit (general): “The amount of memory buffer reserved for keeping the TFS is limited.” 

PTI LOW LEVEL CONTROL KIT FEB 2…

Limit (explicit programming constraints):

For a given TCMID, max (LEDID,#flash) pairs in TFS is 64

Max number of TCMID changes counted while traversing TFS is 64 (restart counts as a change) 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [ERROR] (beyond this you cannot construct/execute the intended sequence correctly)

Multi-Rate Sampling reduces usable physical connectors (SM2/SM3)

Limit: TCMKs can run SM0..SM3, where a marker may flash up to 1/2/4/8 times per cycle, but:

In SM2, a TCMK32 assigns 4 LEDIDs only to its lowest 16 connectors; the rest get no LEDID → markers there never flash.

In SM3, only the lowest 8 connectors get LEDIDs (TCMK32/TCMK16); the rest never flash.

Class: [ERROR] (markers on “non-assigned” connectors won’t be captured)

Octopus battery/branch loading limits

Limit: Each Octopus battery supports up to ~25 markers total, up to 3 branches, and no more than 12 markers per branch. 

QuickStartGuide

Class: [ERROR] (exceeding supported topology is outside spec)

SIK/Octopus “no gaps” enablement rule

Limit: For Octopus/SIK markers, all LEDIDs below a marker under the same TCM must be enabled with no gaps; otherwise higher IDs won’t be captured properly (example: to capture 305 you must enable 301–304). 

QuickStartGuide

Class: [ERROR] (data not captured/identified correctly)

Optical/environment constraints affecting accuracy or measurability
Target wavelength / effective range constraint

Limit: Best long-distance sensing is for LED wavelength roughly 730–810 nm; LEDs outside that range but within ~720–850 nm are supported over shorter (~60%) distance. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [WARNING] (reduced operating distance / robustness)

Target must be the “brightest” light source

Limit: To sense a target, it must be the brightest light source within the tracker’s operating space at the time. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [WARNING] (otherwise accuracy/lock can fail)

Exposure/lighting conditions can cause inaccurate or impossible computation

Limit: If any Eye is over- or under-exposed, computed coordinates may be inaccurate or computation may not be possible; auto-exposure is recommended especially under AC lighting. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [WARNING] (quality/availability risk)

Status-word “quality” flags (data quality degradation warnings)

Limit: Status word includes eye-level flags like COR_CORR_LOW (“Warning: Data quality low…”) and raw/processed signal weak/high indicators that imply noisy or inaccurate positions. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [WARNING] (data still produced but quality compromised)

Command/communication timing limits (low-level control)
No command buffer: don’t send a new command before the previous completes

Limit: Tracker processes commands immediately and has no command buffer; sending a command before the previous one completes may cause unexpected behavior; recovery is hardware reset (RESET).

Class: [ERROR] (undefined behavior / may break capture)

RS-422 comm requirements

Limit: Requires RS-422 serial port and correct wiring (pinouts vary by card manufacturer), and host port settings: 2.5 MBaud, 8N1, RTS/CTS disabled. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [ERROR] (no comms / malfunction)

Reset timing constraints

Limit: Hardware reset requires pulling RESET\ low ≥1s, then wait ≥2s after release for reboot. 

PTI LOW LEVEL CONTROL KIT FEB 2…


Core-reset command `&`` resets other hardware for 0.1s then idles ~1.5s; you must wait for ACK before issuing another command. 

PTI LOW LEVEL CONTROL KIT FEB 2…

Class: [ERROR] (violating timing can break control flow)

Setup/operation workflow constraints (VZSoft / quick start)
Tracker detection window must remain open

Limit: “Do NOT close the tracker detection window or your tracker will no longer be detected.” 

QuickStartGuide

Class: [ERROR] (loses detection/operation)

Standard-marker control module reset timing

Limit: When turning off a TCM, wait 30 seconds to let it reset properly before turning it back on. 

QuickStartGuide

Class: [WARNING] (prevents flaky behavior)

Multi-tracker trigger chain termination

Limit: For 4–8 trackers, a 75Ω terminator should be used; with >8 trackers a terminator may not be necessary (consult PTI if issues). 

QuickStartGuide

Class: [WARNING] (risk of false triggers / instability if wrong)

Analog acquisition side limits (only if you’re using VZDaq)
Max analog channels per VZDaq instance

Limit: Each VZDaq instance supports max 64 analog input channels; more requires multiple instances/PCs (theoretically unlimited). 

VZDaq_Manual_V200

Class: [ERROR] (software hard limit per instance)

Version compatibility

Limit: VZDaq 2.00 requires VZSoft 2.70.11beta or later for proper operation. 

VZDaq_Manual_V200

Class: [ERROR] (incompatible versions)