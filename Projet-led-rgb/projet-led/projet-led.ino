// Code de test ultra simple pour ESP32-C6
// Clignote la LED + messages dans le Serial Monitor

#define LED_PIN 8          // LED intégrée la plus courante sur ESP32-C6 DevKit / Dev Module
                           // Si ça ne clignote pas, essaie 7, 9, 10 ou 15 selon ta carte

void setup() {
  Serial.begin(115200);    // Vitesse standard pour ESP32
  delay(200);              // Petit délai pour que le moniteur série se connecte
  Serial.println("Démarrage du test !");
  Serial.println("Si tu vois ce message → la communication série fonctionne");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);   // Éteint au démarrage
}

void loop() {
  Serial.println("LED ON");
  digitalWrite(LED_PIN, HIGH);   // Allume la LED
  delay(500);

  Serial.println("LED OFF");
  digitalWrite(LED_PIN, LOW);    // Éteint la LED
  delay(500);
}
