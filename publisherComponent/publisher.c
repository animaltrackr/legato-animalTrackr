#include "legato.h"
#include "interfaces.h"
#include "le_data_interface.h"
#include <curl/curl.h>
#include "gps.h"

#define TIMEOUT_SECS (60)

#define SSL_ERROR_HELP    "Make sure your system date is set correctly (e.g. `date -s '2016-7-7'`)"
#define SSL_ERROR_HELP_2  "You can check the minimum date for this SSL cert to work using: `openssl s_client -connect httpbin.org:443 2>/dev/null | openssl x509 -noout -dates`"

/*
 * static function declarations
 */
static void PositionTimerHandler(le_timer_Ref_t timer);
static le_result_t GpsRead(void *value);
static void ConnectionStateHandler(const char *intfName, bool isConnected, void *contextPtr);
static void TimeoutHandler(le_timer_Ref_t timerRef);
static size_t PrintCallback(void *bufferPtr, size_t size, size_t nbMember, void *userData);
static void PublishGpsSample(void * sample);

/*
 * variable defintions
 */
static le_timer_Ref_t PositionTimer;
static le_data_RequestObjRef_t ConnectionRef;
static uint16_t PositioningInterval = 10;
static bool WaitingForConnection;

static const char * Url = "http://api.animaltrackr.com/animal/points";
static const char * TrackerUuid = "bc6bf7b3-b173-4355-b5d2-ac1cdb2263ad";

//--------------------------------------------------------------------------------------------------
/**
 * Callback for printing the response of a succesful request
 */
//--------------------------------------------------------------------------------------------------
static size_t PrintCallback
(
    void *bufferPtr,
    size_t size,
    size_t nbMember,
    void *userData // unused, but can be set with CURLOPT_WRITEDATA
)
{
    printf("Succesfully received data:\n");
    fwrite(bufferPtr, size, nbMember, stdout);
    return size * nbMember;
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for the timeout timer
 */
//--------------------------------------------------------------------------------------------------

static void TimeoutHandler
(
    le_timer_Ref_t timerRef
)
{
    if (WaitingForConnection)
    {
        LE_ERROR("Couldn't establish connection after " STRINGIZE(TIMEOUT_SECS) " seconds");
        exit(EXIT_FAILURE);
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for when the connection state changes
 */
//--------------------------------------------------------------------------------------------------
static void ConnectionStateHandler
(
    const char *intfName,
    bool isConnected,
    void *contextPtr
)
{
    if (isConnected)
    {
        WaitingForConnection = false;
        LE_INFO("Interface %s connected.", intfName);
    }
    else
    {
        WaitingForConnection = true;
        LE_INFO("Interface %s disconnected.", intfName);
    }

}

static void PublishGpsSample
(
    void * sample  ///< Pointer to GpsFix sample
)
{
    struct GpsFix *s = sample;
    CURL *curl;
    CURLcode res;
    char str[1000];

    snprintf(str, 1000, "{\"tracker\": \"%s\", \"timestamp\": "
            "\"%04d-%02d-%02dT%02d:%02d:%02d,%dZ\", \"geo_lat\": \"%f\","
            "\"geo_long\": \"%f\", \"geo_error_radius\": \"%f\"}",
            TrackerUuid, s->date.year, s->date.month, s->date.day,
            s->time.hours, s->time.minutes, s->time.seconds, s->time.milliseconds,
            s->loc.latitude, s->loc.longitude, s->loc.hAccuracy);

    if (!WaitingForConnection) {
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, Url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PrintCallback);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                LE_ERROR("curl_easy_perform() failed: %s", curl_easy_strerror(res));
                if (res == CURLE_SSL_CACERT)
                {
                    LE_ERROR(SSL_ERROR_HELP);
                    LE_ERROR(SSL_ERROR_HELP_2);
                }
            }
            curl_easy_cleanup(curl);
        }
        else {
            LE_ERROR("Couldn't initialize cURL.");
        }
        curl_global_cleanup();
    }
    else {
        LE_INFO("No data connction, couldn't publish");
    }

}


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
        PublishGpsSample(&fix);
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

    le_timer_Ref_t timerPtr = le_timer_Create("Connection timeout timer");
    le_clk_Time_t interval = {TIMEOUT_SECS, 0};
    le_timer_SetInterval(timerPtr, interval);
    le_timer_SetHandler(timerPtr, &TimeoutHandler);
    WaitingForConnection = true;
    le_timer_Start(timerPtr);

    le_data_AddConnectionStateHandler(&ConnectionStateHandler, NULL);
    LE_INFO("Requesting connection...");
    ConnectionRef = le_data_Request();

    PositionTimerHandler(0);

}
