#include "CppUnitTest.h"
#include "../Detect/Measure_HHD.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool HasError(const std::vector<HHD_ValidationIssue> &issues, const std::string &substring)
{
    return std::any_of(issues.begin(), issues.end(), [&](const HHD_ValidationIssue &i)
                       { return i.severity == HHD_IssueSeverity::Error &&
                                i.message.find(substring) != std::string::npos; });
}

static bool HasWarning(const std::vector<HHD_ValidationIssue> &issues, const std::string &substring)
{
    return std::any_of(issues.begin(), issues.end(), [&](const HHD_ValidationIssue &i)
                       { return i.severity == HHD_IssueSeverity::Warning &&
                                i.message.find(substring) != std::string::npos; });
}

static int CountErrors(const std::vector<HHD_ValidationIssue> &issues)
{
    return static_cast<int>(std::count_if(issues.begin(), issues.end(),
                                          [](const HHD_ValidationIssue &i)
                                          { return i.severity == HHD_IssueSeverity::Error; }));
}

static int CountWarnings(const std::vector<HHD_ValidationIssue> &issues)
{
    return static_cast<int>(std::count_if(issues.begin(), issues.end(),
                                          [](const HHD_ValidationIssue &i)
                                          { return i.severity == HHD_IssueSeverity::Warning; }));
}

static HHD_MarkerEntry ValidMarker(uint8_t tcm = 1, uint8_t led = 1, uint8_t fc = 1)
{
    return {tcm, led, fc};
}

// ===========================================================================
// Error tests
// ===========================================================================

TEST_CLASS(ValidationErrors)
{
public:
    TEST_METHOD(EmptyMarkers)
    {
        auto issues = ValidateMeasurementSetup(10, {});
        Assert::IsTrue(HasError(issues, "No markers"));
    }

    TEST_METHOD(FrequencyTooLow)
    {
        auto issues = ValidateMeasurementSetup(0, {ValidMarker()});
        Assert::IsTrue(HasError(issues, "below minimum"));
    }

    TEST_METHOD(FrequencyTooHigh)
    {
        auto issues = ValidateMeasurementSetup(5000, {ValidMarker()});
        Assert::IsTrue(HasError(issues, "exceeds maximum"));
    }

    TEST_METHOD(SotTooLow)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 1);
        Assert::IsTrue(HasError(issues, "SOT"));
    }

    TEST_METHOD(SotTooHigh)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 16);
        Assert::IsTrue(HasError(issues, "SOT"));
    }

    TEST_METHOD(TcmIdZero)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(0, 1, 1)});
        Assert::IsTrue(HasError(issues, "TCM ID out of range"));
    }

    TEST_METHOD(TcmIdTooHigh)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(9, 1, 1)});
        Assert::IsTrue(HasError(issues, "TCM ID out of range"));
    }

    TEST_METHOD(LedIdZero)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(1, 0, 1)});
        Assert::IsTrue(HasError(issues, "LED ID out of range"));
    }

    TEST_METHOD(LedIdTooHigh)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(1, 65, 1)});
        Assert::IsTrue(HasError(issues, "LED ID out of range"));
    }

    TEST_METHOD(FlashCountZero)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(1, 1, 0)});
        Assert::IsTrue(HasError(issues, "flash count is 0"));
    }

    TEST_METHOD(TotalMarkersExceed512)
    {
        std::vector<HHD_MarkerEntry> markers;
        for (uint8_t tcm = 1; tcm <= 8; ++tcm)
            for (uint8_t led = 1; led <= 64; ++led)
                markers.push_back({tcm, led, 1});
        markers.push_back({1, 1, 1}); // 513th
        auto issues = ValidateMeasurementSetup(1, markers);
        Assert::IsTrue(HasError(issues, "exceeds system maximum"));
    }

    TEST_METHOD(TfsPairsPerTcmExceed64)
    {
        std::vector<HHD_MarkerEntry> markers;
        for (int i = 0; i < 65; ++i)
            markers.push_back({1, static_cast<uint8_t>((i % 64) + 1), 1});
        auto issues = ValidateMeasurementSetup(1, markers);
        Assert::IsTrue(HasError(issues, "marker entries in the TFS"));
    }

    TEST_METHOD(TfsTcmTransitionsExceed64)
    {
        // 66 alternating entries = 66 TCM transitions > 64
        std::vector<HHD_MarkerEntry> markers;
        for (int i = 0; i < 66; ++i)
            markers.push_back({static_cast<uint8_t>((i % 2) + 1), 1, 1});
        auto issues = ValidateMeasurementSetup(1, markers);
        Assert::IsTrue(HasError(issues, "TCM ID transitions"));
    }

    TEST_METHOD(SamplingTooFast)
    {
        // 100 flashes at 115us = 11500us active time.  At 4600 Hz frame period = 217us.
        std::vector<HHD_MarkerEntry> markers;
        for (int i = 0; i < 100; ++i)
            markers.push_back({1, static_cast<uint8_t>((i % 64) + 1), 1});
        auto issues = ValidateMeasurementSetup(4600, markers);
        Assert::IsTrue(HasError(issues, "Maximum achievable rate"));
    }
};

// ===========================================================================
// Warning tests
// ===========================================================================

TEST_CLASS(ValidationWarnings)
{
public:
    TEST_METHOD(LedOverheating)
    {
        auto issues = ValidateMeasurementSetup(120, {ValidMarker()});
        Assert::IsTrue(HasWarning(issues, "overheat"));
    }

    TEST_METHOD(SotBoundedRateExceeded)
    {
        // SOT=15 => maxTargetHz ~ 1736.  With 10 flashes => maxFps ~ 173.
        std::vector<HHD_MarkerEntry> markers;
        for (int i = 0; i < 10; ++i)
            markers.push_back({1, static_cast<uint8_t>(i + 1), 1});
        auto issues = ValidateMeasurementSetup(200, markers, 15);
        Assert::IsTrue(HasWarning(issues, "per-target limit"));
    }

    TEST_METHOD(LedIdGaps)
    {
        // TCM 1 has LED 1 and LED 3 but not LED 2 => gap
        std::vector<HHD_MarkerEntry> markers = {{1, 1, 1}, {1, 3, 1}};
        auto issues = ValidateMeasurementSetup(10, markers);
        Assert::IsTrue(HasWarning(issues, "gaps in LED IDs"));
    }

    TEST_METHOD(HighFlashCount)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker(1, 1, 15)});
        Assert::IsTrue(HasWarning(issues, "flash count"));
        Assert::IsTrue(HasWarning(issues, "heat load"));
    }

    TEST_METHOD(DoubleSamplingPenalty)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 3, true);
        Assert::IsTrue(HasWarning(issues, "Double Sampling"));
    }

    TEST_METHOD(DoubleSamplingReducesEffectiveRate)
    {
        // SOT=8, no double sampling => maxTargetHz ~ 26040/8 = 3255
        // With 10 flashes => maxFps ~ 325.  Request 300 Hz => no warning.
        std::vector<HHD_MarkerEntry> markers;
        for (int i = 0; i < 10; ++i)
            markers.push_back({1, static_cast<uint8_t>(i + 1), 1});
        auto issuesNormal = ValidateMeasurementSetup(300, markers, 8, false);
        Assert::IsFalse(HasWarning(issuesNormal, "per-target limit"));

        // With double sampling => effectiveSot=16, maxTargetHz ~ 26040/16 = 1627
        // With 10 flashes => maxFps ~ 162.  Request 300 Hz => warning.
        auto issuesDouble = ValidateMeasurementSetup(300, markers, 8, true);
        Assert::IsTrue(HasWarning(issuesDouble, "per-target limit"));
    }

    TEST_METHOD(TetherlessInterference)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 3, false, true);
        Assert::IsTrue(HasWarning(issues, "Tetherless"));
    }

    TEST_METHOD(ExposureGainHigh)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 3, false, false, 15);
        Assert::IsTrue(HasWarning(issues, "Exposure gain"));
    }

    TEST_METHOD(ExposureGainNormal)
    {
        auto issues = ValidateMeasurementSetup(10, {ValidMarker()}, 3, false, false, 8);
        Assert::IsFalse(HasWarning(issues, "Exposure gain"));
    }
};

// ===========================================================================
// Valid setup — no issues expected
// ===========================================================================

TEST_CLASS(ValidationValid)
{
public:
    TEST_METHOD(ValidSetup)
    {
        std::vector<HHD_MarkerEntry> markers = {{1, 1, 1}, {1, 2, 1}, {2, 1, 1}};
        auto issues = ValidateMeasurementSetup(10, markers);
        Assert::AreEqual(0, CountErrors(issues));
        Assert::AreEqual(0, CountWarnings(issues));
    }

    TEST_METHOD(ValidSetupMaxBoundary)
    {
        // 8 markers at 100 Hz, SOT=3 — well within all limits
        std::vector<HHD_MarkerEntry> markers;
        for (uint8_t led = 1; led <= 8; ++led)
            markers.push_back({1, led, 1});
        auto issues = ValidateMeasurementSetup(100, markers);
        Assert::AreEqual(0, CountErrors(issues));
        Assert::AreEqual(0, CountWarnings(issues));
    }
};
