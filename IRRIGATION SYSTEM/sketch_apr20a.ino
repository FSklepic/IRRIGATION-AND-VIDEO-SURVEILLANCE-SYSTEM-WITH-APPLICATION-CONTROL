#define TINY_GSM_MODEM_SIM7000
#define SerialMon Serial
#define SerialAT Serial1

#include <TinyGsmClient.h>
#include <PubSubClient.h>

// --- CONFIGURATION ---
const char apn[]      = "internet.ht.hr";
const char* broker    = "broker.hivemq.com";
const char* topicCommand = "urs/filip/ventil/naredba";
const char* topicStatus  = "urs/filip/ventil/status"; 

// --- PIN DEFINE ---
#define PIN_TX          27
#define PIN_RX          26
#define PWR_PIN         4 // Pin that turns on/off modem
#define PIN_IN1         32 // L298N Input 1
#define PIN_IN2         33 // L298N Input 2

// --- DEEP SLEEP SETTINGS ---
#define uS_TO_S_FACTOR 1000000ULL  
#define VALVE_IMPULS_TIME 2000       // 2 seconds impuls for latching valve
#define TIME_TO_SLEEP   10           // TESTING: Sleep for 10 seconds

// VARIABLE IN RTC MEMORY - longterm memory (survives shutdown)
RTC_DATA_ATTR bool valveOpen = false; 
bool messageProcessed = false; 

TinyGsm        modem(SerialAT); // Connects modem with my code
TinyGsmClient  client(modem); // Takes my modem and turns it into internet client (TCP/IP)
PubSubClient   mqtt(client); // Connection to mqtt (pipe for conversation)

// --- FUNCTION FOR OPERATING VALVES ---
void operateValve(bool open) {
    SerialMon.println(open ? "\n[L298N] Otvaranje ventila..." : "\n[L298N] Zatvaranje ventila...");

    // Setup a direction of current (Polarity reversal for latching valve)
    if (open) {
        digitalWrite(PIN_IN1, HIGH); 
        digitalWrite(PIN_IN2, LOW);  
    } else {
        digitalWrite(PIN_IN1, LOW);  
        digitalWrite(PIN_IN2, HIGH); 
    }

    // Send impulse
    delay(VALVE_IMPULS_TIME);

    // Turn off and return driver to sleep (Cut power to coil)
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);

    SerialMon.println("[L298N] Operacija zavrsena, struja iskljucena.");
}

// --- MODEM POWER ON ---
void modemPowerOn() {
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, LOW);
    delay(1200); 
    digitalWrite(PWR_PIN, HIGH);
}

// --- SLEEP FUNCTION ---
void gotoSleep() {
    SerialMon.println("\n--- GASENJE SUSTAVA ---");
    if (mqtt.connected()) {
        mqtt.disconnect();
    } 

    modem.gprsDisconnect();
    
    SerialMon.println("Gasim modem (poweroff)...");
    modem.poweroff(); 
    
    SerialMon.println("Ulazim u Deep Sleep (30s).");
    delay(100);
    
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}

// --- MQTT CALLBACK ---
void mqttCallback(char *topic, byte *payload, unsigned int len) {
    String message = "";
    for (unsigned int i = 0; i < len; i++) message += (char)payload[i];
    
    SerialMon.println("[MQTT] Primljeno: " + message);
    bool targetState = (message == "1" || message == "true");

    // KEY LOGIC: Impuls is sent only on state change
    if (targetState != valveOpen) {
        operateValve(targetState);
        valveOpen = targetState; // Save new state in RTC memory
        mqtt.publish(topicStatus, targetState ? "1" : "0", true);

    } else {
        SerialMon.println("[MQTT] Ventil je vec u trazenom stanju. Preskacem impuls.");
    }
    messageProcessed = true; // Confirmation that message has been processed
}

// --- CONNECTION TO MQTT ---
boolean mqttConnect() {
    SerialMon.print("[MQTT] Spajanje na broker...");
    // ID mora biti unikatan
    if (mqtt.connect("LilyGo_Filip_Irrigation_System")) {
        SerialMon.println(" uspjeh!");
        mqtt.subscribe(topicCommand);
        return true;
    }
    SerialMon.println(" neuspjeh.");
    return false;
}

// --- SETUP ---
void setup() {
    SerialMon.begin(115200);
    delay(100);
    SerialMon.println("\n\n==============================");
    SerialMon.println("    CIKLUS PROVJERE VENTILA    ");
    SerialMon.println("==============================");

    // --- PIN INITIALIZATION ---
    pinMode(PIN_IN1, OUTPUT);
    pinMode(PIN_IN2, OUTPUT);
    
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);

    modemPowerOn();
    SerialMon.println("Inicijalizacija modema...");
    delay(6000); 

    SerialAT.begin(9600, SERIAL_8N1, PIN_RX, PIN_TX); // Start UART communication (ESP32 - SIM7000G)

    if (!modem.init()) { 
        SerialMon.println("Modem ne odgovara."); 
        gotoSleep(); 
    }
    
    SerialMon.print("Mreza...");
    if (!modem.waitForNetwork(60000L)) { 
        SerialMon.println(" nedostupna."); 
        gotoSleep(); 
    }
    SerialMon.println(" OK.");
    
    SerialMon.print("GPRS...");
    if (!modem.gprsConnect(apn, "", "")) { 
        SerialMon.println(" greska."); 
        gotoSleep(); 
    }
    SerialMon.println(" OK.");

    mqtt.setServer(broker, 1883);
    mqtt.setCallback(mqttCallback); // Call this function if you ever get message from internet
}

// --- LOOP ---
void loop() {
    if (!mqtt.connected()) {
        if (!mqttConnect()) {
            static int fails = 0;
            if (fails++ > 2) gotoSleep();
            delay(3000);
            return;
        }
    }

    mqtt.loop(); // Check memory and jump to mqttCallback. When finished return to this point

    static unsigned long startMillis = millis();
    // If message has been processed or 15 seconds has elapsed
    if (messageProcessed || (millis() - startMillis > 15000)) {
        if (!messageProcessed) SerialMon.println("[INFO] Nema novih naredbi (Timeout).");
        delay(2000); 
        gotoSleep();
    }
}