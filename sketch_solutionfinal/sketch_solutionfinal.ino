#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>
#include <WiFi.h>

const char* ssid = "ESP_Temperature";
const char* password = "123456789A";

WebServer server(80);

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Capteurs LM35
#define LM35_PIN1 32
#define LM35_PIN2 35

// TLP250 (relais pour la lampe)
#define RELAY_PIN 6

// Utilisation du bouton BOOT de l'ESP32
#define BUTTON_PIN 4  // GPIO0 = bouton BOOT

// Ventilateur 5V commandé via relais
#define FAN_PIN 18

// Buzzer
#define BUZZER_PIN 19

// Température seuil
const float TEMP_SEUIL = 45.0;

// États système
bool lampeAllumee = false;      // système éteint par défaut
bool cycleActif = false;        // cycle inactif au départ
bool ventilateurActif = false;
bool buzzerActif = false;
bool etatBuzzer = false;
bool ventilateurTermine = false;  // Empêche le redémarrage du ventilateur

// Indique si au moins un cycle a déjà été démarré (appui bouton)
bool cycleDemarreAuMoinsUneFois = false;

// Temps pour contrôle des délais
unsigned long tempsDepassement = 0;
unsigned long tempsAttenteVentilateur = 0;
unsigned long tempsVentilateurAllume = 0;
unsigned long tempsBuzzerDebut = 0;
unsigned long dernierToggleBuzzer = 0;

const int MAX_BUTTON_PRESSES = 10;  // Maximum number of records to store
unsigned long buttonPressTimestamps[MAX_BUTTON_PRESSES];
float buttonPressTemperatures[MAX_BUTTON_PRESSES];  // New array for temperatures
int buttonPressCount = 0;

void setup() {
    Serial.begin(115200);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);  // Lampe éteinte par défaut

    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);

    pinMode(BUTTON_PIN, INPUT_PULLUP);  // Bouton BOOT

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    lcd.init();
    lcd.backlight();

    WiFi.softAP(ssid, password);

    server.on("/temperature", HTTP_GET, []() {
        int valeur1 = analogRead(LM35_PIN1);
        int valeur2 = analogRead(LM35_PIN2);
        float temperature1 = (valeur1 * 5.0 / 4095.0) * 100.0;
        float temperature2 = (valeur2 * 5.0 / 4095.0) * 100.0;
        float temperature_moyenne = (temperature1 + temperature2) / 2.0;

        String json = "{\"temperature\": " + String(temperature_moyenne, 2) + "}";
        server.send(200, "application/json", json);
    });

    // New route to get button press history
    server.on("/button-history", HTTP_GET, []() {
        String json = "{\"button_presses\": [";
        for (int i = 0; i < buttonPressCount; i++) {
            if (i > 0) json += ",";
            json += "{\"timestamp\": " + String(buttonPressTimestamps[i]) + 
                   ", \"temperature\": " + String(buttonPressTemperatures[i], 2) + "}";
        }
        json += "], \"count\": " + String(buttonPressCount) + "}";
        server.send(200, "application/json", json);
    });

    server.begin();
}

void loop() {

    server.handleClient();
    
    int valeur1 = analogRead(LM35_PIN1);
    int valeur2 = analogRead(LM35_PIN2);

    float temperature1 = (valeur1 * 5.0 / 4095.0) * 100.0;
    float temperature2 = (valeur2 * 5.0 / 4095.0) * 100.0;
    float temperature_moyenne = (temperature1 + temperature2) / 2.0;

    // Affichage LCD
    lcd.setCursor(0, 1);
    lcd.print("Lampe:");
    lcd.print(lampeAllumee ? "ON " : "OFF");

    lcd.setCursor(9, 1);
    lcd.print("Fan:");
    lcd.print(ventilateurActif ? "ON " : "OFF");
    
    lcd.setCursor(0, 0);
    lcd.print("Moy:");
    lcd.print(temperature_moyenne, 1);
    lcd.print("C   ");

    Serial.print("Température moyenne : ");
    Serial.println(temperature_moyenne);

    if (cycleActif) {
        if (temperature_moyenne > TEMP_SEUIL && lampeAllumee) {
            if (tempsDepassement == 0) {
                tempsDepassement = millis();
            }

            if (millis() - tempsDepassement >= 10000) {
                digitalWrite(RELAY_PIN, LOW);
                lampeAllumee = false;
                cycleActif = false;
                tempsDepassement = 0;

                buzzerActif = true;
                tempsBuzzerDebut = millis();
                dernierToggleBuzzer = millis();
                etatBuzzer = true;
                digitalWrite(BUZZER_PIN, etatBuzzer);

                Serial.println("Lampe ÉTEINTE après 10s");
            }
        } else {
            tempsDepassement = 0;
        }
    }

    // Ventilateur : démarre après 10s, une seule fois, seulement si un cycle a déjà été démarré
    if (!cycleActif && !ventilateurActif && !ventilateurTermine && lampeAllumee == false && cycleDemarreAuMoinsUneFois) {
        if (tempsAttenteVentilateur == 0) {
            tempsAttenteVentilateur = millis();
        } else if (millis() - tempsAttenteVentilateur >= 10000) {
            digitalWrite(FAN_PIN, HIGH);
            ventilateurActif = true;
            tempsVentilateurAllume = millis();
            Serial.println("Ventilateur ALLUMÉ (après 10s)");
        }
    }

    if (ventilateurActif && (millis() - tempsVentilateurAllume >= 10000)) {
        digitalWrite(FAN_PIN, LOW);
        ventilateurActif = false;
        ventilateurTermine = true;
        Serial.println("Ventilateur ÉTEINT (fin de 10s, plus jamais actif)");
    }

    if (cycleActif && ventilateurActif) {
        digitalWrite(FAN_PIN, LOW);
        ventilateurActif = false;
        tempsAttenteVentilateur = 0;
        Serial.println("Ventilateur ÉTEINT (nouveau cycle)");
    }

    // Bouton pour démarrer un nouveau cycle
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50);
        if (digitalRead(BUTTON_PIN) == LOW) {
            // Store button press timestamp and current temperature
            if (buttonPressCount < MAX_BUTTON_PRESSES) {
                buttonPressTimestamps[buttonPressCount] = millis();
                buttonPressTemperatures[buttonPressCount] = temperature_moyenne;
                buttonPressCount++;
                Serial.println("Button press recorded at: " + String(millis()) + 
                             ", temp: " + String(temperature_moyenne, 2) + "C");
            } else {
                // If array is full, shift elements and add new one
                for (int i = 0; i < MAX_BUTTON_PRESSES - 1; i++) {
                    buttonPressTimestamps[i] = buttonPressTimestamps[i + 1];
                    buttonPressTemperatures[i] = buttonPressTemperatures[i + 1];
                }
                buttonPressTimestamps[MAX_BUTTON_PRESSES - 1] = millis();
                buttonPressTemperatures[MAX_BUTTON_PRESSES - 1] = temperature_moyenne;
                Serial.println("Button press recorded (array full, oldest removed), temp: " + 
                             String(temperature_moyenne, 2) + "C");
            }

            digitalWrite(RELAY_PIN, HIGH);
            lampeAllumee = true;
            cycleActif = true;

            cycleDemarreAuMoinsUneFois = true;

            tempsDepassement = 0;
            tempsAttenteVentilateur = 0;
            tempsVentilateurAllume = 0;
            ventilateurTermine = false;

            if (ventilateurActif) {
                digitalWrite(FAN_PIN, LOW);
                ventilateurActif = false;
                Serial.println("Ventilateur ÉTEINT (appui bouton)");
            }

            if (buzzerActif) {
                digitalWrite(BUZZER_PIN, LOW);
                buzzerActif = false;
                etatBuzzer = false;
                Serial.println("Buzzer ÉTEINT (appui bouton)");
            }

            Serial.println("Cycle démarré par bouton");
            while (digitalRead(BUTTON_PIN) == LOW);  // attendre relâchement
        }
    }

    if (buzzerActif) {
        if (millis() - tempsBuzzerDebut >= 5000) {
            digitalWrite(BUZZER_PIN, LOW);
            buzzerActif = false;
            etatBuzzer = false;
            Serial.println("Buzzer ÉTEINT après 5s");
        } 
        else if (millis() - dernierToggleBuzzer >= 500) {
            etatBuzzer = !etatBuzzer;
            digitalWrite(BUZZER_PIN, etatBuzzer);
            dernierToggleBuzzer = millis();
        }
    }

    delay(500);
}
