#include "legato.h"
#include "interfaces.h"
#include "gps.h"

/*
 * static function declarations
 */
static void PositionTimerHandler(le_timer_Ref_t timer);
static le_result_t GpsRead(void *value);

/*
 * variable defintions
 */
static le_timer_Ref_t PositionTimer;
static uint16_t PositioningInterval = 10;

/**
 * Handler for the GPS position sampling timer
 *
 * Each time this function is called due to timer expiry the GPS location
 * will be read from the GPS receiver,
 * and any updates from the server will be received.
 */
static void PositionTimerHandler
(
        le_timer_Ref_t timer ///< Position update timer
)
{
    struct GpsFix fix;

    le_result_t gpsRes = GpsRead(&fix);

    LE_DEBUG("gpsRes: %d", gpsRes);
    if (gpsRes == LE_OK) {
        LE_DEBUG("Gps fix: datetime: %04d-%02d-%02d\n %02d:%02d:%02d:%03d\n "
                "reading: LAT: %f LONG: %f tHACC: %f ALT: %f VACC: %f",
                fix.date.year, fix.date.month, fix.date.day,
                fix.time.hours, fix.time.minutes, fix.time.seconds, fix.time.milliseconds,
                fix.loc.latitude, fix.loc.longitude, fix.loc.hAccuracy,
                fix.loc.altitude, fix.loc.vAccuracy);
    }
    
    else {
        LE_WARN("Couldn't get GPS fix: %d", gpsRes);
    }
}

/**
 * Read the GPS location
 *
 * @return
 *  LE_OK on success. Any other return value is a falure.
 */
static le_result_t GpsRead
(
    void *value ///< Pointer to a struct GpsFix to store location and datetime
)
{
    struct GpsFix *v = value;
    le_result_t res = mangOH_ReadGps(v);

    return res;
}


COMPONENT_INIT {
    PositionTimer = le_timer_Create("Position Read");
    LE_ASSERT_OK(le_timer_SetMsInterval(PositionTimer, PositioningInterval * 1000));
    LE_ASSERT_OK(le_timer_SetRepeat(PositionTimer, 0));
    LE_ASSERT_OK(le_timer_SetHandler(PositionTimer, PositionTimerHandler));
    LE_ASSERT_OK(le_timer_Start(PositionTimer));

    PositionTimerHandler(0);

}
