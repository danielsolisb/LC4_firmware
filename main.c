// main.c
// Autor: Daniel Solis
// fecha 7/7/2025
#include <xc.h>
#include "config.h"
#include "uart.h"
#include "eeprom.h"
#include "rtc.h"
#include "timers.h"
#include "scheduler.h"
#include "sequence_engine.h"

// =============================================================================
// --- DEFINICIONES GLOBALES Y PROTOTIPOS ---
// =============================================================================

// Esta bandera evitar� que la ISR escanee las entradas antes de que el sistema est� listo.
volatile bool g_system_ready = false;

// Prototipo de la nueva funci�n para que sea visible desde la ISR
void Inputs_ScanTask(void);

// Definici�n de las banderas globales para las entradas P1 a P4.
// Aqu� es donde se define y se crea la variable.
volatile bool g_demand_flags[4] = {false, false, false, false};

// --- L�GICA PARA EL SWITCH DE MANTENIMIENTO ---
#define MANUAL_FLASH_PIN PORTJbits.RJ5
#define DEBOUNCE_THRESHOLD 50
static bool g_manual_flash_active = false;

// --- L�GICA DE SONDEO DE ENTRADAS (M�QUINA DE ESTADOS) ---
#define DEBOUNCE_TICKS 5 // Necesitamos 5 ticks (50ms) de estado estable para confirmar.

// Estados para nuestra m�quina de antirrebote
typedef enum {
    STATE_IDLE,      // El bot�n est� liberado, esperando a ser presionado.
    STATE_DEBOUNCE,  // Se detect� un cambio, esperando a que se estabilice.
    STATE_PRESSED    // El bot�n est� confirmado como presionado.
} InputState_t;

static InputState_t input_state[4] = {STATE_IDLE, STATE_IDLE, STATE_IDLE, STATE_IDLE};
static uint8_t debounce_counter[4] = {0, 0, 0, 0};


// =============================================================================
// --- IMPLEMENTACI�N DE FUNCIONES ---
// =============================================================================

/**
 * @brief Pone a cero todas las banderas de demanda.
 * @details Se llamar� desde el motor de secuencias despu�s de evaluar un Punto de Decisi�n.
 */
void Demands_ClearAll(void) {
    for(uint8_t i = 0; i < 4; i++) {
        g_demand_flags[i] = false;
    }
}

/**
 * @brief Escanea las entradas de demanda con una m�quina de estados.
 * @details Esta funci�n DEBE ser llamada a intervalos fijos y regulares
 * (ej. cada 10ms) desde una interrupci�n de temporizador.
 */
void Inputs_ScanTask(void) {
    // Leer el estado f�sico actual de los pines
    bool raw_inputs[4];
    raw_inputs[0] = (P1 == 1); // Lee el estado de P1 (RJ7)
    raw_inputs[1] = (P2 == 1); // Lee el estado de P2 (RH5)
    raw_inputs[2] = (P3 == 1); // Lee el estado de P3 (RH6)
    raw_inputs[3] = (P4 == 1); // Lee el estado de P4 (RH7)

    for (uint8_t i = 0; i < 4; i++) {
        switch (input_state[i]) {
            case STATE_IDLE:
                // Si estamos en reposo y se detecta una pulsaci�n...
                if (raw_inputs[i] == true) {
                    input_state[i] = STATE_DEBOUNCE; // ...pasamos a la fase de antirrebote.
                    debounce_counter[i] = 0; // Reiniciamos el contador.
                }
                break;

            case STATE_DEBOUNCE:
                // Si la entrada sigue presionada...
                if (raw_inputs[i] == true) {
                    debounce_counter[i]++; // ...incrementamos el contador.
                    // Si hemos alcanzado los ticks necesarios...
                    if (debounce_counter[i] >= DEBOUNCE_TICKS) {
                        // ...confirmamos la pulsaci�n.
                        input_state[i] = STATE_PRESSED;
                        g_demand_flags[i] = true; // �AQU� SE ACTIVA LA BANDERA!
                    }
                } else {
                    // Si fue un falso positivo (ruido), volvemos a reposo.
                    input_state[i] = STATE_IDLE;
                }
                break;

            case STATE_PRESSED:
                // Nos mantenemos en este estado mientras el bot�n siga presionado.
                // Cuando se libera, volvemos a reposo para poder detectar la pr�xima pulsaci�n.
                if (raw_inputs[i] == false) {
                    input_state[i] = STATE_IDLE;
                }
                break;
        }
    }
}

/**
 * @brief Maneja la l�gica del switch de mantenimiento.
 */
static void HandleManualFlashSwitch(void) {
    static uint8_t press_counter = 0;
    static uint8_t release_counter = 0;

    if (MANUAL_FLASH_PIN == 1) {
        release_counter = 0;
        if (press_counter < DEBOUNCE_THRESHOLD) {
            press_counter++;
        }
    } else {
        press_counter = 0;
        if (release_counter < DEBOUNCE_THRESHOLD) {
            release_counter++;
        }
    }

    if (press_counter == DEBOUNCE_THRESHOLD) {
        if (!g_manual_flash_active) {
            g_manual_flash_active = true;
            Sequence_Engine_EnterManualFlash();
        }
    } else if (release_counter == DEBOUNCE_THRESHOLD) {
        if (g_manual_flash_active) {
            // --- RECUPERACI�N SEGURA MEDIANTE REINICIO POR HARDWARE ---
            while(1) {
                // Esperar a que el WDT reinicie el sistema.
            }
        }
    }
}


/**
 * @brief Funci�n principal del programa.
 */
void main(void) {
    // --- 1. INICIALIZACI�N DE M�DULOS ---
    PIC_Init();
    EEPROM_Init();
    Sequence_Engine_RunStartupSequence();
    RTC_Init();
    Sequence_Engine_Init();
    Scheduler_Init();
    UART1_Init(9600);
    Timers_Init();

    // --- 2. VERIFICACI�N Y CONFIGURACI�N INICIAL ---
    if (EEPROM_Read(0x000) != 0xAA) {
        UART1_SendString("EEPROM no inicializada. Formateando...\r\n");
        EEPROM_InitStructure();
    }
    UART1_SendString("Controlador semaforico CORMAR inicializado\r\n");

    // --- 3. BARRERA DE SINCRONIZACI�N ---
    // Ahora que todo est� configurado, le decimos a la ISR que puede empezar a trabajar.
    g_system_ready = true;

    // --- 4. ESTADO INICIAL DEL MOTOR DE SECUENCIAS ---
    if (Sequence_Engine_GetRunningPlanID() == -1) {
        Sequence_Engine_EnterFallback();
    }

    // --- 5. BUCLE PRINCIPAL ---
    while(1) {
        CLRWDT(); // Limpiar el Watchdog Timer

        HandleManualFlashSwitch();

        if (!g_manual_flash_active) {
            UART_Task();
        }

        bool half_tick = g_half_second_flag;
        bool sec_tick = g_one_second_flag;

        if (half_tick) g_half_second_flag = false;
        if (sec_tick) g_one_second_flag = false;

        if (sec_tick && !g_manual_flash_active) {
            Scheduler_Task();
        }

        if (half_tick || sec_tick) {
            Sequence_Engine_Run(half_tick, sec_tick);
        }
    }
}
