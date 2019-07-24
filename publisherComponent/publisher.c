#include "legato.h"
#include "interfaces.h"
/* #include "le_data_interface.h" */
#include <curl/curl.h>
#include "gps.h"

#define TIMEOUT_SECS (60)
#define MAX_DATA_SIZE_TO_PRINT (240)

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
static void PublishGpsSample(void *sample);

/*
 * variable defintions
 */
static le_timer_Ref_t PositionTimer;
static le_data_RequestObjRef_t ConnectionRef;
static le_timer_Ref_t TimeoutTimerRef;
static uint16_t PositioningIntervalSeconds = 60;
static bool WaitingForConnection;

const char * Url = "http://api.animaltrackr.com/animal/points";
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
        LE_ASSERT_OK(le_timer_Start(PositionTimer));
        PositionTimerHandler(NULL);
        LE_INFO("Interface %s connected.", intfName);
    }
    else
    {
        WaitingForConnection = true;
        LE_ASSERT_OK(le_timer_Stop(PositionTimer));
        LE_INFO("Interface %s disconnected.", intfName);
    }

}

static void PublishGpsSample
(
    void * sample
)
{
    struct GpsFix * fix = sample;
    CURL *curl;
    CURLcode res;
    uint16_t responseCode = -1;
    char str[1000];

    snprintf(str, 1000, "{\"tracker\":\"%s\",\"timestamp\":"
            "\"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ\",\"geo_long\":\"%9.6f\","
            "\"geo_lat\":\"%9.6f\",\"geo_error_radius\":\"%5.1f\",\"geo_method\":\"B\"}",
            TrackerUuid, fix->date.year, fix->date.month, fix->date.day,
            fix->time.hours, fix->time.minutes, fix->time.seconds,
            fix->time.milliseconds, fix->loc.latitude, fix->loc.longitude,
            fix->loc.hAccuracy);

    printf("Publishing payload: '%s'", str);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, Url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PrintCallback);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
        LE_ASSERT_OK(le_timer_Start(TimeoutTimerRef));
        res = curl_easy_perform(curl);
        LE_ASSERT_OK(le_timer_Stop(TimeoutTimerRef));
        if (res != CURLE_OK)
        {
            LE_ERROR("curl_easy_perform() failed: %s", curl_easy_strerror(res));
            if (res == CURLE_SSL_CACERT)
            {
                LE_ERROR(SSL_ERROR_HELP);
                LE_ERROR(SSL_ERROR_HELP_2);
            }
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        if (responseCode >= 200 && responseCode <300) {
            LE_INFO("Succesfully published data: %d", responseCode);
        } else {
            LE_WARN("Publish failed, response: %d", responseCode);
        }
        curl_easy_cleanup(curl);
    }
    else {
        LE_ERROR("Couldn't initialize cURL.");
    }
    curl_global_cleanup();
}



static void PositionTimerHandler
(
        le_timer_Ref_t timer ///< Position update sample
)
{
    struct GpsFix fix;

    le_result_t gpsRes = GpsRead(&fix);
    LE_ERROR_IF(gpsRes != LE_OK, "Couldn't get GPS fix");

    if (WaitingForConnection) {
        LE_WARN("No data connection, discarding position update");
    } else if (gpsRes == LE_OK) {
        PublishGpsSample(&fix);
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
    LE_ASSERT_OK(le_timer_SetMsInterval(PositionTimer, PositioningIntervalSeconds * 1000));
    LE_ASSERT_OK(le_timer_SetRepeat(PositionTimer, 0));
    LE_ASSERT_OK(le_timer_SetHandler(PositionTimer, PositionTimerHandler));

    le_data_AddConnectionStateHandler(&ConnectionStateHandler, NULL);
    LE_INFO("Requesting connection...");
    ConnectionRef = le_data_Request();

    TimeoutTimerRef = le_timer_Create("Connection timeout timer");
    le_clk_Time_t interval = {TIMEOUT_SECS, 0};
    le_timer_SetInterval(TimeoutTimerRef, interval);
    le_timer_SetHandler(TimeoutTimerRef, &TimeoutHandler);
    WaitingForConnection = true;
}
