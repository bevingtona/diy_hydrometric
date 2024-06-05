/**
 * v0.2 of RemoteLogger library for modular remote data loggers
 * Author: Rachel Pagdin
 * June 4, 2024
*/

#include <Arduino.h>
#include <RemoteLogger.h>

RemoteLogger::RemoteLogger(){
    /** TODO: are these globally available in the library source code? I don't think so */
    IridiumSBD modem(IridiumSerial);        // declare Iridium object
    RTC_PCF8523 rtc;                        // declare RTC object

    /** TODO: this may not be the best spot for this if user can change pins -- when do they do that? */
    pinMode(ledPin, OUTPUT);
    pinMode(vbatPin, INPUT);
    pinMode(tplPin, OUTPUT);
    pinMode(IridSlpPin, OUTPUT);
}

/**
 * blink preset LED with number of blinks, timing, and pause between sequences set
 * n: number of blinks
 * high_ms: time on for each blink
 * low_ms: time off for each blink
 * btw_ms: time between each sequence of n blinks
*/
void RemoteLogger::blinky(int16_t n, int16_t high_ms, int16_t low_ms, int16_t btw_ms){
    for(int i = 1; i <=n; i++){
        digitalWrite(ledPin, HIGH);
        delay(high_ms);
        digitalWrite(ledPin, LOW);
        delay(low_ms);
    }
    delay(btw_ms);
}

/**
 * write specified header and data to CSV file
 * does not manage matching the lengths for you -- you are responsible for making sure your datastring is the right length
 * do NOT add newline characters to the end of datastrings, this will add empty lines in CSV file
 * only writes header if the file is newly created (i.e. has no header yet)
 * 
 * header: column headers for CSV file
 * datastring_for_csv: the line of data to write to the CSV file
 * outname: name of the CSV file (e.g. /HOURLY.csv)
*/
void RemoteLogger::write_to_csv(String header, String datastring_for_csv, String outname){
    File dataFile;      // File instance -- only used within this function

    /* If file doesn't exist, write header and data, otherwise only write data */
    if (!SD.exists(outname)){
        dataFile = SD.open(outname, FILE_WRITE);
        if (dataFile){
            dataFile.println(header);       // write header to file
            dataFile.println(datastring_for_csv);       // write data to file
        }
        dataFile.close();       // make sure the file is closed
    } else {
        dataFile = SD.open(outname, FILE_WRITE);
        if (dataFile) {
            dataFile.println(datastring_for_csv);       // write data to file
        }
        dataFile.close();       // make sure the file is closed
    }
}

/**
 * reads battery voltage, returns in volts
 * if using a board other than Feather M0 Adalogger, check documentation for battery read pin
 * TODO: documentation for changing preset pins for a different board
*/
float RemoteLogger::sample_batt_v(){
   pinMode(vbatPin, INPUT);
   float batt_v = (analogRead(vbatPin) * 2 * 3.3) / 1024;      // preset conversion to volts
   return batt_v; 
}

/**
 * alert TPL done
 * use A0 as output on Feather M0 Adalogger (only analog output)
 * TODO: set A0 to low in setup code first thing to avoid alerting prematurely?
*/
void RemoteLogger::tpl_done(){
    pinMode(tplPin, OUTPUT);       // just in case
    digitalWrite(tplPin, LOW); delay(50); digitalWrite(tplPin, HIGH); delay(50);
    digitalWrite(tplPin, LOW); delay(50); digitalWrite(tplPin, HIGH); delay(50);
    digitalWrite(tplPin, LOW); delay(50); digitalWrite(tplPin, HIGH); delay(50);
    digitalWrite(tplPin, LOW); delay(50); digitalWrite(tplPin, HIGH); delay(50);
}

/**
 * send provided message over the Iridium network
 * connect Iridium sleep pin (7 - grey) to pin 13 or change value of IridSlpPin
 * TODO: investigate -- did removing the Watchdog mess things up? try it with the TPL
*/
int RemoteLogger::send_msg(String my_msg){
    digitalWrite(IridSlpPin, HIGH);     // wake up the modem
    delay(2000);        // wait for RockBlock to power on

    IridiumSerial.begin(19200);     // Iridium serial at 19200 baud
    modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);

    int err = modem.begin();        // start up the modem
    if (err == ISBD_IS_ASLEEP){
        err = modem.begin();    // try to start up again
    }

    err = modem.sendSBDText(my_msg.c_str());    // try to send the message

    if (err != ISBD_SUCCESS) { // if unsuccessful try again
        err = modem.begin();
        err = modem.sendSBDText(my_msg.c_str());
    }

    // calibrate the RTC time roughly every 5 days
    /** TODO: will this miss days by accident? what if we miss noon? (i.e. only sending every two hours)*/
    /** TODO: do we need the pre/post time strings? */
    if (rtc.now().hour() == 12 & rtc.now().day() % 5 == 0) {
        sync_clock();
    }

    digitalWrite(IridSlpPin, LOW);      // put the modem back to sleep
    return err; 
}

/**
 * test the Iridium modem + connection by sending a message
 * warning that this will attempt to send a message and use credits
 * prints status messages to Serial - does not return any status information from function
*/
void RemoteLogger::irid_test(String msg){
    digitalWrite(IridSlpPin, HIGH);         // turn on modem
    delay(2000);        // wait for modem to start up

    int signalQuality = -1;     // need this to pass in for signal quality query

    IridiumSerial.begin(19200);
    modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);

    /* begin satellite modem operation */
    Serial.println(" - starting modem...");
    int err = modem.begin();
    if (err != ISBD_SUCCESS) {
        Serial.print(" - begin failed: error ");
        Serial.println(err);
        if (err == ISBD_NO_MODEM_DETECTED) {
            Serial.println(" - no modem detected: check wiring.");
        }
        return;     // leave the function - no point in trying to send
    }

    /* print the firmware version */
    char version[12];
    err = modem.getFirmwareVersion(version, sizeof(version));
    if (err != ISBD_SUCCESS) {      // didn't get version info
        Serial.print(" - firmware version failed: error ");
        Serial.println(err);
        return;     // leave the test function
    }
    Serial.print(" - firmware version is ");
    Serial.print(version);
    Serial.println(".");

    /* get signal quality */
    int n = 0;
    while (n < 10) {    // test signal quality 10 times
        err = modem.getSignalQuality(signalQuality);    // query signal quality
        if (err != ISBD_SUCCESS) {
            Serial.print(" - signalQuality failed: error ");
            Serial.println(err);
            return;        // leave the test function
        }
        Serial.print(" - signal quality is currently ");
        Serial.print(signalQuality);
        Serial.println(".");
        n++;
        delay(1000);
    }

    /* send the message */
    Serial.print(" - Attempting: ");
    msg = "Hello world! " + msg;
    Serial.println(msg);
    err = modem.sendSBDText(msg.c_str());
    if (err != ISBD_SUCCESS) {
        Serial.print(" - sendSBDText failed: error ");
        Serial.println(err);
        if (err =- ISBD_SENDRECEIVE_TIMEOUT) {
            Serial.println(" - try again with a better view of the sky.");
        }
    } else {
        Serial.println(" - hey, it worked!");
    }

    /* sync clock to Iridium */
    Serial.println("Sync clock to Iridium");
    sync_clock();
    
}

/**
 * helper function 
 * sync RTC to system time from Iridium RockBlock modem
*/
void RemoteLogger::sync_clock(){
    struct tm t;
    int err_time = modem.getSystemTime(t);
    if (err_time == ISBD_SUCCESS) {
        String pre_time = rtc.now().timestamp();
        rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));
        String post_time = rtc.now().timestamp();
    }
}
