// BLE_Proximity_Multiple_Detector

/***********************************************************************************

  Golf Cart Proximity Detector - Enable golf cart ignition when your phone is within
  range.  Phone must be running a BLE beacon program which sends a particular UUID
  which is recorded in the knownDevices array.
  
  Based on code by Pete Dziekan, a.k.a PeteTheGeek

************************************************************************************/

/**********************************************************************************
  Configuration parameters
 **********************************************************************************/
#define SCAN_DURATION           3       // BLEscan duration in sec.
#define RSSI_PROXIMITY          -92     // minimum RSSI needed to declare "presence"
#define RELAY_PIN               15      // Port for LED/relay
#define EXT_LED_PIN             23      // External LED or other device
#define MISSES_BEFORE_ABSENT    20      // Has to miss this many in a row to turn off
#define uS_TO_S_FACTOR          1000000 // Conversion factor for micro seconds to seconds
#define NORMAL_SLEEP            15      // sleep duration (secs)
#define OUT_OF_RANGE_DELAY_MSEC 5000    // wait this long before checking again after signal disappears


#define CONFIG_LOG_DEFAULT_LEVEL ESP_LOG_VERBOSE //enable logging

/**********************************************************************************
  My known devices
 **********************************************************************************/
struct typeKnownDevice {
  String uuid;                // BLE UUID
  int    rssi;                // RSSI
  String name;                // Friendly device name
};

typeKnownDevice knownDevices[] = {
  {"78acf66b44ce43aab23aa1a8247ed9dd", 0, "David's Phone"},
  {"78acf66b44ce43aab23aa1a8247ed9bb", 0, "Judy's Phone"}
};

int numKnownDevices = sizeof(knownDevices) / sizeof(knownDevices[0]);
int misses=0;     //number of passes left to miss before declaring absent
uint64_t sleep_secs=((uint64_t)NORMAL_SLEEP * (uint64_t)uS_TO_S_FACTOR);

//extern "C" char *sbrkx(int i); //for debugging memory leaks


/**********************************************************************************
  setup BLE pointers
 **********************************************************************************/
#include    <BLEDevice.h>
//static      BLEAddress *pServerAddress;
BLEScan*    pBLEScan;
BLEClient*  pClient;


/**********************************************************************************
  class MyAdvertisedDeviceCallbacks - Called for each advertising BLE server
 **********************************************************************************/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("    - Checking found device..."); 
      
      int rssi=advertisedDevice.getRSSI();

      char* mfgData;
      char* hexData;
      if (advertisedDevice.getManufacturerData().length()>0) {
        hexData=BLEUtils::buildHexData(nullptr, 
            (uint8_t*)advertisedDevice.getManufacturerData().data(), 
            (uint8_t)advertisedDevice.getManufacturerData().length());
      }


      if ((uint8_t)advertisedDevice.getManufacturerData().length()>24) {
        mfgData=hexData+8;
        mfgData[32]=0;
        
        Serial.printf("\n    - Device found: RSSI=%d, dataLength=%d, raw=%d, MfrUUID=%s\n", 
            rssi, 
            (uint8_t)advertisedDevice.getManufacturerData().length(),
            (uint8_t*)advertisedDevice.getManufacturerData().data(),
            mfgData
            );
      }
      else 
        Serial.printf(" - Device not of interest (rssi=%d).\n",rssi); 

      for (int i=0; i<numKnownDevices; i++) {
        if (!mfgData)
          continue;
        else if (strncmp(mfgData, knownDevices[i].uuid.c_str(), knownDevices[i].uuid.length()) == 0) {
          Serial.printf("Found %s, rssi=%d, mfgData is %s\n",knownDevices[i].uuid.c_str(),rssi,mfgData);
          knownDevices[i].rssi = rssi;                              // save RSSI
          //Serial.printf("%s\n", advertisedDevice.getName().c_str());
          //advertisedDevice.getScan()->stop(); // We can stop if any of our devices were found, we don't need to scan any further
        }
      }

//      delete hexData;
//      hexData=NULL;
    }
    
  
//  size_t freeRAM(void)
//  {
//  char stack_dummy = 0;
//  return(&stack_dummy - sbrkx(0));
//  }

};


/**********************************************************************************
  setup()
 **********************************************************************************/
void setup() {
  Serial.begin(115200);                           // setup serial monitor for debugging
  pinMode(RELAY_PIN,OUTPUT);                      // for the relay
  pinMode(EXT_LED_PIN,OUTPUT);                    // for the external LED
  pinMode(LED_BUILTIN, OUTPUT);                   // for the built-in LED
  delay(100);
  Serial.printf("\n%s, %s\n%s\n", __DATE__, __TIME__, __FILE__);
  Serial.printf("\nLooking for %d devices",numKnownDevices);
  
 // esp_log_level_set("*", ESP_LOG_VERBOSE);

  BLEDevice::init("");                            // setup Bluetooth
  pClient  = BLEDevice::createClient();           // ... create Client

  pBLEScan = BLEDevice::getScan();                // create a new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());  // set the callback
  pBLEScan->setActiveScan(true);                  // active scan uses more power, but get results faster

  while(true)
    {
    scan(); //scan for devices.  This will keep going until sleep mode is entered.
    }
}

void goToSleep()
  { 
  esp_sleep_enable_timer_wakeup(sleep_secs);// configure timer as wakeup source

  /*
  Decide what all peripherals to shut down or keep on.
  By default, ESP32 will automatically power down the peripherals
  not needed by the wakeup source, but if you want to be a power user
  this is for you. Read in detail at the API docs
  http://esp-idf.readthedocs.io/en/latest/api-reference/system/deep_sleep.html
  Left the line commented as an example of how to configure peripherals.
  The line below turns off all RTC peripherals in deep sleep.  Uncomment to enable.
  */
  //esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  //Serial.println("Configured all RTC Peripherals to be powered down in sleep");


  /*
   * Go to sleep
   */
  Serial.printf("Going to sleep for %d seconds to save power\n\n", sleep_secs/uS_TO_S_FACTOR);
//  WiFi.disconnect();  // disconnect WiFi cleanly
  esp_bt_controller_disable(); //turn off BT
  delay(100);
  Serial.flush(); 
  esp_deep_sleep_start();                               // go to sleep
  }

/**********************************************************************************
  loop()
 **********************************************************************************/
void scan() {
  int i;
  for (i=0; i<numKnownDevices; i++) knownDevices[i].rssi = RSSI_PROXIMITY-1;  // reset last RSSI value
  
  Serial.printf("\nBLE - Starting scan...\n");
  BLEScanResults scanResults = pBLEScan->start(SCAN_DURATION);                // start scan to find our BLE devices
//  Serial.printf("BLE - Found %d devices\n",scanResults.getCount());
  Serial.printf("BLE - Scan complete\n");
  if (scanResults.getCount()<1)
    {
    goToSleep();  //nothing found, wait a bit and look again
    }

  else
    {
    bool found=false;
    
    for (i=0; i<numKnownDevices; i++) {
      if (knownDevices[i].rssi > RSSI_PROXIMITY) { // Larger (more positive) number is stronger signal
        Serial.printf("Device in range: RSSI %d, %s\n",
                      knownDevices[i].rssi,
                      knownDevices[i].name.c_str());
                      
        found=true; // found one!
        break;
      }
    }
    if (found) {
      digitalWrite(RELAY_PIN, HIGH);  // turn on relay
      digitalWrite(EXT_LED_PIN,HIGH); // and the external LED
      digitalWrite(LED_BUILTIN, HIGH);// and the internal LED
      misses=MISSES_BEFORE_ABSENT;
      }
    else if (--misses <=0) {
      digitalWrite(RELAY_PIN, LOW);   // turn off relay
      digitalWrite(EXT_LED_PIN,LOW);  // external LED always follows relay
      digitalWrite(LED_BUILTIN, LOW);  // warning LED off
      misses=0;     //keep it at zero until device found again
      goToSleep(); //shut everything down for a while to save power
      }
    else {
      Serial.printf("All devices out of range: %d more attempts to contact it before disabling cart.\n", misses);
      digitalWrite(LED_BUILTIN, LOW);  // warning LED off
      delay(OUT_OF_RANGE_DELAY_MSEC); //hang out a bit before checking again
      }
    }
}

void loop() {} //not used
