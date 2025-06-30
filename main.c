//main.c
// Autor: Daniel Solis
// Final 1era etapa.
#include <xc.h>
#include "config.h"
#include "uart.h"
#include "eeprom.h"
#include "rtc.h"
#include "timers.h"
#include "scheduler.h"
#include "sequence_engine.h"

void main(void) {
    // --- ORDEN DE INICIALIZACIÓN SEGURO Y DEFINITIVO ---

    // 1. Inicialización de bajo nivel del PIC y la EEPROM.
    PIC_Init();
    EEPROM_Init();

    // 2. Ejecutar la secuencia de inicio bloqueante pero segura.
    // Esta función ahora es segura contra reinicios del WDT.
    Sequence_Engine_RunStartupSequence();

    // 3. Inicializar el resto de los módulos.
    RTC_Init();
    Sequence_Engine_Init(); // Pone el motor en estado FALLBACK por defecto
    Scheduler_Init();       // Carga el caché y puede que tome el control
    
    // 4. Iniciar las comunicaciones y las interrupciones al final.
    UART1_Init(9600);
    Timers_Init();
    
    if (EEPROM_Read(0x000) != 0xAA) {
        UART1_SendString("EEPROM no inicializada. Formateando...\r\n");
        EEPROM_InitStructure();
    }
    
    UART1_SendString("Controlador semaforico CORMAR inicializado\r\n");

    // 5. Verificación final: si después de todo, no hay un plan activo,
    // nos aseguramos de que esté en modo Fallback.
    if (Sequence_Engine_GetRunningPlanID() == -1) {
        Sequence_Engine_EnterFallback();
    }

    while(1) {
        CLRWDT();
        UART_Task();

        bool half_tick = g_half_second_flag;
        bool sec_tick = g_one_second_flag;

        if (half_tick) g_half_second_flag = false;
        if (sec_tick) g_one_second_flag = false;
        
        if (sec_tick) Scheduler_Task();
        
        if (half_tick || sec_tick) {
            Sequence_Engine_Run(half_tick, sec_tick);
        }
    }
}
