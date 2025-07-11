// main.c
// Autor: Daniel Solis
// fecha 11/07/2025
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



// =============================================================================
// --- IMPLEMENTACIÓN DE FUNCIONES ---
// =============================================================================

void Demands_ClearAll(void) {
    for(uint8_t i = 0; i < 4; i++) {
        g_demand_flags[i] = false;
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