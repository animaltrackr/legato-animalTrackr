#include "legato.h"
#include "interfaces.h"

#include "gps.h"

le_result_t mangOH_ReadGps(
    struct GpsFix* fix
)
{
    int32_t latitudeReading;
    int32_t longitudeReading;
    int32_t hAccuracyReading;
    int32_t altitudeReading;
    int32_t vAccuracyReading;

    uint16_t hoursReading;
    uint16_t minutesReading;
    uint16_t secondsReading;
    uint16_t millisecondsReading;

    uint16_t yearReading;
    uint16_t monthReading;
    uint16_t dayReading;


    le_result_t res = le_pos_Get3DLocation(
        &latitudeReading,
        &longitudeReading,
        &hAccuracyReading,
        &altitudeReading,
        &vAccuracyReading);
    if (res == LE_OUT_OF_RANGE) {
        LE_WARN("GPS fix not available");
        return res;
    }
    LE_ASSERT_OK(res);
    res = le_pos_GetTime(
        &hoursReading,
        &minutesReading,
        &secondsReading,
        &millisecondsReading);
    if (res == LE_OUT_OF_RANGE) {
        LE_WARN("GPS fix not available");
        return res;
    }
    LE_ASSERT_OK(res);
    res = le_pos_GetDate(
        &yearReading,
        &monthReading,
        &dayReading);
    if (res == LE_OUT_OF_RANGE) {
        LE_WARN("timeRes not ok: %d", res);
        return res;
    }
    LE_ASSERT_OK(res);

    LE_ERROR_IF(fix == NULL, "Output variable is null");

    if (res == LE_OK) {
        fix->loc.latitude  = latitudeReading / 1000000.0;
        fix->loc.longitude = longitudeReading / 1000000.0;
        fix->loc.hAccuracy = (double)hAccuracyReading;
        fix->loc.altitude  = altitudeReading / 1000.0;
        fix->loc.vAccuracy = (double)vAccuracyReading;

        fix->time.hours = hoursReading;
        fix->time.minutes = minutesReading;
        fix->time.seconds = secondsReading;
        fix->time.milliseconds = millisecondsReading;

        fix->date.year = yearReading;
        fix->date.month = monthReading;
        fix->date.day = dayReading;
    }

    return res;
}

COMPONENT_INIT
{
    le_posCtrl_ActivationRef_t posCtrlRef = le_posCtrl_Request();
    LE_FATAL_IF(posCtrlRef == NULL, "Couldn't activate positioning service");
    le_result_t suplRes = le_gnss_SetSuplAssistedMode(LE_GNSS_MS_BASED_MODE);
    if (suplRes != LE_OK) {
        LE_WARN("Couldn't set A-GPS MS-Based mode, running in standalone");
        LE_ASSERT_OK(le_gnss_SetSuplAssistedMode(LE_GNSS_STANDALONE_MODE));
    }
}
