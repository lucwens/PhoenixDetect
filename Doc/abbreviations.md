# Abbreviations

## Hardware and Markers

| Abbreviation | Full Name | Description |
|---|---|---|
| PTI | PhoeniX Technologies Incorporated | The manufacturer of the Visualeyez system. |
| TCM | Target Control Module | The hardware unit that powers and controls standard LED markers. |
| TCMID | Target Control Module Identifier | A unique ID (1-8) assigned to a group of 64 markers. |
| TCMK | K-series Target Control Module | Specific models include TCMK8, TCMK16, and TCMK32 depending on the number of connectors. |
| LEDID | LED Identifier | The specific ID (1-64) of a marker within a TCM group. |
| SIK | Self-Identified K-series Marker | A wireless or smart marker that has its own fixed ID. |
| OctKMarker | Octopus K-series Marker | A semi-wireless marker type. |
| OctBatt | Octopus Battery | The battery unit for Octopus markers. |
| OctRx | Octopus Receiver | The receiver unit for Octopus markers. |
| VIBID | Vibrator Identifier | The ID used to trigger a vibrator on specific markers (usually the same as the marker's LEDID). |

## Timing, Sequencing, and Operation

| Abbreviation | Full Name | Description |
|---|---|---|
| SOT | Sample Operation Time | A configurable parameter (2-15) that limits the maximum time the tracker spends sampling a single target's light signal. |
| TFS | Target Flashing Sequence | The programmed order in which markers flash to be sensed by the tracker. |
| VAS | Vibrator Activation Sequence | A queue of vibrator IDs to be activated sequentially. |
| SM0-SM3 | Sampling Modes | Multi-rate sampling settings that determine how many times a marker flashes per capture frame (e.g., SM0 is once, SM3 is up to eight times). |
| EOF | End Of Frame | A pulse signal generated at the end of every motion frame, often used to trigger analog data collection. |
| MTS | Multi-Tracker System | A configuration where multiple trackers are synchronized via external triggers. |
| CRF | Coordinate Reference Frame | The 3D coordinate space origin/alignment, usually defined by the master tracker. |

## Signal Quality and Control

| Abbreviation | Full Name | Description |
|---|---|---|
| SQR | Signal-Quality-Requirement | A coarse test parameter setting the minimum signal quality level required for data to be considered valid. |
| MSR | Minimum-Signal-Requirement | The minimum peak signal value required before the system computes a marker's position. |
| TTL | Transistor-Transistor Logic | A specific voltage signal standard required for external triggering (improper signals can damage the tracker). |

## Software, Data, and Communication

| Abbreviation | Full Name | Description |
|---|---|---|
| ACK | Acknowledgement | A message sent by the tracker to the host confirming a command was received and executed. |
| ERR / BEL | Error / Bell | A message sent by the tracker indicating an invalid command. |
| MSB | Most Significant Byte | The byte with the highest value in a multi-byte number. |
| LSB | Least Significant Byte (or Bit) | The byte or bit with the lowest value. |
| CR | Carriage Return | The ASCII character (0Dh) used to indicate the end of a command string. |
| DAQ | Data Acquisition | Refers to the collection of analog data (e.g., EMG, force plates) alongside motion data. |
| NI | National Instruments | The manufacturer of the DAQ cards supported by the VZDaq software. |
| DIFF | Differential | An analog input mode for the DAQ card. |
| RSE | Referenced Single-Ended | An analog input mode for the DAQ card. |
| NRSE | Non-Referenced Single-Ended | An analog input mode for the DAQ card. |
