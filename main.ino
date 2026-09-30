#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>
#include <ESP32Servo.h>
#include <map>

#include "config.h"


// ═══════════════════════════════════════════════════════════════════════════
// Forward declarations
// ═══════════════════════════════════════════════════════════════════════════

void buzzerOn();
void buzzerOff();

void setRGB(int comp,bool r,bool g,bool b);
void allRGBOff(int comp);

void openLid();
void closeLid();
bool anyAlarmActive();

void triggerAlarm(int comp);
void confirmAlarm(int comp);
void snoozeAlarm();

void checkAlarms();
void checkMissed();

void updateOLED();

void handleBuzzerPattern();
void handleFlash();
void handleGreenHold();

void handleButtons();

void logEntry(
    int comp,
    int schedH,
    int schedM,
    uint32_t takenEpoch,
    bool missed
);

String buildLogJson(
    int page,
    int perPage
);

String buildAlarmJson();

void saveAlarms();
void loadAlarms();


void setupWiFi();
void setupBLE();
void setupWebServer();

String getTimeString(
    const DateTime &dt
);

String getDateString(
    const DateTime &dt
);

DateTime addMinutes(
    const DateTime &dt,
    int mins
);



// ═══════════════════════════════════════════════════════════════════════════
// Hardware objects
// ═══════════════════════════════════════════════════════════════════════════


RTC_DS3231 rtc;


U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);


Preferences prefs;



AsyncWebServer server(80);

Servo lidServo;
bool lidIsOpen = false;



// ═══════════════════════════════════════════════════════════════════════════
// Alarm data
// ═══════════════════════════════════════════════════════════════════════════


struct AlarmSchedule
{
    int hour = -1;
    int minute = 0;
};


AlarmSchedule alarms[
    NUM_COMPARTMENTS
];

//Medicine name for oled 

String medicineNames[NUM_COMPARTMENTS]=
{
    "Medicine1",
    "Medicine2",
    "medicine3",
};


enum AlarmState
{
    IDLE,
    RINGING,
    SNOOZED,
    CONFIRMED,
    MISSED_STATE
};



struct CompState
{

    AlarmState state = IDLE;

    int snoozeCount = 0;

    uint32_t alarmEpoch = 0;

    uint32_t snoozeUntil = 0;

    uint32_t confirmedAt = 0;

    bool greenHold = false;

    bool confirmedToday = false;


};



CompState compState[
    NUM_COMPARTMENTS
];



// ═══════════════════════════════════════════════════════════════════════════
// RGB pins
// ═══════════════════════════════════════════════════════════════════════════


const int RGB_PINS[3][3] =
{

    {
        PIN_RGB1_R,
        PIN_RGB1_G,
        PIN_RGB1_B
    },


    {
        PIN_RGB2_R,
        PIN_RGB2_G,
        PIN_RGB2_B
    },


    {
        PIN_RGB3_R,
        PIN_RGB3_G,
        PIN_RGB3_B
    }

};



// ═══════════════════════════════════════════════════════════════════════════
// Timing
// ═══════════════════════════════════════════════════════════════════════════


bool buzzerState = false;

uint32_t buzzerLastMs = 0;


bool flashState = false;

uint32_t flashLastMs = 0;



bool btnPrev[4] =
{
    HIGH,
    HIGH,
    HIGH,
    HIGH
};



uint32_t oledLastMs = 0;



#define OLED_REFRESH_MS 1000



// ═══════════════════════════════════════════════════════════════════════════
// BLE
// ═══════════════════════════════════════════════════════════════════════════


NimBLECharacteristic *pCharTime = nullptr;

NimBLECharacteristic *pCharAlarm = nullptr;

NimBLECharacteristic *pCharLog = nullptr;

NimBLECharacteristic *pCharNotif = nullptr;



// Prevent duplicate alarms
std::map<uint32_t,uint32_t> firedToday;
uint32_t firedTodayDay = 0;



// ═══════════════════════════════════════════════════════════════════════════
// RGB functions
// ═══════════════════════════════════════════════════════════════════════════


void setRGB(
    int comp,
    bool r,
    bool g,
    bool b
)
{

    digitalWrite(
        RGB_PINS[comp][0],
        r ? HIGH : LOW
    );


    digitalWrite(
        RGB_PINS[comp][1],
        g ? HIGH : LOW
    );


    digitalWrite(
        RGB_PINS[comp][2],
        b ? HIGH : LOW
    );

}



void allRGBOff(int comp)
{
    setRGB(
        comp,
        false,
        false,
        false
    );
}



// ═══════════════════════════════════════════════════════════════════════════
// Buzzer
// ═══════════════════════════════════════════════════════════════════════════


void buzzerOn()
{
    digitalWrite(
        PIN_BUZZER,
        HIGH
    );

    buzzerState=true;
}



void buzzerOff()
{
    digitalWrite(
        PIN_BUZZER,
        LOW
    );

    buzzerState=false;
}

//====================================================================
//Servo Motor
//====================================================================
void openLid()
{
    lidServo.write(90);
    lidIsOpen = true;
}

void closeLid()
{
    lidServo.write(0);
    lidIsOpen = false;
}
bool anyAlarmActive()
{
    for(int i = 0; i < NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == RINGING)
            return true;
    }

    return false;
}


// ═══════════════════════════════════════════════════════════════════════════
// Log storage
// ═══════════════════════════════════════════════════════════════════════════


void logEntry(
    int comp,
    int schedH,
    int schedM,
    uint32_t takenEpoch,
    bool missed
)
{

    prefs.begin(
        NVS_NS_LOG,
        false
    );


    int count =
        prefs.getInt(
            NVS_KEY_COUNT,
            0
        );

    int pos =
        prefs.getInt(
            NVS_KEY_LOG_POS,
            0
        );


    if(count > MAX_LOG_ENTRIES)
        count = MAX_LOG_ENTRIES;


    int writeIndex =
        count == MAX_LOG_ENTRIES
            ? pos
            : count;


    char key[16];

    snprintf(
        key,
        sizeof(key),
        "e%d",
        writeIndex
    );


    char value[64];


    snprintf(
        value,
        sizeof(value),
        "%d,%d,%d,%lu,%d",
        comp+1,
        schedH,
        schedM,
        (unsigned long)takenEpoch,
        missed ? 1 : 0
    );


    prefs.putString(
        key,
        value
    );


    if(count == MAX_LOG_ENTRIES)
        pos = (pos + 1) % MAX_LOG_ENTRIES;
    else
        count++;

    prefs.putInt(
        NVS_KEY_COUNT,
        count
    );

    prefs.putInt(
        NVS_KEY_LOG_POS,
        pos
    );

    prefs.end();

}// ═══════════════════════════════════════════════════════════════════════════
// Log JSON builder
// ═══════════════════════════════════════════════════════════════════════════

String buildLogJson(int page, int perPage)
{

    prefs.begin(
        NVS_NS_LOG,
        true
    );


    int count =
        prefs.getInt(
            NVS_KEY_COUNT,
            0
        );

    int pos =
        prefs.getInt(
            NVS_KEY_LOG_POS,
            0
        );


    int start =
        page * perPage;


    int end =
        min(
            start + perPage,
            count
        );

    int ringStart =
        count == MAX_LOG_ENTRIES
            ? pos
            : 0;


    DynamicJsonDocument doc(4096);


    doc["total"] = count;
    doc["page"] = page;



    JsonArray arr =
        doc.createNestedArray(
            "entries"
        );



    for(
        int i=start;
        i<end;
        i++
    )
    {

        char key[16];

        int idx =
            (ringStart + i) % MAX_LOG_ENTRIES;

        snprintf(
            key,
            sizeof(key),
            "e%d",
            idx
        );


        String val =
            prefs.getString(
                key,
                ""
            );


        if(val.length()==0)
            continue;



        int comp;
        int sh;
        int sm;
        int miss;

        unsigned long epoch;



        sscanf(
            val.c_str(),
            "%d,%d,%d,%lu,%d",
            &comp,
            &sh,
            &sm,
            &epoch,
            &miss
        );



        JsonObject obj =
            arr.createNestedObject();



        obj["comp"] = comp;


        obj["schedTime"] =
            String(sh)
            + ":"
            + (sm<10 ? "0":"")
            + String(sm);



        obj["takenEpoch"] =
            (uint32_t)epoch;



        obj["missed"] =
            miss==1;

    }


    prefs.end();


    String output;


    serializeJson(
        doc,
        output
    );


    return output;

}



// Clear logs

void clearLog()
{

    prefs.begin(
        NVS_NS_LOG,
        false
    );


    int count =
        prefs.getInt(
            NVS_KEY_COUNT,
            0
        );



    for(
        int i=0;
        i<count;
        i++
    )
    {

        char key[16];

        snprintf(
            key,
            sizeof(key),
            "e%d",
            i
        );


        prefs.remove(key);

    }



    prefs.putInt(
        NVS_KEY_COUNT,
        0
    );

    prefs.putInt(
        NVS_KEY_LOG_POS,
        0
    );


    prefs.end();

}



// ═══════════════════════════════════════════════════════════════════════════
// Alarm storage
// ═══════════════════════════════════════════════════════════════════════════


void saveAlarms()
{

    prefs.begin(
        NVS_NS_ALARMS,
        false
    );


    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        char h[8];
        char m[8];


        snprintf(
            h,
            sizeof(h),
            "h%d",
            i
        );


        snprintf(
            m,
            sizeof(m),
            "m%d",
            i
        );


        prefs.putInt(
            h,
            alarms[i].hour
        );


        prefs.putInt(
            m,
            alarms[i].minute
        );
    char n[8];

snprintf(
    n,
    sizeof(n),
    "n%d",
    i
);

prefs.putString(
    n,
    medicineNames[i]
);

    }


    prefs.end();

}




void loadAlarms()
{

    prefs.begin(
        NVS_NS_ALARMS,
        true
    );


    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        char h[8];
        char m[8];


        snprintf(
            h,
            sizeof(h),
            "h%d",
            i
        );


        snprintf(
            m,
            sizeof(m),
            "m%d",
            i
        );


        alarms[i].hour =
            prefs.getInt(
                h,
                -1
            );


        alarms[i].minute =
            prefs.getInt(
                m,
                0
            );
        char n[8];

snprintf(
    n,
    sizeof(n),
    "n%d",
    i
);

medicineNames[i] =
    prefs.getString(
        n,
        i == 0 ? "Medicine1" :
        i == 1 ? "Medicine2" :
                 "Medicine3"
    );

    }


    prefs.end();

}




String buildAlarmJson()
{

    DynamicJsonDocument doc(512);



    JsonArray arr =
        doc.createNestedArray(
            "alarms"
        );



    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        JsonObject obj =
            arr.createNestedObject();


        obj["comp"] =
            i+1;


        obj["hour"] =
            alarms[i].hour;


        obj["minute"] =
            alarms[i].minute;

    }



    String out;


    serializeJson(
        doc,
        out
    );


    return out;

}



// ═══════════════════════════════════════════════════════════════════════════
// Alarm trigger
// ═══════════════════════════════════════════════════════════════════════════


void triggerAlarm(int comp)
{
    compState[comp].confirmedToday = false;

    if(
        compState[comp].state
        ==
        RINGING
    )
        return;



    DateTime now =
        rtc.now();



    compState[comp].state =
        RINGING;
    openLid();



    compState[comp].alarmEpoch =
        now.unixtime();



    compState[comp].snoozeCount =
        0;

    buzzerLastMs =
        millis();



    Serial.printf(
        "[ALARM] Compartment %d\n",
        comp+1
    );



    if(pCharNotif)
    {

        char msg[64];


        snprintf(
            msg,
            sizeof(msg),
            "{\"event\":\"alarm\",\"comp\":%d}",
            comp+1
        );


        pCharNotif->setValue(
            msg
        );


        pCharNotif->notify();

    }

}// ═══════════════════════════════════════════════════════════════════════════
// Confirm alarm
// ═══════════════════════════════════════════════════════════════════════════

void confirmAlarm(int comp)
{

    if(
        compState[comp].state != RINGING &&
        compState[comp].state != SNOOZED
    )
        return;



    DateTime now =
        rtc.now();



    logEntry(
        comp,
        alarms[comp].hour,
        alarms[comp].minute,
        now.unixtime(),
        false
    );



    compState[comp].state =
        CONFIRMED;
    if(!anyAlarmActive())
     closeLid();
    compState[comp].confirmedToday = true;


    compState[comp].confirmedAt =
        millis();


    compState[comp].greenHold =
        true;

    if (pCharNotif)
{
    char msg[96];

    snprintf(
        msg,
        sizeof(msg),
        "{\"event\":\"confirmed\",\"comp\":%d,\"takenEpoch\":%lu}",
        comp + 1,
        (unsigned long)now.unixtime()
    );

    pCharNotif->setValue(msg);
    pCharNotif->notify();
}



    allRGBOff(comp);


    setRGB(
        comp,
        false,
        true,
        false
    );


    buzzerOff();


    Serial.printf(
        "[CONFIRM] Compartment %d\n",
        comp+1
    );

}




// ═══════════════════════════════════════════════════════════════════════════
// Snooze
// ═══════════════════════════════════════════════════════════════════════════

void snoozeAlarm()
{

    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        if(
            compState[i].state
            ==
            RINGING
        )
        {

            if(
                compState[i].snoozeCount
                >=
                MAX_SNOOZES
            )
                return;



            compState[i].snoozeCount++;


            DateTime now =
                rtc.now();



            DateTime end =
                addMinutes(
                    now,
                    SNOOZE_MINUTES
                );



            compState[i].snoozeUntil =
                end.unixtime();



            compState[i].state =
                SNOOZED;



            allRGBOff(i);


            buzzerOff();



            Serial.printf(
                "[SNOOZE] Compartment %d\n",
                i+1
            );


            return;

        }

    }

}





// ═══════════════════════════════════════════════════════════════════════════
// Alarm checking
// ═══════════════════════════════════════════════════════════════════════════

void checkAlarms()
{

    DateTime now =
        rtc.now();



    uint32_t epoch =
        now.unixtime();



    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        if(
            alarms[i].hour < 0
        )
            continue;



        if(
            compState[i].state
            ==
            SNOOZED
        )
        {

            if(
                epoch >=
                compState[i].snoozeUntil
            )
            {

                compState[i].state =
                    RINGING;

            }


            continue;

        }




        if(
            compState[i].state != IDLE &&
            compState[i].state != MISSED_STATE &&
            compState[i].state != CONFIRMED
        )
            continue;




        if(
            now.hour()
            ==
            alarms[i].hour
            &&
            now.minute()
            ==
            alarms[i].minute
        )
        {


            if(
                firedTodayDay == 0
            )
            {
                firedTodayDay =
                    now.year()*10000
                    +
                    now.month()*100
                    +
                    now.day();
            }


            uint32_t key =
                (uint32_t)i*10000
                +
                alarms[i].hour*100
                +
                alarms[i].minute;


            uint32_t day =
                now.year()*10000
                +
                now.month()*100
                +
                now.day();



            uint32_t full =
                key
                +
                day*100000;



            if(
                firedToday.find(full)
                ==
                firedToday.end()
            )
            {

                firedToday[full] =
                    epoch;


                triggerAlarm(i);

            }

        }

    }

}
//--------//////----//-/-/-//////

uint32_t lastMissedCheck = 0;

void checkMissed()
{
    // The timer that protects your hardware wires from crashing
    if (millis() - lastMissedCheck < 1000) return;
    lastMissedCheck = millis();

    // Your original real-time clock reading line
    DateTime now = rtc.now();

    uint32_t epoch = now.unixtime();

    for(int i=0; i<NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == RINGING || compState[i].state == SNOOZED)
        {
            if(epoch >= compState[i].alarmEpoch + MISSED_MINUTES*60)
            {
                logEntry(i, alarms[i].hour, alarms[i].minute, compState[i].alarmEpoch, true);
                compState[i].state = MISSED_STATE;
                allRGBOff(i);
                buzzerOff();
            }
        }
    }
}









// ═══════════════════════════════════════════════════════════════════════════
// BLE
// ═══════════════════════════════════════════════════════════════════════════
class AlarmCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(
        NimBLECharacteristic *pCharacteristic,
        NimBLEConnInfo &connInfo
    ) override
    {
        std::string value = pCharacteristic->getValue();

        if (value.length() == 0)
            return;

        String data = String(value.c_str());

        Serial.print("[BLE] Received: ");
        Serial.println(data);

        /*
          Expected format:

          C1=14:30|Paracetamol
          C2=18:00|Vitamin D
          C3=21:00|Medicine Name

          Old format is also accepted:

          C1=14:30
        */

        if (data.length() < 7)
            return;

        if (data.charAt(0) != 'C')
            return;

        int comp = data.charAt(1) - '1';

        if (
            comp < 0 ||
            comp >= NUM_COMPARTMENTS
        )
            return;

        int equal = data.indexOf('=');
        int colon = data.indexOf(':');
        int separator = data.indexOf('|');

        if (equal < 0 || colon < 0 )
    return;

        // -----------------------------
        // TIME
        // -----------------------------

        int hour =
            data.substring(
                equal + 1,
                colon
            ).toInt();

        // Look for medicine-name separator
        //int separator =
          //  data.indexOf('|', colon);

        int minute;

        if (separator >= 0)
        {
            minute =
                data.substring(
                    colon + 1,
                    separator
                ).toInt();
        }
        else
        {
            minute =
                data.substring(
                    colon + 1
                ).toInt();
        }

        if (
            hour < 0 ||
            hour > 23 ||
            minute < 0 ||
            minute > 59
        )
            return;

        // -----------------------------
        // MEDICINE NAME
        // -----------------------------

        if (separator >= 0)
        {
            String name =
                data.substring(
                    separator + 1
                );

            name.trim();

            if (name.length() == 0)
            return;
        medicineNames[comp] = name;
            {
                Serial.print(
                    "[BLE] Medicine C"
                );

                Serial.print(comp + 1);

                Serial.print(": ");

                Serial.println(medicineNames[comp]);
            }
        }

        // -----------------------------
        // SAVE ALARM
        // -----------------------------

        alarms[comp].hour = hour;
        alarms[comp].minute = minute;

        compState[comp].confirmedToday = false;

        saveAlarms();

        updateOLED();

        Serial.printf(
            "[BLE] C%d alarm set to %02d:%02d\n",
            comp + 1,
            hour,
            minute
        );
    }
};
AlarmCallbacks alarmCallbacks;

void setupBLE()
{

    NimBLEDevice::init(
        "MedBox"
    );


    NimBLEServer *bleServer =
        NimBLEDevice::createServer();



    NimBLEService *service =
        bleServer->createService(
            BLE_SVC_UUID
        );



    pCharTime =
        service->createCharacteristic(
            BLE_CHAR_TIME,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE
        );


    pCharAlarm =
        service->createCharacteristic(
            BLE_CHAR_ALARM,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::WRITE
        );
    pCharAlarm->setCallbacks(&alarmCallbacks);

    pCharLog =
        service->createCharacteristic(
            BLE_CHAR_LOG,
            NIMBLE_PROPERTY::READ
        );


    pCharNotif =
        service->createCharacteristic(
            BLE_CHAR_NOTIF,
            NIMBLE_PROPERTY::NOTIFY
        );



    service->start();



    NimBLEAdvertising *adv =
        NimBLEDevice::getAdvertising();

    adv->setName("MedBox");


    adv->addServiceUUID(
        BLE_SVC_UUID
    );


    adv->start();



    Serial.println(
        "[BLE] Started"
    );

}


// ═══════════════════════════════════════════════════════════════════════════
// Buttons
// ═══════════════════════════════════════════════════════════════════════════

void handleButtons()
{

    int buttons[3] =
    {
        PIN_BTN1,
        PIN_BTN2,
        PIN_BTN3
    };



    for(
        int i=0;
        i<3;
        i++
    )
    {

        bool state =
            digitalRead(
                buttons[i]
            );


        if(
            btnPrev[i]
            ==
            HIGH
            &&
            state
            ==
            LOW
        )
        {

            confirmAlarm(i);

        }


        btnPrev[i]=state;

    }
    if (PIN_BTN_SNOOZE >= 0) {
        bool snooze = digitalRead(PIN_BTN_SNOOZE);

        if (btnPrev[3] == HIGH && snooze == LOW) {
            snoozeAlarm();
        }

        btnPrev[3] = snooze;
    }


    
}

void setup()
{
    lidServo.setPeriodHertz(50);
    lidServo.attach(PIN_SERVO, 500, 2400);
    lidServo.write(0);

    Serial.begin(
        115200
    );



    for(
        int i=0;
        i<NUM_COMPARTMENTS;
        i++
    )
    {

        pinMode(
            RGB_PINS[i][0],
            OUTPUT
        );


        pinMode(
            RGB_PINS[i][1],
            OUTPUT
        );


        pinMode(
            RGB_PINS[i][2],
            OUTPUT
        );


        allRGBOff(i);

    }



    pinMode(
        PIN_BUZZER,
        OUTPUT
    );


    buzzerOff();
    if(PIN_BTN_SNOOZE >=0)
    {pinMode(PIN_BTN_SNOOZE,INPUT_PULLUP);
    }



    pinMode(
        PIN_BTN1,
        INPUT_PULLUP
    );


    pinMode(
        PIN_BTN2,
        INPUT_PULLUP
    );


    pinMode(
        PIN_BTN3,
        INPUT_PULLUP
    );


    pinMode(
        PIN_BTN_SNOOZE,
        INPUT_PULLUP
    );



    Wire.begin(
        PIN_SDA,
        PIN_SCL
    );



    //rtc.begin();
    if (!rtc.begin()) {
    Serial.println("RTC NOT FOUND!");
} else {
    Serial.println("RTC OK");
    //rtc.adjust(DateTime(2026, 8, 22, 11, 32, 0));
}

u8g2.begin();
Serial.println("OLED OK");

updateOLED();
    


    loadAlarms();
  

//saveAlarms();



    //setupWiFi();


    setupBLE();


    //setupWebServer();



    Serial.println(
        "MedBox Started"
    );

}



// ═══════════════════════════════════════════════════════════════════════════
// Loop
// ═══════════════════════════════════════════════════════════════════════════

void loop()
{

    checkAlarms();


    checkMissed();


    handleButtons();


    handleBuzzerPattern();


    handleFlash();


    handleGreenHold();



    if(
        millis()-oledLastMs
        >
        OLED_REFRESH_MS
    )
    {

        oledLastMs =
            millis();
        checkAlarms();
        checkMissed();
        updateOLED();

    }



    delay(10);

}


// ═══════════════════════════════════════════════════════════════════════════
// Web server
// ═══════════════════════════════════════════════════════════════════════════

void setupWebServer()
{
    server.on(
    "/clearalarm",
    HTTP_GET,
    [](AsyncWebServerRequest *req)
    {
        if(!req->hasParam("comp"))
        {
            req->send(
                400,
                "text/plain",
                "Missing compartment"
            );
            return;
        }

        int comp =
            req->getParam("comp")->value().toInt() - 1;

        if(
            comp < 0 ||
            comp >= NUM_COMPARTMENTS
        )
        {
            req->send(
                400,
                "text/plain",
                "Invalid compartment"
            );
            return;
        }

        alarms[comp].hour = -1;
        alarms[comp].minute = 0;

        saveAlarms();

        req->send(
            200,
            "application/json",
            buildAlarmJson()
        );
    }
);

    server.on(
        "/setalarm",
        HTTP_GET,
        [](AsyncWebServerRequest *req)
        {
            if (!req->hasParam("comp") || !req->hasParam("hour") || !req->hasParam("minute"))
            {
                req->send(400, "text/plain", "Missing parameters");
                return;
            }

            int comp = req->getParam("comp")->value().toInt() - 1;
            int hour = req->getParam("hour")->value().toInt();
            int minute = req->getParam("minute")->value().toInt();

            if (comp < 0 || comp >= NUM_COMPARTMENTS || hour < 0 || hour > 23 || minute < 0 || minute > 59)
            {
                req->send(400, "text/plain", "Invalid alarm values");
                return;
            }

            alarms[comp].hour = hour;
            alarms[comp].minute = minute;
            saveAlarms();

            req->send(200, "application/json", buildAlarmJson());
        }
    );

    server.on(
        "/clearlog",
        HTTP_GET,
        [](AsyncWebServerRequest *req)
        {
            clearLog();
            req->send(200, "application/json", "{\"status\":\"ok\"}");
        }
    );

    server.begin();
    Serial.println("[Web] Server started");
}// ═══════════════════════════════════════════════════════════════════════════
// ADDITIONAL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════


// Add minutes to a DateTime



// WiFi Access Point
void setupWiFi()
{
    WiFi.mode(WIFI_AP);

    WiFi.softAP(
        WIFI_SSID,
        WIFI_PASS
    );

    Serial.println("[WiFi] Access Point started");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.softAPIP());
}


// Buzzer pattern
void handleBuzzerPattern()
{
    bool ringing = false;

    for(int i = 0; i < NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == RINGING)
        {
            ringing = true;
            break;
        }
    }

    if(!ringing)
    {
        buzzerOff();
        return;
    }

    uint32_t now = millis();

    if(buzzerState)
    {
        if(now - buzzerLastMs >= BEEP_ON_MS)
        {
            buzzerOff();
            buzzerLastMs = now;
        }
    }
    else
    {
        if(now - buzzerLastMs >= BEEP_OFF_MS)
        {
            buzzerOn();
            buzzerLastMs = now;
        }
    }
}


// Flash red LED during alarm
void handleFlash()
{
    bool ringing = false;

    for(int i = 0; i < NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == RINGING)
        {
            ringing = true;
            break;
        }
    }

    if(!ringing)
    {
        flashState = false;
        return;
    }

    uint32_t now = millis();

    if(now - flashLastMs >= FLASH_HALF_MS)
    {
        flashLastMs = now;
        flashState = !flashState;

        for(int i = 0; i < NUM_COMPARTMENTS; i++)
        {
            if(compState[i].state == RINGING)
            {
                if(flashState)
                    setRGB(i, true, false, false);
                else
                    allRGBOff(i);
            }
        }
    }
}


// Keep green LED on after confirmation
void handleGreenHold()
{
    uint32_t now = millis();

    for(int i = 0; i < NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == CONFIRMED &&
           compState[i].greenHold)
        {
            if(now - compState[i].confirmedAt >= GREEN_HOLD_MS)
            {
                compState[i].greenHold = false;

                allRGBOff(i);

                compState[i].state = IDLE;
            }
        }
    }
}
// ═══════════════════════════════════════════════════════════════════════════
// Time helpers
// ═══════════════════════════════════════════════════════════════════════════

String getTimeString(const DateTime &dt)
{
    char buf[12];

    snprintf(
        buf,
        sizeof(buf),
        "%02d:%02d:%02d",
        dt.hour(),
        dt.minute(),
        dt.second()
    );

    return String(buf);
}


String getDateString(const DateTime &dt)
{
    char buf[16];

    snprintf(
        buf,
        sizeof(buf),
        "%04d-%02d-%02d",
        dt.year(),
        dt.month(),
        dt.day()
    );

    return String(buf);
}


DateTime addMinutes(
    const DateTime &dt,
    int mins
)
{
    return DateTime(
        dt.unixtime()
        +
        mins * 60
    );
}


// OLED display
// ═══════════════════════════════════════════════════════════════════════════
// OLED
// ═══════════════════════════════════════════════════════════════════════════

bool getActiveAlarmComp(int &comp)
{
    for(int i = 0; i < NUM_COMPARTMENTS; i++)
    {
        if(compState[i].state == RINGING)
        {
            comp = i;
            return true;
        }
    }

    return false;
}


void showAlarmOLED(int comp)
{
    DateTime now = rtc.now();

    u8g2.firstPage();

    do
    {
        // -----------------------------
        // ALARM RINGING
        // -----------------------------

        u8g2.setFont(
            u8g2_font_6x10_tf
        );

        u8g2.drawStr(
            25,
            10,
            "ALARM RINGING"
        );


        // ----------
        //-------------------
        // MEDICINE NAME
        // -----------------------------

        u8g2.drawStr(
            0,
            24,
            "MED:"
        );

        u8g2.drawStr(
            28,
            24,
            medicineNames[comp].c_str()
        );


        // -----------------------------
        // ALARM TIME
        // -----------------------------

        char alarmTime[12];

        snprintf(
            alarmTime,
            sizeof(alarmTime),
            "%02d:%02d",
            alarms[comp].hour,
            alarms[comp].minute
        );

        u8g2.drawStr(
            0,
            37,
            "TIME:"
        );

        u8g2.drawStr(
            32,
            37,
            alarmTime
        );


        // -----------------------------
        // COMPARTMENT
        // -----------------------------

        char compartmentText[20];

        snprintf(
            compartmentText,
            sizeof(compartmentText),
            "COMPARTMENT C%d",
            comp + 1
        );

        u8g2.drawStr(
            0,
            50,
            compartmentText
        );


        // -----------------------------
        // CONFIRM BUTTON
        // -----------------------------

        char buttonText[20];

        snprintf(
            buttonText,
            sizeof(buttonText),
            "PRESS C%d",
            comp + 1
        );

        u8g2.drawStr(
            42,
            63,
            buttonText
        );

    }
    while(
        u8g2.nextPage()
    );
}


void updateOLED()
{
    int activeComp = -1;

    // ==========================================
    // ALARM SCREEN
    // ==========================================

    if(getActiveAlarmComp(activeComp))
    {
        showAlarmOLED(activeComp);
        return;
    }


    // ==========================================
    // ORIGINAL / NORMAL SCREEN
    // DO NOT CHANGE THIS
    // ==========================================

    DateTime now =
        rtc.now();

    u8g2.firstPage();

    do
    {
        u8g2.setFont(
            u8g2_font_6x10_tf
        );

        char line1[32];

        snprintf(
            line1,
            sizeof(line1),
            "%s",
            getDateString(now).c_str()
        );

        u8g2.drawStr(
            0,
            12,
            line1
        );


        u8g2.setFont(
            u8g2_font_8x13_tf
        );

        String time =
            getTimeString(now);

        u8g2.drawStr(
            0,
            30,
            time.c_str()
        );


        u8g2.setFont(
            u8g2_font_6x10_tf
        );

        char temp[24];

        snprintf(
            temp,
            sizeof(temp),
            "Temp %.1f C",
            rtc.getTemperature()
        );

        u8g2.drawStr(
            0,
            45,
            temp
        );
///Alarm status
bool alarmSet = false;

for(int i = 0; i < NUM_COMPARTMENTS; i++)
{
    if( 
       alarms[i].hour >= 0 &&
       compState[i].state != CONFIRMED
    )
    {
        alarmSet = true;
        break;
    }
}

if(alarmSet)
{
    u8g2.drawStr(
        0,
        54,
        "Alarm: SET"
    );
}
else
{
    u8g2.drawStr(
        0,
        54,
        "Alarm: OFF"
    );

       // u8g2.drawStr(
       //     0,
       //     63,
       //     "MedBox Ready"
       // );
        
}
///Compartment status 
u8g2.drawStr(0, 63, "C1 OFF C2 OFF C3 OFF");
char compStatus[32];
snprintf(
    compStatus,
    sizeof(compStatus),
    "C1 %s C2 %s C3 %s",
    (alarms[0].hour >= 0 && !compState[0].confirmedToday) ? "SET" : "OFF",
    (alarms[1].hour >= 0 && !compState[1].confirmedToday) ? "SET" : "OFF",
    (alarms[2].hour >= 0 && !compState[2].confirmedToday) ? "SET" : "OFF"
);
u8g2.drawStr(0, 63, compStatus);
    }
    while(
        u8g2.nextPage()
    );
}
