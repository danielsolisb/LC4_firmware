//timers.c
#include "timers.h"
#include "config.h"
#include "uart.h"

// Definición de las banderas globales
volatile bool g_one_second_flag = false;
volatile bool g_half_second_flag = false;

#define TIMER1_PRELOAD_VAL 63036 
#define TMR1_PRELOAD_H ((TIMER1_PRELOAD_VAL >> 8) & 0xFF)
#define TMR1_PRELOAD_L (TIMER1_PRELOAD_VAL & 0xFF)

void __interrupt() ISR(void) {
    
    // Manejador de Interrupción del Timer1
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF) {
        TMR1H = TMR1_PRELOAD_H;
        TMR1L = TMR1_PRELOAD_L;
        
        static uint16_t ms_counter = 0;
        ms_counter++;

        // --- LÓGICA DE BANDERAS MODIFICADA ---
        // A los 500ms, activa la bandera de medio segundo
        if (ms_counter % 500 == 0) {
            g_half_second_flag = true;
        }
        
        // A los 1000ms, activa la bandera de un segundo y resetea el contador
        if (ms_counter >= 1000) {
            ms_counter = 0;
            g_one_second_flag = true; 
        }

        PIR1bits.TMR1IF = 0; 
    }
    
    // --- Manejador de Recepción UART1 (RX) ---
    if (PIE1bits.RC1IE && PIR1bits.RC1IF) {
        
        // >>> CORRECCIÓN DE ROBUSTEZ: MANEJO DE OVERRUN ERROR (OERR) <<<
        // Si el bit OERR está activado, significa que se perdió un byte,
        // usualmente por ruido o alta carga del CPU.
        if(RCSTA1bits.OERR)
        {
            // Para limpiar el error, el datasheet indica que se debe resetear
            // el receptor de la UART deshabilitando y habilitando el bit CREN.
            RCSTA1bits.CREN = 0; 
            RCSTA1bits.CREN = 1; 
        }
        
        // Solo después de verificar el error, leemos el dato de forma segura.
        uint8_t data = RCREG1;
        
        // Pasamos el byte recibido a la función de procesamiento en uart.c
        UART_ProcessReceivedByte(data);
    }

    // Manejador de Transmisión UART1 (TX)
    if (PIE1bits.TX1IE && PIR1bits.TX1IF) {
        UART_Transmit_ISR();
    }
}

void Timers_Init(void) {
    T1CONbits.TMR1CS = 0b00;
    T1CONbits.T1CKPS = 0b01;
    T1CONbits.RD16 = 1;     
    TMR1H = TMR1_PRELOAD_H;
    TMR1L = TMR1_PRELOAD_L;
    PIE1bits.TMR1IE = 1;
    IPR1bits.TMR1IP = 1;  
    RCONbits.IPEN = 1;    
    INTCONbits.GIEH = 1;  
    INTCONbits.GIEL = 1;  
    T1CONbits.TMR1ON = 1;
}
