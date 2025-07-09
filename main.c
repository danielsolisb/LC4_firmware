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

volatile bool g_system_ready = false;
void Inputs_ScanTask(void);
volatile bool g_demand_flags[4] = {false, false, false, false};

// --- LÓGICA PARA EL SWITCH DE MANTENIMIENTO ---
#define MANUAL_FLASH_PIN PORTJbits.RJ5
#define DEBOUNCE_THRESHOLD 50
static bool g_manual_flash_active = false;

// --- LÓGICA DE SONDEO DE ENTRADAS (MÁQUINA DE ESTADOS REFINADA) ---
// <<< AJUSTE SUTIL: Reducimos el tiempo de debounce para una sensación más instantánea >>>
#define DEBOUNCE_TICKS 2 // Necesitamos 2 ticks (20ms) de estado estable para confirmar.

// <<< LÓGICA MEJORADA: Más simple y enfocada en el "enganche" (latching) >>>
// Estados para nuestra máquina de antirrebote
typedef enum {
    STATE_RELEASED,  // El botón está liberado, esperando un flanco de subida.
    STATE_DEBOUNCING,// Se detectó un cambio, esperando a que se estabilice.
    STATE_PRESSED   // El botón está presionado, esperando ser liberado.
} InputState_t;

static InputState_t input_state[4] = {STATE_RELEASED, STATE_RELEASED, STATE_RELEASED, STATE_RELEASED};
static uint8_t debounce_counter[4] = {0, 0, 0, 0};


// =============================================================================
// --- IMPLEMENTACIÓN DE FUNCIONES ---
// =============================================================================

void Demands_ClearAll(void) {
    for(uint8_t i = 0; i < 4; i++) {
        g_demand_flags[i] = false;
    }
}

/**
 * @brief Escanea las entradas de demanda con una máquina de estados de enganche.
 * @details Detecta el flanco de subida (press), activa la bandera UNA SOLA VEZ,
 * y no permite reactivarla hasta que el botón sea liberado.
 */
void Inputs_ScanTask(void) {
    bool raw_inputs[4];
    raw_inputs[0] = (P1 == 1);
    raw_inputs[1] = (P2 == 1);
    raw_inputs[2] = (P3 == 1);
    raw_inputs[3] = (P4 == 1);

    for (uint8_t i = 0; i < 4; i++) {
        switch (input_state[i]) {
            case STATE_RELEASED:
                // Si estamos en reposo y se detecta una pulsación...
                if (raw_inputs[i] == true) {
                    input_state[i] = STATE_DEBOUNCING; // ...pasamos a la fase de antirrebote.
                    debounce_counter[i] = 0;
                }
                break;

            case STATE_DEBOUNCING:
                if (raw_inputs[i] == true) {
                    debounce_counter[i]++;
                    if (debounce_counter[i] >= DEBOUNCE_TICKS) {
                        // <<< ¡LÓGICA CLAVE! >>>
                        // El botón está confirmado como presionado.
                        // Activamos la bandera y pasamos al estado PRESSED.
                        g_demand_flags[i] = true;
                        input_state[i] = STATE_PRESSED;
                    }
                } else {
                    // Si fue ruido, volvemos al estado inicial sin hacer nada.
                    input_state[i] = STATE_RELEASED;
                }
                break;

            case STATE_PRESSED:
                // Mientras el botón esté presionado, nos quedamos aquí.
                // La bandera ya está en 'true' y no la volvemos a tocar.
                // Solo cuando se libera, volvemos al estado inicial para
                // poder detectar la *próxima* pulsación.
                if (raw_inputs[i] == false) {
                    input_state[i] = STATE_RELEASED;
                }
                break;
        }
    }
}

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
            while(1) { /* Esperar reinicio por WDT */ }
        }
    }
}

void main(void) {
    PIC_Init();
    EEPROM_Init();
    Sequence_Engine_RunStartupSequence();
    RTC_Init();
    Sequence_Engine_Init();
    Scheduler_Init();
    UART1_Init(9600);
    Timers_Init();

    if (EEPROM_Read(0x000) != 0xAA) {
        UART1_SendString("EEPROM no inicializada. Formateando...\r\n");
        EEPROM_InitStructure();
    }
    UART1_SendString("Controlador semaforico CORMAR inicializado\r\n");

    g_system_ready = true;

    if (Sequence_Engine_GetRunningPlanID() == -1) {
        Sequence_Engine_EnterFallback();
    }

    while(1) {
        CLRWDT();
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