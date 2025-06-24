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
    
    // =========================================================================
    // >>> CORRECCIÓN CRÍTICA DE ORDEN <<<
    // 1. Se inicializa el motor de secuencias (lo preparamos).
    Sequence_Engine_Init();
    
    // 2. Se inicializa el planificador, que ahora puede usar el motor de forma segura.
    Scheduler_Init();
    // =========================================================================
    
    UART1_SendString("Controlador semaforico CORMAR inicializado\r\n");

    // El bucle principal no cambia
    while(1) {
        CLRWDT();
        UART_Task();

        // --- NUEVA LÓGICA DE MANEJO DE TIEMPO CENTRALIZADO ---
        
        // 1. Leer las banderas una sola vez al inicio del ciclo.
        bool half_tick = g_half_second_flag;
        bool sec_tick = g_one_second_flag;

        // 2. Consumir las banderas inmediatamente para no perder ticks.
        if (half_tick) {
            g_half_second_flag = false;
        }
        if (sec_tick) {
            g_one_second_flag = false;
        }
        
        // 3. Ejecutar las tareas pasando las banderas como parámetros.
        
        // El Scheduler solo necesita el tick de un segundo.
        if (sec_tick) {
            Scheduler_Task();
        }

        // El Motor de Secuencias necesita ambos ticks para funcionar correctamente.
        // Se llama si ha ocurrido cualquiera de los dos ticks.
        if (half_tick || sec_tick) {
            Sequence_Engine_Run(half_tick, sec_tick);
        }
    }
}