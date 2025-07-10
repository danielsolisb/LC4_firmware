// timers.c
#include "timers.h"
#include "config.h"
#include "uart.h"

// =============================================================================
// --- REFERENCIAS A FUNCIONES Y VARIABLES GLOBALES EXTERNAS ---
// =============================================================================

// Hacemos que la ISR conozca la función de sondeo de entradas de main.c
//extern void Inputs_ScanTask(void);
// Hacemos que la ISR conozca la bandera de sincronización de main.c
extern volatile bool g_system_ready;

// =============================================================================
// --- DEFINICIONES GLOBALES DEL MÓDULO ---
// =============================================================================

// Definición de las banderas globales para el control de tiempo
volatile bool g_one_second_flag = false;
volatile bool g_half_second_flag = false;

// Valor de precarga para el Timer1 para que interrumpa cada 1ms con un cristal de 20MHz
// (20MHz / 4) / 2 (prescaler) = 2,500,000 ticks/seg
// 65536 - 2500 = 63036
#define TIMER1_PRELOAD_VAL 63036
#define TMR1_PRELOAD_H ((TIMER1_PRELOAD_VAL >> 8) & 0xFF)
#define TMR1_PRELOAD_L (TIMER1_PRELOAD_VAL & 0xFF)

extern volatile bool g_system_ready;
extern volatile bool g_demand_flags[4];


// =============================================================================
// --- RUTINA DE SERVICIO DE INTERRUPCIÓN (ISR) ---
// =============================================================================
void __interrupt() ISR(void) {

    // --- Manejador de Interrupción del Timer1 ---
    if (PIE1bits.TMR1IE && PIR1bits.TMR1IF) {
        // Recargar el timer para la próxima interrupción de 1ms
        TMR1H = TMR1_PRELOAD_H;
        TMR1L = TMR1_PRELOAD_L;

        static uint16_t ms_counter = 0;
        ms_counter++;

        // Se sondea únicamente el pin P4 (RB3) cada 10ms.
        // La lógica de antirrebote se puede añadir aquí después.
        if (g_system_ready && (ms_counter % 10 == 0)) {
            if (P4 == 0) { // <--- La condición cambia de P4 == 1 a P4 == 0
                g_demand_flags[3] = true;
            }
        }

        // Generación de banderas de tiempo
        if (ms_counter % 500 == 0) g_half_second_flag = true;
        if (ms_counter >= 1000) {
            ms_counter = 0;
            g_one_second_flag = true;
        }

        PIR1bits.TMR1IF = 0; // Limpiar la bandera de interrupción del Timer1
    }
    
    if (INTCONbits.INT0IE && INTCONbits.INT0IF) {
        g_demand_flags[0] = true;
        INTCONbits.INT0IF = 0; // Limpiar bandera
    }

    // <<< NUEVO >>> --- Manejador para Interrupción Externa 1 (P2) ---
    if (INTCON3bits.INT1IE && INTCON3bits.INT1IF) {
        g_demand_flags[1] = true;
        INTCON3bits.INT1IF = 0; // Limpiar bandera
    }

    // <<< NUEVO >>> --- Manejador para Interrupción Externa 2 (P3) ---
    if (INTCON3bits.INT2IE && INTCON3bits.INT2IF) {
        g_demand_flags[2] = true;
        INTCON3bits.INT2IF = 0; // Limpiar bandera
    }

    
    // --- Manejador de Recepción UART1 (RX) ---
    if (PIE1bits.RC1IE && PIR1bits.RC1IF) {

        // Manejo de error de sobre-escritura (Overrun)
        if(RCSTA1bits.OERR)
        {
            RCSTA1bits.CREN = 0;
            RCSTA1bits.CREN = 1;
        }

        // Leer el dato y pasarlo a la tarea de procesamiento de UART
        uint8_t data = RCREG1;
        UART_ProcessReceivedByte(data);
    }

    // --- Manejador de Transmisión UART1 (TX) ---
    if (PIE1bits.TX1IE && PIR1bits.TX1IF) {
        UART_Transmit_ISR();
    }
}

// =============================================================================
// --- FUNCIÓN DE INICIALIZACIÓN ---
// =============================================================================
void Timers_Init(void) {
    // Configuración del Timer1
    T1CONbits.TMR1CS = 0b00; // Fuente de reloj interna (FOSC/4)
    T1CONbits.T1CKPS = 0b01; // Prescaler 1:2
    T1CONbits.RD16 = 1;      // Habilitar operación de 16 bits

    // Cargar valor inicial
    TMR1H = TMR1_PRELOAD_H;
    TMR1L = TMR1_PRELOAD_L;

    // Configuración de Interrupciones
    PIE1bits.TMR1IE = 1;  // Habilitar interrupción del Timer1
    IPR1bits.TMR1IP = 1;  // Asignar alta prioridad
    RCONbits.IPEN = 1;    // Habilitar sistema de prioridades de interrupción
    INTCONbits.GIEH = 1;  // Habilitar interrupciones de alta prioridad
    INTCONbits.GIEL = 1;  // Habilitar interrupciones de baja prioridad (buena práctica)

    // Configuración de Interrupciones Externas (Alta Prioridad)
    INTCON2bits.INTEDG0 = 0; // INT0 por flanco de subida
    INTCON2bits.INTEDG1 = 0; // INT1 por flanco de subida
    INTCON2bits.INTEDG2 = 0; // INT2 por flanco de subida

    INTCONbits.INT0IE = 1;   // Habilitar INT0
    INTCON3bits.INT1IE = 1;  // Habilitar INT1
    INTCON3bits.INT2IE = 1;  // Habilitar INT2
    
    INTCON3bits.INT1IP = 1;  // Asignar INT1 a alta prioridad
    INTCON3bits.INT2IP = 1;  // Asignar INT2 a alta prioridad
    
    // Iniciar el Timer1
    T1CONbits.TMR1ON = 1;
}
