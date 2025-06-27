//main.c
// Autor: Daniel Solis
// fecha 23/06/2025 nuevo commit
#include <xc.h>
#include "config.h"
#include "uart.h"
#include "eeprom.h"
#include "rtc.h"
#include "timers.h"
#include "scheduler.h"
#include "sequence_engine.h"

// principal code
void main(void) {
    PIC_Init();

    #if STARTUP_SEQUENCE_ENABLED == 1
        Sequence_Engine_RunStartupSequence();
    #endif

    // Inicialización de módulos base
    RTC_Init();
    EEPROM_Init();
    Timers_Init();
    UART1_Init(9600);
    
    if (EEPROM_Read(0x000) != 0xAA) {
        UART1_SendString("EEPROM no inicializada. Formateando...\r\n");
        EEPROM_InitStructure();
    }
    
    // Inicialización de los módulos de lógica principal
    Sequence_Engine_Init();
    Scheduler_Init();
    
    UART1_SendString("Controlador semaforico CORMAR inicializado\r\n");

    // =========================================================================
    // --- NUEVA VERIFICACIÓN DE ARRANQUE SEGURO ---
    // Esta es la "red de seguridad". Si después de inicializar todo,
    // el motor no tiene un plan activo, significa que el Scheduler no encontró
    // nada que ejecutar. En este caso, forzamos el modo Fallback para
    // asegurar que el controlador nunca se quede en silencio.
    if (Sequence_Engine_GetRunningPlanID() == -1) {
        Sequence_Engine_EnterFallback();
    }
    // =========================================================================

    // El bucle principal no cambia
    while(1) {
        CLRWDT();
        UART_Task();

        // Lógica de manejo de tiempo centralizado
        bool half_tick = g_half_second_flag;
        bool sec_tick = g_one_second_flag;

        if (half_tick) {
            g_half_second_flag = false;
        }
        if (sec_tick) {
            g_one_second_flag = false;
        }
        
        // El Scheduler solo necesita el tick de un segundo.
        if (sec_tick) {
            Scheduler_Task();
        }

        // El Motor de Secuencias necesita ambos ticks para funcionar.
        if (half_tick || sec_tick) {
            Sequence_Engine_Run(half_tick, sec_tick);
        }
    }
}