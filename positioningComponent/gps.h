#ifndef GPS_H
#define GPS_H

#include "legato.h"


/*
 * type defintions
 */

// Record GPS 3d location
struct Location3d
{
    double latitude;
    double longitude;
    double hAccuracy;
    double altitude;
    double vAccuracy;
};

struct TimeGps
{
    uint16_t hours;
    uint16_t minutes;
    uint16_t seconds;
    uint16_t milliseconds;
};

struct DateGps
{
    uint16_t year;
    uint16_t month;
    uint16_t day;
};

struct GpsFix
{
    struct Location3d loc;
    struct TimeGps time;
    struct DateGps date;
};


LE_SHARED le_result_t mangOH_ReadGps(
    struct GpsFix* fix       ///< Pointer to GPS fix structure
);

#endif // GPS_H
