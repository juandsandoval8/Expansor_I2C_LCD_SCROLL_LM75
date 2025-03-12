
#include "mbed.h"

#define DIRECCION_RTC 0xD0
#define REG_SEGUNDOS 0x00
#define REG_MINUTOS 0x01
#define REG_HORAS 0x02
#define REG_DIA 0x04
#define REG_MES 0x05
#define REG_ANIO 0x06
#define REG_DIA_SEMANA 0x03
#define MCP23017 (0x40)


I2C i2c1(PB_9, PB_8);
UnbufferedSerial pc(USBTX, USBRX, 9600);
//UnbufferedSerial serial(PA_9, PC_7, 9600); // Usa los pines de USBTX y USBRX
// Definir pines
DigitalOut dirpin(PC_3); // Pines de dirección
DigitalOut steppin(PC_2); // Pines de pasos
DigitalOut enable(PH_1); // Pin de habilitación

// Variables para velocidad y total de pasos
int velocidad; // En microsegundos entre pasos
int total_pasos; // Total de pasos a realizar

const int DIRECCIONES_SENSORES[8] = {0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};
int parteEnteraTemp[8], parteDecimalTemp[8];

uint16_t estadoLEDs; // Almacena el estado de los LEDs


char segundosRTC, minutosRTC, horasRTC, diaRTC, mesRTC, anoRTC;
int sensorSeleccionado = 0;  // Sensor seleccionado por los botones
bool actualizarLCD = false;

Timer temporizador;
Timeout debounceTimeout;
volatile bool debounceActive = false;

InterruptIn botonArriba(PA_1, PullUp);
InterruptIn botonAbajo(PA_0, PullUp);

Thread hiloRTC;
Thread hiloLEDS;
Thread serial_paso_paso;

char LeerRegistroRTC(char reg) {
    i2c1.write(DIRECCION_RTC, &reg, 1);
    char data;
    i2c1.read(DIRECCION_RTC, &data, 1);
    return data;
}

void LeerDatosRTC() {
    segundosRTC = LeerRegistroRTC(0x00);
    minutosRTC = LeerRegistroRTC(0x01);
    horasRTC = LeerRegistroRTC(0x02);
    diaRTC = LeerRegistroRTC(0x04);
    mesRTC = LeerRegistroRTC(0x05);
    anoRTC = LeerRegistroRTC(0x06);
}

void LeerTemperaturas() {
    for (int i = 0; i < 8; i++) {
        char cmd[1] = {0x00};
        if (i2c1.write(DIRECCIONES_SENSORES[i] << 1, cmd, 1) == 0) {
            char data[2];
            if (i2c1.read(DIRECCIONES_SENSORES[i] << 1, data, 2) == 0) {
                float temp = (float((data[0] << 8) | data[1]) / 256.0);
                float tempEnCelsius = temp * 100;
                int tempTotal = (int)tempEnCelsius;
                parteEnteraTemp[i] = tempTotal / 100;
                parteDecimalTemp[i] = tempTotal % 100;
            } else {
                parteEnteraTemp[i] = 0;
                parteDecimalTemp[i] = 0;
            }
        } else {
            parteEnteraTemp[i] = 0;
            parteDecimalTemp[i] = 0;
        }
    }
}

void mover_motor(bool direccion, int total_pasos) {
    // Configuración de dirección
    dirpin = direccion;

    for (int i = 0; i < total_pasos; i++) {
        steppin = 0;
        wait_us(velocidad); // Esperar según la velocidad especificada
        steppin = 1;
        wait_us(velocidad); // Esperar según la velocidad especificada
    }
}


void EnviarDatosMatlab() {
    char buffer[128];

    if (pc.readable()) {
    char buffer1[256];
    int n = pc.read(buffer, sizeof(buffer) - 1);
    buffer[n] = '\0';

    // Parsear los valores de velocidad y total de pasos
    sscanf(buffer, "%d %d", &velocidad, &total_pasos); // Leer velocidad y total de pasos
    }

    // Enviar temperatura del sensor seleccionado
    int longitud = sprintf(buffer, "%d: %d.%d °C\n", sensorSeleccionado + 1, parteEnteraTemp[sensorSeleccionado], parteDecimalTemp[sensorSeleccionado]);
    pc.write(buffer, longitud);

    // Enviar fecha y hora
    longitud = sprintf(buffer, "%02x/%02x/20%02x %02x:%02x:%02x\n", diaRTC, mesRTC, anoRTC, horasRTC, minutosRTC, segundosRTC);
    pc.write(buffer, longitud);

    // Enviar el estado de los LEDs como una secuencia de bits
    longitud = sprintf(buffer, "Estado de los LEDs: ");
    pc.write(buffer, longitud);
    for (int i = 15; i >= 0; i--) {
        char bit = (estadoLEDs & (1 << i)) ? '1' : '0';
        pc.write(&bit, 1);
    }
    pc.write("\n", 1);


    longitud = sprintf(buffer, "Valor de estadoLEDs: %04X\n", estadoLEDs);
    pc.write(buffer, longitud);

}

void CambiarSensorArriba() {
    if (!debounceActive) {
        debounceActive = true;
        sensorSeleccionado++;
        if (sensorSeleccionado > 7)
            sensorSeleccionado = 0;
        actualizarLCD = true;
        debounceTimeout.attach([] { debounceActive = false; }, 300ms);
    }
}

void CambiarSensorAbajo() {
    if (!debounceActive) {
        debounceActive = true;
        sensorSeleccionado--;
        if (sensorSeleccionado < 0)
            sensorSeleccionado = 7;
        actualizarLCD = true;
        debounceTimeout.attach([] { debounceActive = false; }, 300ms);
    }
}

void EscribirRegistroRTC(char reg, char valor) {
    char datos[2] = {reg, valor};
    i2c1.write(DIRECCION_RTC, datos, 2);
}

void ConfigurarRTC(char segundos, char minutos, char horas, char dia, char mes, char anio) {
    EscribirRegistroRTC(REG_SEGUNDOS, segundos);
    EscribirRegistroRTC(REG_MINUTOS, minutos);
    EscribirRegistroRTC(REG_HORAS, horas);
    EscribirRegistroRTC(REG_DIA, dia);
    EscribirRegistroRTC(REG_MES, mes);
    EscribirRegistroRTC(REG_ANIO, anio);
}

void escrituraExpansor(uint8_t reg, uint16_t data) {
    char cmd[3];
    cmd[0] = reg;
    cmd[1] = data & 0xFF;
    cmd[2] = (data >> 8) & 0xFF;
    i2c1.write(MCP23017, cmd, 3);
}

void ControlarLEDs() {
    // Inicializa el estado de los LEDs en función de la temperatura
    static bool inicializado = false;
    if (!inicializado) {
        escrituraExpansor(0x00, 0x00);  // Configura IODIRA como salida
        escrituraExpansor(0x01, 0x00);  // Configura IODIRB como salida
        inicializado = true;  // Marca como inicializado
    }

    // Aquí podrías asignar un valor predeterminado para el estadoLEDs
    estadoLEDs = 0; // O puedes configurar un valor predeterminado si no hay lectura

    int temp = parteEnteraTemp[sensorSeleccionado];

    // Define el estado de los LEDs según la temperatura
    if (temp < 7) {
        estadoLEDs = 0x0001;  // 1 LED
    } else if (temp < 13) {
        estadoLEDs = 0x0003;  // 2 LEDs
    } else if (temp < 19) {
        estadoLEDs = 0x0007;  // 3 LEDs
    } else if (temp < 25) {
        estadoLEDs = 0x000F;  // 4 LEDs
    } else if (temp < 31) {
        estadoLEDs = 0x001F;  // 5 LEDs
    } else if (temp < 38) {
        estadoLEDs = 0x003F;  // 6 LEDs
    } else if (temp < 44) {
        estadoLEDs = 0x007F;  // 7 LEDs
    } else if (temp < 50) {
        estadoLEDs = 0x00FF;  // 8 LEDs
    } else if (temp < 56) {
        estadoLEDs = 0x01FF;  // 9 LEDs
    } else if (temp < 63) {
        estadoLEDs = 0x03FF;  // 10 LEDs
    } else if (temp < 69) {
        estadoLEDs = 0x07FF;  // 11 LEDs
    } else if (temp < 75) {
        estadoLEDs = 0x0FFF;  // 12 LEDs
    } else if (temp < 81) {
        estadoLEDs = 0x1FFF;  // 13 LEDs
    } else if (temp < 88) {
        estadoLEDs = 0x3FFF;  // 14 LEDs
    } else if (temp < 94) {
        estadoLEDs = 0x7FFF;  // 15 LEDs
    } else {
        estadoLEDs = 0xFFFF;  // 16 LEDs
    }
    
    // Enviar el estado de los LEDs al expansor
    char cmd[3] = {0x12, (char)(estadoLEDs & 0xFF), (char)(estadoLEDs >> 8)};
    i2c1.write(MCP23017, cmd, 3);
}

void ActualizarRTC() {
    while (true) {
        if (temporizador.elapsed_time() >= 1s) {
            LeerDatosRTC();
            LeerTemperaturas();
            ControlarLEDs();
            EnviarDatosMatlab();
            temporizador.reset();
        }
        ThisThread::sleep_for(500ms);
    }
}

char BCD(char valor) {
    return ((valor / 10) << 4) | (valor % 10);
}

void setup() {
    // Configuración inicial
    enable = 1; // Habilitar el motor
    pc.set_blocking(false); // No bloquear la lectura
}

void controlar_motor() {
    while (true) {
        

        // Solo mover el motor si tenemos valores válidos
        if (total_pasos > 0) {
            // Rotación en sentido horario
            mover_motor(1, total_pasos);
            thread_sleep_for(100); // Pausa de 100 ms

            // Rotación en sentido antihorario
            mover_motor(0, total_pasos);
            thread_sleep_for(100); // Pausa de 100 ms
        }

        ThisThread::sleep_for(100ms); // Dormir un poco para no saturar el bucle
    }
}

int main() {
    temporizador.start();
    setup();

    botonArriba.fall(&CambiarSensorArriba);
    botonAbajo.fall(&CambiarSensorAbajo);

    ConfigurarRTC(BCD(0), BCD(11), BCD(13), BCD(4), BCD(10), BCD(24));
    hiloRTC.start(ActualizarRTC);
    hiloLEDS.start(ControlarLEDs);
    Thread hiloMotor;
    hiloMotor.start(controlar_motor);

    while (true) {
        
        thread_sleep_for(500); // Pausa de 100 ms
    }
}
