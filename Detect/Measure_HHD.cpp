#include "Measure_HHD.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <cstring>
#include <map>
#include <sstream>

// --------------------------------------------------------------------------
// Internal helpers and constants
// --------------------------------------------------------------------------
namespace
{
    // Every record from the tracker is exactly 19 bytes (PTI Section 4.3)
    const int RECORD_SIZE                     = 19;

    // ACK response size (same as record size — PTI Section 4.4)
    const int ACK_SIZE                        = 19;

    // Timing defaults observed in the IRP capture
    const uint32_t DEFAULT_SAMPLING_PERIOD_US = 115; // per-marker sampling period (μs)

    // Timeouts for the command/response cycle
    const int CMD_ACK_TIMEOUT_MS              = 500;  // max wait for ACK after a command
    const int CMD_ACK_POLL_MS                 = 1;    // poll interval while waiting for ACK
    const int CMD_ACK_MAX_RETRIES             = 10;   // max non-ACK records to skip before giving up
    const int STOP_GAP_MS                     = 1500; // gap between first and second stop command
    const int FETCH_READ_TIMEOUT_MS           = 5;    // short timeout for non-blocking fetch reads
    const int RESET_POLL_MS                   = 10;   // poll interval while waiting for device after reset
    const int RESET_SILENCE_THRESHOLD_MS      = 300;  // require this much silence after reset before proceeding
    const int RESET_MIN_BOOT_MS               = 1700; // minimum boot time — VZSoft waits ~1.7s after software reset

    // --------------------------------------------------------------------------
    // Build a PTI command buffer
    // --------------------------------------------------------------------------
    // Format: & <code> <index> <bytesPerParam> <numParams> CR [param data]
    std::vector<uint8_t> BuildCommand(char code, char index, char bytesPerParam, char numParams, const uint8_t *paramData = nullptr, int paramLen = 0)
    {
        std::vector<uint8_t> cmd;
        cmd.push_back(0x26); // '&'
        cmd.push_back(static_cast<uint8_t>(code));
        cmd.push_back(static_cast<uint8_t>(index));
        cmd.push_back(static_cast<uint8_t>(bytesPerParam));
        cmd.push_back(static_cast<uint8_t>(numParams));
        cmd.push_back(0x0D); // CR
        if (paramData && paramLen > 0)
        {
            cmd.insert(cmd.end(), paramData, paramData + paramLen);
        }
        return cmd;
    }

    // --------------------------------------------------------------------------
    // Send a command and wait for the 19-byte ACK (Message Set)
    // --------------------------------------------------------------------------
    // Returns true if ACK was received and the command code echo matches.
    // For commands that generate no ACK (e.g., &3 START), use sendOnly=true.
    bool SendCommand(HANDLE hPort, const std::vector<uint8_t> &cmd, bool sendOnly = false)
    {
        // Purge RX buffer before sending (matches IRP capture pattern)
        PurgeComm(hPort, PURGE_RXCLEAR);

        DWORD bytesWritten = 0;
        if (!WriteFile(hPort, cmd.data(), static_cast<DWORD>(cmd.size()), &bytesWritten, NULL) || bytesWritten != cmd.size())
        {
            std::cerr << "  [Measure] WriteFile failed (error " << GetLastError() << ")" << std::endl;
            return false;
        }

        if (sendOnly)
            return true;

        // Poll COMMSTATUS waiting for ACK data (≥19 bytes in RX queue)
        DWORD   errors  = 0;
        COMSTAT comstat = {};
        int     elapsed = 0;

        while (elapsed < CMD_ACK_TIMEOUT_MS)
        {
            ClearCommError(hPort, &errors, &comstat);
            if (comstat.cbInQue >= ACK_SIZE)
                break;
            Sleep(CMD_ACK_POLL_MS);
            elapsed += CMD_ACK_POLL_MS;
        }

        if (comstat.cbInQue < ACK_SIZE)
        {
            std::cerr << "  [Measure] ACK timeout for command 0x" << std::hex << (int)cmd[1] << std::dec << " (got " << comstat.cbInQue << " bytes)"
                      << std::endl;
            return false;
        }

        // Read the ACK — if we get stale measurement data instead of the
        // expected command echo, skip it and retry.  The device may still be
        // streaming records from a retained TFS after a software reset.
        for (int ackRetry = 0; ackRetry <= CMD_ACK_MAX_RETRIES; ackRetry++)
        {
            uint8_t ackBuf[ACK_SIZE] = {};
            DWORD   bytesRead        = 0;
            if (!ReadFile(hPort, ackBuf, ACK_SIZE, &bytesRead, NULL) || bytesRead < ACK_SIZE)
            {
                std::cerr << "  [Measure] ReadFile ACK failed (error " << GetLastError() << ")" << std::endl;
                return false;
            }

            // Validate: byte 0 should echo the command code
            if (ackBuf[0] == cmd[1])
                return true;

            if (ackRetry < CMD_ACK_MAX_RETRIES)
            {
                std::cout << "  [Measure] Skipping non-ACK data (got 0x" << std::hex << (int)ackBuf[0] << ", expected 0x" << (int)cmd[1] << std::dec
                          << "), retry " << (ackRetry + 1) << "/" << CMD_ACK_MAX_RETRIES << std::endl;

                // Wait for the next 19 bytes to arrive
                errors  = 0;
                comstat = {};
                elapsed = 0;
                while (elapsed < CMD_ACK_TIMEOUT_MS)
                {
                    ClearCommError(hPort, &errors, &comstat);
                    if (comstat.cbInQue >= ACK_SIZE)
                        break;
                    Sleep(CMD_ACK_POLL_MS);
                    elapsed += CMD_ACK_POLL_MS;
                }
                if (comstat.cbInQue < ACK_SIZE)
                {
                    std::cerr << "  [Measure] ACK timeout after skipping stale data" << std::endl;
                    return false;
                }
            }
        }

        std::cerr << "  [Measure] ACK not found after " << CMD_ACK_MAX_RETRIES << " retries for command 0x" << std::hex << (int)cmd[1] << std::dec << std::endl;
        return false;
    }

    // --------------------------------------------------------------------------
    // Encode a uint32_t as 4 bytes, MSB first (big-endian)
    // --------------------------------------------------------------------------
    void EncodeBE32(uint8_t *out, uint32_t val)
    {
        out[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
        out[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
        out[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
        out[3] = static_cast<uint8_t>(val & 0xFF);
    }

    // --------------------------------------------------------------------------
    // Decode a big-endian unsigned 32-bit value
    // --------------------------------------------------------------------------
    uint32_t DecodeBE32(const uint8_t *buf)
    {
        return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) | (static_cast<uint32_t>(buf[2]) << 8) |
               static_cast<uint32_t>(buf[3]);
    }

    // --------------------------------------------------------------------------
    // Decode a big-endian signed 24-bit value (sign-extended to int32_t)
    // --------------------------------------------------------------------------
    int32_t DecodeBE24Signed(const uint8_t *buf)
    {
        int32_t val = (static_cast<int32_t>(buf[0]) << 16) | (static_cast<int32_t>(buf[1]) << 8) | static_cast<int32_t>(buf[2]);
        if (val & 0x800000)
            val |= static_cast<int32_t>(0xFF000000); // sign-extend
        return val;
    }

    // --------------------------------------------------------------------------
    // Parse a single 19-byte record into a measurement sample
    // --------------------------------------------------------------------------
    HHD_MeasurementSample ParseRecord(const uint8_t *rec)
    {
        HHD_MeasurementSample s = {};
        s.timestamp_us          = DecodeBE32(&rec[0]);                // bytes 1-4
        s.x_mm                  = DecodeBE24Signed(&rec[4]) / 100.0;  // bytes 5-7
        s.y_mm                  = DecodeBE24Signed(&rec[7]) / 100.0;  // bytes 8-10
        s.z_mm                  = DecodeBE24Signed(&rec[10]) / 100.0; // bytes 11-13
        s.status                = DecodeBE32(&rec[13]);               // bytes 14-17
        s.ledId                 = rec[17] & 0x7F;                     // byte 18, bits 6-0
        s.tcmId                 = rec[18] & 0x0F;                     // byte 19, bits 3-0

        // Decode status word fields (bytes 14-17, 0-indexed: rec[13]-rec[16])
        // Byte 14: E|HHH|mmmm
        uint8_t b14        = rec[13];
        s.endOfFrame       = (b14 >> 7) & 1;
        s.coordStatus      = (b14 >> 4) & 0x07;
        s.ambientLight     = b14 & 0x0F;

        // Byte 15: ???|La|AAAA (right eye)
        uint8_t b15        = rec[14];
        s.rightEyeSignal   = (b15 >> 4) & 0x01;
        s.rightEyeStatus   = b15 & 0x0F;

        // Byte 16: TTT|Lb|BBBB (center eye + trigger high bits)
        uint8_t b16        = rec[15];
        s.centerEyeSignal  = (b16 >> 4) & 0x01;
        s.centerEyeStatus  = b16 & 0x0F;

        // Byte 17: TTT|Lc|CCCC (left eye + trigger low bits)
        uint8_t b17        = rec[16];
        s.leftEyeSignal    = (b17 >> 4) & 0x01;
        s.leftEyeStatus    = b17 & 0x0F;

        // Trigger index: upper 3 bits of byte 16 + upper 3 bits of byte 17
        s.triggerIndex     = ((b16 >> 5) & 0x07) << 3 | ((b17 >> 5) & 0x07);

        return s;
    }

    // --------------------------------------------------------------------------
    // Wait for the device to become ready after a software reset.
    // The device may stream retained measurement data after rebooting, so we
    // drain ALL incoming data and wait for a sustained period of silence
    // before returning.  This prevents stale data from colliding with the
    // first configuration command ACK.
    // --------------------------------------------------------------------------
    bool WaitForDeviceReady(HANDLE hPort, int timeoutMs)
    {
        DWORD   errors   = 0;
        COMSTAT comstat  = {};
        int     elapsed  = 0;
        int     silentMs = 0;
        bool    sawData  = false;

        while (elapsed < timeoutMs)
        {
            ClearCommError(hPort, &errors, &comstat);
            if (comstat.cbInQue > 0)
            {
                if (!sawData)
                    std::cout << "  [Measure] Device responding after " << elapsed << "ms — draining" << std::endl;
                sawData = true;
                PurgeComm(hPort, PURGE_RXCLEAR);
                silentMs = 0; // reset silence counter — more data may follow
            }
            else
            {
                silentMs += RESET_POLL_MS;
                // Require sustained silence AND a minimum boot time
                if (silentMs >= RESET_SILENCE_THRESHOLD_MS && elapsed >= RESET_MIN_BOOT_MS)
                {
                    std::cout << "  [Measure] Device ready after " << elapsed << "ms (" << silentMs << "ms silence)" << std::endl;
                    PurgeComm(hPort, PURGE_RXCLEAR);
                    return true;
                }
            }
            Sleep(RESET_POLL_MS);
            elapsed += RESET_POLL_MS;
        }

        std::cout << "  [Measure] Reset timeout (" << timeoutMs << "ms) — proceeding anyway" << std::endl;
        PurgeComm(hPort, PURGE_RXCLEAR);
        return false;
    }

} // anonymous namespace

// --------------------------------------------------------------------------
// HHD_MeasurementSession definition (must be after anonymous namespace)
// --------------------------------------------------------------------------
struct HHD_MeasurementSession
{
    HANDLE                       hPort;
    int                          frequencyHz;
    std::vector<HHD_MarkerEntry> markers;
    std::vector<uint8_t>         residual;
};

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

HHD_MeasurementSession *StartMeasurement(HANDLE hPort, int frequencyHz, const std::vector<HHD_MarkerEntry> &markers, int resetTimeoutMs)
{
    if (markers.empty())
    {
        std::cerr << "[Measure] No markers specified" << std::endl;
        return nullptr;
    }

    // Clamp frequency to valid range
    if (frequencyHz < 1)
        frequencyHz = 1;
    if (frequencyHz > 4600)
        frequencyHz = 4600;

    // Count total samples per frame (sum of all flash counts)
    uint32_t totalFlashes = 0;
    for (const auto &m : markers)
        totalFlashes += m.flashCount;

    std::cout << "[Measure] Starting measurement: " << frequencyHz << " Hz, " << markers.size() << " markers (" << totalFlashes << " flashes/frame)"
              << std::endl;

    // Set timeouts for command/response phase
    COMMTIMEOUTS timeouts                = {};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = CMD_ACK_TIMEOUT_MS;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hPort, &timeouts);

    // Purge all buffers for clean state
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // Wait for any Initial Message the device sends after DTR assertion / port open,
    // then drain it so it doesn't collide with the first command ACK.
    Sleep(300);
    {
        DWORD   drainErrors  = 0;
        COMSTAT drainStat    = {};
        ClearCommError(hPort, &drainErrors, &drainStat);
        if (drainStat.cbInQue > 0)
        {
            std::cout << "  [Measure] Draining " << drainStat.cbInQue << " bytes (Initial Message)" << std::endl;
            std::vector<uint8_t> drain(drainStat.cbInQue);
            DWORD bytesRead = 0;
            ReadFile(hPort, drain.data(), drainStat.cbInQue, &bytesRead, NULL);
        }
        PurgeComm(hPort, PURGE_RXCLEAR);
    }

    // --- Configuration sequence (replicating IRP capture) ---

    // 0. Pre-reset STOP: halt any measurement from a previous session.
    //    The device retains its TFS across software resets, so it may resume
    //    streaming data immediately after reboot.  Sending &5 first ensures
    //    the device is idle before we reset.
    std::cout << "  [Measure] Sending pre-reset STOP (&5)" << std::endl;
    auto cmdPreStop = BuildCommand('5', '0', '0', '0');
    SendCommand(hPort, cmdPreStop, /*sendOnly=*/true); // ignore result — device may not be running
    Sleep(100);
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // 1. Software Reset: &` 000
    //    This command does NOT generate an ACK — the device reboots.
    //    VZSoft waits ~1.7s after sending this before continuing.
    std::cout << "  [Measure] Sending Software Reset (&`)" << std::endl;
    auto cmdReset = BuildCommand('`', '0', '0', '0');
    if (!SendCommand(hPort, cmdReset, /*sendOnly=*/true))
    {
        std::cerr << "  [Measure] Software Reset send failed" << std::endl;
        return nullptr;
    }

    // Wait for device to reboot — returns early if the device sends data
    WaitForDeviceReady(hPort, resetTimeoutMs);

    // 2. Set timing: &v 042 + [sampling_period(4)] + [intermission(4)]
    //    The intermission is computed so that:
    //    frame_period = totalFlashes * sampling_period + intermission = 1e6/freq
    uint32_t samplingPeriod_us = DEFAULT_SAMPLING_PERIOD_US;
    uint32_t framePeriod_us    = 1000000 / static_cast<uint32_t>(frequencyHz);
    uint32_t activeTime_us     = totalFlashes * samplingPeriod_us;
    uint32_t intermission_us   = (framePeriod_us > activeTime_us) ? (framePeriod_us - activeTime_us) : 0;

    uint8_t timingParams[8];
    EncodeBE32(&timingParams[0], samplingPeriod_us);
    EncodeBE32(&timingParams[4], intermission_us);

    std::cout << "  [Measure] Setting timing: period=" << samplingPeriod_us << "us, intermission=" << intermission_us << "us" << std::endl;
    auto cmdTiming = BuildCommand('v', '0', '4', '2', timingParams, 8);
    if (!SendCommand(hPort, cmdTiming))
        return nullptr;

    // 3. Signal Quality (SQR): &L 011 + 0x02
    uint8_t sqrParam = 0x02;
    auto    cmdSQR   = BuildCommand('L', '0', '1', '1', &sqrParam, 1);
    if (!SendCommand(hPort, cmdSQR))
        return nullptr;

    // 4. Min Signal (MSR): &O 021 + 0x00 0x02
    uint8_t msrParams[] = {0x00, 0x02};
    auto    cmdMSR      = BuildCommand('O', '0', '2', '1', msrParams, 2);
    if (!SendCommand(hPort, cmdMSR))
        return nullptr;

    // 5. Exposure Gain: &Y A11 + 0x08
    uint8_t gainParam = 0x08;
    auto    cmdGain   = BuildCommand('Y', 'A', '1', '1', &gainParam, 1);
    if (!SendCommand(hPort, cmdGain))
        return nullptr;

    // 6. SOT Limit: &U 011 + 0x03
    uint8_t sotParam = 0x03;
    auto    cmdSOT   = BuildCommand('U', '0', '1', '1', &sotParam, 1);
    if (!SendCommand(hPort, cmdSOT))
        return nullptr;

    // 7. Tether Mode: &^ 011 + 0x0D
    uint8_t tetherParam = 0x0D;
    auto    cmdTether   = BuildCommand('^', '0', '1', '1', &tetherParam, 1);
    if (!SendCommand(hPort, cmdTether))
        return nullptr;

    // 8. Single Sampling: &Q A00
    auto cmdSingleSamp = BuildCommand('Q', 'A', '0', '0');
    if (!SendCommand(hPort, cmdSingleSamp))
        return nullptr;

    // 9. Clear TFS: &p 000
    std::cout << "  [Measure] Programming TFS (" << markers.size() << " markers across TCMs)" << std::endl;
    auto cmdClearTFS = BuildCommand('p', '0', '0', '0');
    if (!SendCommand(hPort, cmdClearTFS))
        return nullptr;

    // 10. Append each marker to TFS: &p {tcmId}12 + {ledId} {flashCount}
    //     Command index = TCMID ('1'-'8'), 2 params of 1 byte each
    for (const auto &m : markers)
    {
        uint8_t tcm          = (m.tcmId >= 1 && m.tcmId <= 8) ? m.tcmId : 1;
        uint8_t led          = (m.ledId >= 1 && m.ledId <= 64) ? m.ledId : 1;
        uint8_t fc           = (m.flashCount >= 1) ? m.flashCount : 1;

        char    indexChar    = static_cast<char>('0' + tcm); // '1'-'8'
        uint8_t tfsParams[]  = {led, fc};
        auto    cmdAppendTFS = BuildCommand('p', indexChar, '1', '2', tfsParams, 2);
        if (!SendCommand(hPort, cmdAppendTFS))
            return nullptr;
    }

    // 11. Sync EOF: &o 000
    auto cmdSyncEOF = BuildCommand('o', '0', '0', '0');
    if (!SendCommand(hPort, cmdSyncEOF))
        return nullptr;

    // 12. Multi-Rate Sampling SM0: &X 018 + 8 zero bytes
    uint8_t multiRateParams[8] = {};
    auto    cmdMultiRate       = BuildCommand('X', '0', '1', '8', multiRateParams, 8);
    if (!SendCommand(hPort, cmdMultiRate))
        return nullptr;

    // 13. Upload TFS: &r 000
    auto cmdUploadTFS = BuildCommand('r', '0', '0', '0');
    if (!SendCommand(hPort, cmdUploadTFS))
        return nullptr;

    // 14. Refraction OFF: &: 000
    auto cmdRefraction = BuildCommand(':', '0', '0', '0');
    if (!SendCommand(hPort, cmdRefraction))
        return nullptr;

    // 15. Internal Trigger: &S 000
    auto cmdTrigger = BuildCommand('S', '0', '0', '0');
    if (!SendCommand(hPort, cmdTrigger))
        return nullptr;

    // --- Switch to short read timeouts for streaming ---
    timeouts.ReadIntervalTimeout        = 1;
    timeouts.ReadTotalTimeoutConstant   = FETCH_READ_TIMEOUT_MS;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hPort, &timeouts);

    // --- START: &3 000 (no ACK generated) ---
    std::cout << "  [Measure] Sending START (&3)" << std::endl;
    auto cmdStart = BuildCommand('3', '0', '0', '0');
    if (!SendCommand(hPort, cmdStart, /*sendOnly=*/true))
        return nullptr;

    std::cout << "[Measure] Measurement started" << std::endl;

    // Allocate session
    HHD_MeasurementSession *session = new HHD_MeasurementSession();
    session->hPort                  = hPort;
    session->frequencyHz            = frequencyHz;
    session->markers                = markers;
    return session;
}

int FetchMeasurements(HHD_MeasurementSession *session, std::vector<HHD_MeasurementSample> &samples)
{
    if (!session)
        return 0;

    // Check how many bytes are available in the RX queue
    DWORD   errors  = 0;
    COMSTAT comstat = {};
    ClearCommError(session->hPort, &errors, &comstat);

    if (comstat.cbInQue == 0 && session->residual.empty())
        return 0; // nothing to read

    // Read all available bytes
    int newSamples = 0;

    if (comstat.cbInQue > 0)
    {
        std::vector<uint8_t> readBuf(comstat.cbInQue);
        DWORD                bytesRead = 0;

        if (!ReadFile(session->hPort, readBuf.data(), static_cast<DWORD>(readBuf.size()), &bytesRead, NULL))
            return 0;

        readBuf.resize(bytesRead);

        // Prepend any residual bytes from the previous call
        if (!session->residual.empty())
        {
            session->residual.insert(session->residual.end(), readBuf.begin(), readBuf.end());
            readBuf.swap(session->residual);
            session->residual.clear();
        }

        // Parse complete 19-byte records
        size_t offset = 0;
        while (offset + RECORD_SIZE <= readBuf.size())
        {
            samples.push_back(ParseRecord(&readBuf[offset]));
            newSamples++;
            offset += RECORD_SIZE;
        }

        // Save any trailing partial record for the next call
        if (offset < readBuf.size())
        {
            session->residual.assign(readBuf.begin() + offset, readBuf.end());
        }
    }
    else if (!session->residual.empty())
    {
        // Only residual data, no new bytes — check if we have a complete record
        if (session->residual.size() >= RECORD_SIZE)
        {
            size_t offset = 0;
            while (offset + RECORD_SIZE <= session->residual.size())
            {
                samples.push_back(ParseRecord(&session->residual[offset]));
                newSamples++;
                offset += RECORD_SIZE;
            }
            if (offset < session->residual.size())
            {
                session->residual.erase(session->residual.begin(), session->residual.begin() + offset);
            }
            else
            {
                session->residual.clear();
            }
        }
    }

    return newSamples;
}

// Send STOP (&5) and drain streaming data until the ACK arrives.
// During active measurement the device pipeline may contain many queued
// records, so we read in a time-bounded loop rather than using the
// generic SendCommand retry logic.
static bool SendStopAndDrain(HANDLE hPort, int timeoutMs)
{
    // Drain any data already in the host buffer
    PurgeComm(hPort, PURGE_RXCLEAR);

    // Send &5
    auto  cmdStop      = BuildCommand('5', '0', '0', '0');
    DWORD bytesWritten = 0;
    if (!WriteFile(hPort, cmdStop.data(), static_cast<DWORD>(cmdStop.size()), &bytesWritten, NULL))
        return false;

    // Read 19-byte records until we find the ACK or timeout.
    // Use GetTickCount64 for accurate wall-clock timing — the loop must
    // not spin forever even when data is flowing continuously.
    ULONGLONG startTick = GetTickCount64();

    while ((GetTickCount64() - startTick) < static_cast<ULONGLONG>(timeoutMs))
    {
        DWORD   errors  = 0;
        COMSTAT comstat = {};
        ClearCommError(hPort, &errors, &comstat);

        if (comstat.cbInQue >= ACK_SIZE)
        {
            uint8_t buf[ACK_SIZE] = {};
            DWORD   bytesRead     = 0;
            if (ReadFile(hPort, buf, ACK_SIZE, &bytesRead, NULL) && bytesRead >= ACK_SIZE)
            {
                if (buf[0] == 0x35 && buf[1] == 0x30) // '5' '0' = STOP ACK
                    return true;
                // Measurement data — discard and keep reading
            }
        }
        else
        {
            Sleep(CMD_ACK_POLL_MS);
        }
    }

    return false;
}

bool StopMeasurement(HHD_MeasurementSession *session)
{
    if (!session)
        return false;

    std::cout << "[Measure] Stopping measurement" << std::endl;

    // Restore longer timeouts for the stop phase
    COMMTIMEOUTS timeouts                = {};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = CMD_ACK_TIMEOUT_MS;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(session->hPort, &timeouts);

    // Send STOP and drain streaming data until ACK
    std::cout << "  [Measure] Sending STOP (&5) — attempt 1" << std::endl;
    bool ack1 = SendStopAndDrain(session->hPort, 2000);

    if (!ack1)
    {
        // Retry after a gap (matches IRP capture pattern)
        Sleep(STOP_GAP_MS);
        std::cout << "  [Measure] Sending STOP (&5) — attempt 2" << std::endl;
        SendStopAndDrain(session->hPort, 2000);
    }

    // Drain any remaining measurement data from the RX buffer
    PurgeComm(session->hPort, PURGE_RXCLEAR);

    std::cout << "[Measure] Measurement stopped" << std::endl;

    // Free session
    delete session;

    return true;
}

// ---------------------------------------------------------------------------
// ConfigDetect — automatic marker & TCM discovery via probe measurement
// ---------------------------------------------------------------------------

HHD_ConfigDetectResult ConfigDetect(HANDLE hPort, const HHD_ConfigDetectOptions &options)
{
    HHD_ConfigDetectResult result = {};
    result.success                = false;

    int maxTcm = (options.maxTcmId >= 1 && options.maxTcmId <= 8) ? options.maxTcmId : 8;
    int maxLed = (options.maxLedId >= 1 && options.maxLedId <= 64) ? options.maxLedId : 16;

    // Build candidate marker list: all combinations of TCM 1..maxTcm × LED 1..maxLed
    std::vector<HHD_MarkerEntry> candidates;
    for (int tcm = 1; tcm <= maxTcm; tcm++)
        for (int led = 1; led <= maxLed; led++)
            candidates.push_back({static_cast<uint8_t>(tcm), static_cast<uint8_t>(led), 1});

    std::cout << "[ConfigDetect] Probing " << candidates.size() << " candidate markers"
              << " (TCM 1-" << maxTcm << ", LED 1-" << maxLed << ")" << std::endl;

    // Start a probe measurement session
    HHD_MeasurementSession *session = StartMeasurement(hPort, options.probeFreqHz, candidates);
    if (!session)
    {
        result.summary = "Failed to start probe measurement";
        std::cerr << "[ConfigDetect] " << result.summary << std::endl;
        return result;
    }

    // --- Warm-up phase: discard data while the tracker adjusts auto-exposure ---
    std::cout << "[ConfigDetect] Warm-up: discarding data for " << options.warmupMs << "ms" << std::endl;
    {
        ULONGLONG warmupStart = GetTickCount64();
        std::vector<HHD_MeasurementSample> discarded;
        while ((GetTickCount64() - warmupStart) < static_cast<ULONGLONG>(options.warmupMs))
        {
            discarded.clear();
            FetchMeasurements(session, discarded);
            Sleep(10);
        }
    }

    // --- Evaluation phase: collect data and classify markers ---
    std::cout << "[ConfigDetect] Evaluating for " << options.evalMs << "ms" << std::endl;

    // Per-marker statistics keyed by (tcmId << 8 | ledId)
    struct ProbeStats
    {
        int framesTotal   = 0;
        int framesValid   = 0; // coordStatus==0 AND at least one eye has signal
        int framesAllLow  = 0; // all three eyes report signal low
        int framesCoordOk = 0; // coordStatus==0 (regardless of signal)
    };
    std::map<uint16_t, ProbeStats> stats;

    {
        ULONGLONG evalStart = GetTickCount64();
        std::vector<HHD_MeasurementSample> samples;
        while ((GetTickCount64() - evalStart) < static_cast<ULONGLONG>(options.evalMs))
        {
            samples.clear();
            FetchMeasurements(session, samples);
            for (const auto &s : samples)
            {
                uint16_t key = (static_cast<uint16_t>(s.tcmId) << 8) | s.ledId;
                auto    &st  = stats[key];
                st.framesTotal++;

                if (s.coordStatus == 0)
                    st.framesCoordOk++;

                bool allEyesOk = (s.rightEyeStatus == 0) &&
                                 (s.centerEyeStatus == 0) &&
                                 (s.leftEyeStatus == 0);
                if (!allEyesOk)
                    st.framesAllLow++;

                // A sample is "valid" only if all three eye statuses are 0
                // (no anomaly on any lens).
                if (allEyesOk)
                    st.framesValid++;
            }
            Sleep(5);
        }
    }

    // Stop the probe measurement
    StopMeasurement(session);
    session = nullptr;

    // --- Classify markers ---
    // Group by TCM, then filter by detection threshold.
    // A marker is considered "present" if a sufficient fraction of eval
    // frames had coordStatus==0 AND at least one camera eye saw the signal.
    std::map<uint8_t, std::vector<HHD_DetectedMarker>> tcmMarkers;
    int totalDetected = 0;

    // Diagnostic: print per-marker stats
    std::cout << "[ConfigDetect] Per-marker evaluation results:" << std::endl;
    for (const auto &[key, st] : stats)
    {
        uint8_t tcm = static_cast<uint8_t>(key >> 8);
        uint8_t led = static_cast<uint8_t>(key & 0xFF);

        double validRate   = (st.framesTotal > 0) ? static_cast<double>(st.framesValid) / st.framesTotal : 0.0;
        double allLowRate  = (st.framesTotal > 0) ? static_cast<double>(st.framesAllLow) / st.framesTotal : 0.0;

        // Only print markers that have some valid frames (reduce noise)
        if (st.framesValid > 0 || st.framesAllLow < st.framesTotal)
        {
            std::cout << "  TCM" << (int)tcm << " LED" << std::setw(2) << (int)led
                      << "  total=" << st.framesTotal
                      << "  valid=" << st.framesValid
                      << "  coordOk=" << st.framesCoordOk
                      << "  allEyesLow=" << st.framesAllLow
                      << "  rate=" << std::fixed << std::setprecision(0) << (validRate * 100) << "%"
                      << std::endl;
        }

        if (st.framesTotal < options.minFrames)
            continue;

        if (validRate >= options.detectionThreshold)
        {
            HHD_DetectedMarker dm = {};
            dm.tcmId              = tcm;
            dm.ledId              = led;
            dm.framesDetected     = st.framesValid;
            dm.framesTotal        = st.framesTotal;
            dm.detectionRate      = validRate;
            tcmMarkers[tcm].push_back(dm);
            totalDetected++;
        }
    }

    // Build result
    result.success = true;
    std::ostringstream summaryStream;
    summaryStream << "Found " << tcmMarkers.size() << " TCM(s): ";

    bool first = true;
    for (auto &[tcmId, markers] : tcmMarkers)
    {
        // Sort markers by LED ID
        std::sort(markers.begin(), markers.end(),
                  [](const HHD_DetectedMarker &a, const HHD_DetectedMarker &b) { return a.ledId < b.ledId; });

        HHD_DetectedTCM dtcm;
        dtcm.tcmId   = tcmId;
        dtcm.markers = markers;
        result.tcms.push_back(dtcm);

        // Add to flat marker list
        for (const auto &dm : markers)
            result.markerList.push_back({dm.tcmId, dm.ledId, 1});

        // Build summary
        if (!first)
            summaryStream << ", ";
        first = false;
        summaryStream << "TCM" << (int)tcmId << " (LED";
        if (markers.size() == 1)
        {
            summaryStream << " " << (int)markers[0].ledId;
        }
        else
        {
            summaryStream << "s";
            for (size_t i = 0; i < markers.size(); i++)
            {
                summaryStream << (i == 0 ? " " : ",") << (int)markers[i].ledId;
            }
        }
        summaryStream << ")";
    }
    summaryStream << " — " << totalDetected << " marker(s) total";
    result.summary = summaryStream.str();

    std::cout << "[ConfigDetect] " << result.summary << std::endl;
    return result;
}
