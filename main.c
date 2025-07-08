//main.c
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

// --- LÓGICA PARA EL SWITCH DE MANTENIMIENTO ---
#define MANUAL_FLASH_PIN PORTJbits.RJ5
#define DEBOUNCE_THRESHOLD 50

static bool g_manual_flash_active = false;

// Definición de las banderas globales para las entradas P1 a P4.
volatile bool g_demand_flags[4] = {false, false, false, false};

// Implementación de la función para limpiar las banderas.
void Demands_ClearAll(void) {
    for(uint8_t i = 0; i < 4; i++) {
        g_demand_flags[i] = false;
    }
}

static void HandleDemandInputs(void);
// Función para manejar la lógica del switch de mantenimiento
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
            // --- RECUPERACIÓN SEGURA MEDIANTE REINICIO POR HARDWARE ---
            // En lugar de intentar reiniciar los módulos por software,
            // forzamos un reinicio del microcontrolador entrando en un
            // bucle infinito. El Watchdog Timer (WDT), que está habilitado,
            // se desbordará después de ~4 segundos y reiniciará el PIC
            // de forma segura y completa.
            while(1) {
                // Esperar a que el WDT reinicie el sistema.
            }
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

    if (Sequence_Engine_GetRunningPlanID() == -1) {
        Sequence_Engine_EnterFallback();
    }

    while(1) {
        CLRWDT();
        
        HandleManualFlashSwitch();
        HandleDemandInputs();
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


static void HandleDemandInputs(void) {
    // Lógica simple de "latching": si se presiona una vez, la bandera queda en 'true'.
    // NOTA: Para una implementación final, se podría añadir un antirrebote (debounce) aquí si los botones son mecánicos.
    if (P1 == 1) g_demand_flags[0] = true;
    if (P2 == 1) g_demand_flags[1] = true;
    if (P3 == 1) g_demand_flags[2] = true;
    if (P4 == 1) g_demand_flags[3] = true;
}