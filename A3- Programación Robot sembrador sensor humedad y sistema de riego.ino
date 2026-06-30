#include "arduino_secrets.h"
#include "thingProperties.h"
#include <ESP32Servo.h>

Servo miServo;

const int pinSensor = 34;
const int pinServo = 18;
const int pinBomba = 5; 

// Variables de Control
int fase = 0; 
int contadorCiclos = 0; 
unsigned long tiempoReferencia = 0;
unsigned long ultimoSegundo = 0;
int sumaLecturas = 0;
int contadorLecturas = 0;
unsigned long tiempoRiego = 2000; // 2 segundos de riego

// Control de envío único por evento de humedad
bool yaEnviadoHumedo = false; 

// Rangos ajustados de forma estricta
const int seco = 3151;
const int humedo_ok_min = 2651;
const int humedo_ok_max = 3150;
const int muy_humedo = 2650;

void setup() {
  Serial.begin(9600);
  delay(1500);
  
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  miServo.attach(pinServo);
  miServo.write(0);

  pinMode(pinBomba, OUTPUT);
  digitalWrite(pinBomba, LOW); 
  
  Serial.println("Sistema listo. Esperando pulsacion en Dashboard.");
}

void loop() {
  ArduinoCloud.update();
  unsigned long currentMillis = millis();

  // FASE 1: ESPERA DE 7 SEGUNDOS
  if (fase == 1) {
    if (currentMillis - tiempoReferencia >= 7000) {
      fase = 2;
      ultimoSegundo = currentMillis;
      sumaLecturas = 0;
      contadorLecturas = 0;
      Serial.print("Iniciando Medida - Ciclo: "); Serial.println(contadorCiclos + 1);
    }
  }

  // FASE 2: MUESTREO (4 segundos)
  else if (fase == 2) {
    if (currentMillis - ultimoSegundo >= 1000) {
      int lectura = analogRead(pinSensor);
      sumaLecturas += lectura;
      contadorLecturas++;
      ultimoSegundo = currentMillis;

      if (contadorLecturas >= 4) {
        float promedio = (float)sumaLecturas / 4.0; 
        
        // 1. Asignamos valores numéricos
        humedadSuelo = (int)promedio;
        int porcentaje = map(promedio, 3700, 1800, 0, 100);
        porcentaje = constrain(porcentaje, 0, 100); 
        humedadPorcentaje = porcentaje;

        // 2. Asignación del estado real de humedad
        if (promedio >= seco) {
          estadoHumedad = "SECO";
          yaEnviadoHumedo = false; 
        } else if (promedio <= muy_humedo) {
          estadoHumedad = "MUY HÚMEDO";
        } else if (promedio >= humedo_ok_min && promedio <= humedo_ok_max) {
          estadoHumedad = "HUMEDAD OK";
          yaEnviadoHumedo = false; 
        } else {
          estadoHumedad = "INTERMEDIO";
          yaEnviadoHumedo = false; 
        }

        Serial.print("Ciclo "); Serial.print(contadorCiclos + 1);
        Serial.print(" - Promedio: "); Serial.print(promedio);
        Serial.print(" - Estado: "); Serial.println(estadoHumedad);

        contadorCiclos++;

        // Clasificación de acciones según el estado obtenido
        if (estadoHumedad == "HUMEDAD OK") {
          miServo.write(40);
          fase = 3; 
          ultimoSegundo = currentMillis; 
        } 
        else if (estadoHumedad == "MUY HÚMEDO") {
          // SOLO envía y mantiene las coordenadas si es la primera vez que entra en este estado
          if (!yaEnviadoHumedo) {
            String gpsData = String(posicion.getValue().lat, 6) + ", " + String(posicion.getValue().lon, 6);
            coordenadas = gpsData;
            coordenadasAlerta = gpsData; 
            yaEnviadoHumedo = true;      
            Serial.print("¡Alerta de Humedad! Coordenadas enviadas: "); Serial.println(gpsData);
          } else {
            Serial.println("El suelo sigue MUY HÚMEDO. Coordenadas retenidas en pantalla.");
          }
          
          miServo.write(0);
          gestionarSiguientePaso(currentMillis);
        }
        else if (estadoHumedad == "SECO") {
          miServo.write(0);
          digitalWrite(pinBomba, HIGH); 
          Serial.println("Bomba ACTIVADA - Regando...");
          fase = 4; 
          ultimoSegundo = currentMillis; 
        }
        else { 
          miServo.write(0);
          gestionarSiguientePaso(currentMillis);
        }
      }
    }
  }

  // FASE 3: ACCION SERVO (7 segundos)
  else if (fase == 3) {
    if (currentMillis - ultimoSegundo >= 7000) { 
      miServo.write(0);
      gestionarSiguientePaso(currentMillis);
    }
  }

  // FASE 4: ACCION BOMBA DE RIEGO (10 segundos)
  else if (fase == 4) {
    if (currentMillis - ultimoSegundo >= tiempoRiego) { 
      digitalWrite(pinBomba, LOW); 
      Serial.println("Bomba APAGADA - Fin del riego.");
      gestionarSiguientePaso(currentMillis); 
    }
  }

  // FASE 5: RETENCIÓN EN PANTALLA ANTES DE LIMPIEZA TOTAL (5 segundos)
  else if (fase == 5) {
    if (currentMillis - tiempoReferencia >= 5000) {
      Serial.println("Pasados los 5 segundos de cortesía. Ejecutando LIMPIEZA TOTAL.");
      fase = 0;
      maizSwitch = false; 
      contadorCiclos = 0;
      yaEnviadoHumedo = false;

      // Limpieza absoluta del dashboard sin textos
      humedadSuelo = 0;
      humedadPorcentaje = 0;
      estadoHumedad = ""; 
      coordenadas = "";
      coordenadasAlerta = "";
    }
  }
}

// Controla si repetimos ciclo o si pasamos a la fase de retención de datos
void gestionarSiguientePaso(unsigned long tiempoFinalizacion) {
  if (contadorCiclos >= 2) {
    Serial.println("Completados 2 ciclos. Iniciando espera de 5 segundos en pantalla...");
    fase = 5; // Saltamos a la fase de espera en lugar de borrar todo ya
    tiempoReferencia = tiempoFinalizacion; 
  } else {
    // Si pasamos al ciclo 2 y el suelo YA NO está muy húmedo, limpiamos el rastro del ciclo 1
    if (estadoHumedad != "MUY HÚMEDO") {
      coordenadas = "";
      coordenadasAlerta = "";
    }
    Serial.println("Repitiendo: Volviendo a Fase 1 (Espera de 7s reales).");
    fase = 1;
    tiempoReferencia = tiempoFinalizacion; 
  }
}

void onMaizSwitchChange() {
  if (maizSwitch) {
    Serial.println("Boton Maiz ON");
    
    miServo.write(40);
    delay(1300); 
    miServo.write(0);
    
    contadorCiclos = 0;
    sumaLecturas = 0;
    contadorLecturas = 0;
    yaEnviadoHumedo = false;
    
    coordenadas = "";
    coordenadasAlerta = "";
    estadoHumedad = "";
    
    digitalWrite(pinBomba, LOW); 
    
    fase = 1;
    tiempoReferencia = millis(); 
    calabacinSwitch = false; 
  } else {
    fase = 0;
    miServo.write(0);
    digitalWrite(pinBomba, LOW); 
    yaEnviadoHumedo = false;
    
    humedadSuelo = 0;
    humedadPorcentaje = 0;
    estadoHumedad = "";
    coordenadas = "";
    coordenadasAlerta = "";
    Serial.println("Boton Maiz OFF manual. Dashboard limpio.");
  }
}

void onCalabacinSwitchChange() {
  if (calabacinSwitch) {
    maizSwitch = false;
    fase = 0;
    miServo.write(0);
    digitalWrite(pinBomba, LOW); 
    yaEnviadoHumedo = false;
    
    humedadSuelo = 0;
    humedadPorcentaje = 0;
    estadoHumedad = "";
    coordenadas = "";
    coordenadasAlerta = "";
  }
}

void onCoordenadasChange() {}
void onPosicionChange() {}