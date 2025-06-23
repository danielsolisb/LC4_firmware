//main.c
#include <xc.h>
#include "config.h"
#include "uart.h"
#include "eeprom.h"
#include "rtc.h"
#include "timers.h"
#include "scheduler.h"
#include "sequence_engine.h"

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

        if (g_one_second_flag) {
            Scheduler_Task();
            g_one_second_flag = false;
        }

        if (g_half_second_flag) {
            Sequence_Engine_Run();
            g_half_second_flag = false;
        }
    }
}