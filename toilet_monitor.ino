#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Milkcocoa.h>
#include "Gsender.h"

#pragma region Globals
const char* ssid = "";                           // WIFI network name
const char* password = "";                       // WIFI network password
uint8_t connection_state = 0;                    // Connected to WIFI or not
uint16_t reconnect_interval = 10000;             // If not connected wait time to try again
#pragma endregion Globals


#define MILKCOCOA_ENABLE    1
#define IFTTT_ENABLE        0

// Deep Sleep
#define SLEEP_INTERVAL         1 // 1分ごとに実行

#if MILKCOCOA_ENABLE == 1
// Milkcocoa
#define MILKCOCOA_APP_ID       ""
#define MILKCOCOA_DATASTORE    "" // データストアを変更したいときは修正
#define MILKCOCOA_SERVERPORT   1883

const char MQTT_SERVER[] PROGMEM    = MILKCOCOA_APP_ID ".mlkcca.com";
const char MQTT_CLIENTID[] PROGMEM  = __TIME__ MILKCOCOA_APP_ID;
#endif

#if IFTTT_ENABLE == 1
// IFTTT
const char *IFTTT_HOST      = "";
const char *IFTTT_URI_OPEN  = ";
const char *IFTTT_URI_USE   = "";
#endif

// WiFi
const char *WIFI_SSID       = "";
const char *WIFI_PASSWORD   = "";


// NTP
const char *NTP_SERVER = "ntp.nict.jp";
const int TIME_OFFSET = 9 * 60 * 60;  // UTC+9h (JST)


WiFiClient client;

// RTCメモリのデータ
struct {
  int lastIlluminance;
  #define RTC_DATA_MAGIC 0x65737037
  unsigned long magic;
} rtcData;

void setup() {
  
  // put your setup code here, to run once:
  Serial.begin(74880);
  Serial.println("\r\nReset Reason: " + ESP.getResetReason());
  
  // Setup WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Waiting for Wi-Fi connection");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Setup NTP
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, NTP_SERVER, TIME_OFFSET);
  timeClient.begin();
  while (!timeClient.update()) {
    delay(500);
  }
  
  setTime(timeClient.getEpochTime());
}

void loop() {
  // put your main code here, to run repeatedly:
  int illuminance;

  // DeepSleep
  int min = SLEEP_INTERVAL - minute() % SLEEP_INTERVAL;
  if (min != SLEEP_INTERVAL) {
    Serial.println(getDateTime() + " " + "Go to sleep for " + min+ " minutes.");
    ESP.deepSleep((min * 60 - second() + 10) * 1000 * 1000, WAKE_RF_DEFAULT);
  }

  // RTCメモリに保存しておいたデータを読み出す
  ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData));
  if (rtcData.magic != RTC_DATA_MAGIC) {
    // 無効なデータの場合、初期化する
    Serial.println();
    Serial.print("RTC memory invalid. Initialized...");
    rtcData.lastIlluminance = 0;   
  }

  illuminance = analogRead(A0);

  Serial.println();
  Serial.println("lastIlluminance is " + String(rtcData.lastIlluminance));
  Serial.println("Illuminance is " + String(illuminance));
  
  int diff = abs(rtcData.lastIlluminance -  (int)illuminance);

  Serial.println("diff is " + String(diff));
  
  if(diff > 300) { // 照度300luxより大きな差がある場合を検出
 
    // RTCデータ更新
    rtcData.lastIlluminance = (int)illuminance;
    rtcData.magic = RTC_DATA_MAGIC;
    ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

    DataElement elem = DataElement();
    char ch[20];
    String st = getDateTime();
    st.toCharArray(ch, 17);
    elem.setValue("datetime", ch);
    elem.setValue("illuminance", (int)illuminance);

#if MILKCOCOA_ENABLE == 1
    // Milkcocoa
    Milkcocoa milkcocoa = Milkcocoa(&client, MQTT_SERVER, MILKCOCOA_SERVERPORT, MILKCOCOA_APP_ID, MQTT_CLIENTID);
    milkcocoa.loop();
    milkcocoa.push(MILKCOCOA_DATASTORE, &elem);
#endif

    Serial.println(getDateTime() + " " +
                              "DateTime:" + String(elem.getString("datetime")) +
                              " Illuminance:" + String(elem.getInt("illuminance")));
    
#if IFTTT_ENABLE == 1
    // IFTTT
    RequestToIFTTT((int)illuminance);
#endif
    
    // send mail
    Gsender *gsender = Gsender::Instance();    // Getting pointer to class instance
    String subject = "";
    if((int)illuminance > 300) {
        subject = "男子トイレ使用中";
    }
    else {
        subject = "男子トイレ空いてます！";
    }
    
    if(gsender->Subject(subject)->Send("trigger@applet.ifttt.com", "時刻：" + String(elem.getString("datetime")) + "<br />照度：" + String(illuminance))) {
      Serial.println("Message send.");
    } else {
      Serial.print("Error sending message: ");
      Serial.println(gsender->getError());
    }
  }
  // Deep Sleep
  min = SLEEP_INTERVAL - minute() % SLEEP_INTERVAL;
  Serial.println(getDateTime() + " " + "Go to sleep for " + min+ " minutes.");
  ESP.deepSleep((min * 60 - second() + 10) * 1000 * 1000, WAKE_RF_DEFAULT); 
}

// getDateTime
String getDateTime() {
  char dt[20];
  sprintf(dt, "%04d/%02d/%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
  return String(dt);
}

#if IFTTT_ENABLE == 1
// IFTTTにリクエストを送る
void RequestToIFTTT(int illuminance) {
  
  WiFiClientSecure client;
  
  String param = "?value1=" + String(illuminance);
  const char *IFTTT_URL;
  
  while (!client.connect(IFTTT_HOST, 443)) {
    delay(10);
  }
  
  if(illuminance > 300) {
    IFTTT_URL = IFTTT_URI_USE;
  }
  else {
    IFTTT_URL = IFTTT_URI_OPEN;
  }

  //Serial.println(IFTTT_URL);
  //Serial.println(param);
  client.print(String("GET ") + IFTTT_URL + param +
                       " HTTP/1.1\r\n" +
                       "Host: " + IFTTT_HOST + "\r\n" +
                       "User-Agent: ESP8266\r\n" +
                       "Connection: close\r\n\r\n");
  while (!client.available()) {
    delay(10);
  }
  Serial.println(client.readStringUntil('\r'));
  client.flush();
  client.stop();
}
#endif
