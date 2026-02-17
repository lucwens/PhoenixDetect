Based on the provided technical documentation for the Visualeyez system (VZ10K/VZ10K5) and VZDaq, here is an investigation of the operational limits, classified as Errors (hard limits, failures, or damage risks) and Warnings (potential for poor data quality or reduced performance).
1. Measurement Frequency and Timing Limits
The operational speed of the tracker is governed by the Sample Operation Time (SOT). This is a configurable parameter (ranging from 2 to 15) that dictates how long the tracker spends sampling a single target [1].
Classification
Operational Limitation
Details & Source
Error
Maximum Sampling Frequency (Single Target)
The tracker has a hard frequency limit based on the SOT. For a single target, the maximum frequency is 13,020 Hz (at SOT 2) and drops to 2,441 Hz (at SOT 15) [2]. Exceeding this physical limit is impossible.
Error
Sequence Intermission / Skipping
If the user-specified "Sampling Period" is set too short relative to the SOT and the number of Target Control Modules (TCMs) involved, the tracker will automatically skip a sampling process to allow time for switching TCMs. No target is sensed during a skip [3].
Warning
Frequency vs. Marker Count
The system senses targets one at a time [4, 5]. Therefore, the effective frame rate for a full rigid body decreases as you add markers. To capture complex motions, you must balance the number of markers against the SOT setting [6].
Warning
Double Sampling Penalty
If "Double Sampling" (background subtraction) is enabled to handle ambient light, the required SOT doubles. This reduces the maximum sampling rate accordingly [7].
2. Marker and LED Limits (Flash Parameters)
These limits relate to the physical LEDs (Standard, Octopus, or SIK markers) and their control modules (TCMs).
Classification
Operational Limitation
Details & Source
Warning
Overheating Risk
To prevent damage to LED markers, it is highly recommended to keep the capture frequency below 120 fps. LEDs should never get too hot during capture [8].
Error
Maximum Marker Capacity
The system is limited to tracking a maximum of 512 individually identifiable targets (8 groups of 64 markers) [4].
Error
TFS Memory Buffer
The "Target Flashing Sequence" (TFS) has memory limits: 1. Maximum 64 (LEDID, #flash) pairs per TCMID [9].2. Maximum 64 TCMID changes within a single sequence [10].
Error
Tetherless Interference
In tetherless mode, radio noise can disrupt operation. If interference occurs, the LED_ID and TCM_ID data may become scrambled (though coordinates may remain accurate), ruining the frame data [11].
3. Exposure and Optical Limits
The VZ10K trackers use linear CCDs that require specific exposure times to resolve target coordinates.
Classification
Operational Limitation
Details & Source
Error
Exposure Time vs. SOT
The exposure time for the sensing eyes cannot exceed the SOT setting. If the environment requires a longer exposure than the SOT allows, the SOT must be increased, or the computation will fail [1, 12].
Error2
Signal Saturation
If the raw signal is too high (Status bit 4: NUC_PEAK_HIGH), the captured marker position will be inaccurate due to sensor saturation [13].
Warning
Weak Signal
If the signal is weak (Status bit 2: NUC_PEAK_LOW), the captured marker position may be noisy [13].
Warning
Auto-Exposure Gain
Setting the Auto-Exposure feedback gain too high may cause the system to overshoot when a marker moves abruptly, resulting in signal strength that is worse for position computation [14].
4. System and Communicati
These limits involve the communication between the host computer and the tracker hardware.
Classification
Operational Limitation
Details & Source
Error
No Command Buffer
The tracker does not contain a command buffer. Any command sent before the previous one is fully processed may cause unexpected behavior. Users must wait for an acknowledgement (ACK) before sending the next command [15, 16].
Error
Hardware Reset Delay
After a software reset command (&``), the tracker core requires ~1.5 seconds to reboot. Sending commands during this window will fail [17].
Error
Permanent Damage Risk (External Trigger)
Applying improper signals (e.g., non-TTL, wrong width) or not using the designated "Ext Start Input adaptor" when using external triggering can cause permanent damage to the tracker and void the warranty [18, 19].
Error
Multi-Tracker Chain Limit
For systems with 4–8 trackers, a 75-ohm terminator is required on the last slave tracker to prevent signal bouncing (false triggers) [20, 21].
5. Vibrator and Accessory Limits
The system supports tactile feedback via vibrators on specific markers (OctKMarkers/SIKMarkers).
Classification
Operational Limitation
Details & Source
Error
Vibrator Queue Blocking
Vibrators activate in a First-Come-First-Served order. If the first vibrator ID in the queue is not present in the active Target Flashing Sequence (TFS), it will never flash/activate, and none of the subsequent vibrators in the queue will activate [22].
Error
Vibrator Queue Capacity
The Vibrator Activation Sequence (VAS) is limited to accumulating a maximum of 64 vibrator IDs [23].
Error
Analog Input Clipping
In VZDaq, selecting a small input voltage range for a large signa1l will c2ause the signal to be cut off, resulting in data loss [24].