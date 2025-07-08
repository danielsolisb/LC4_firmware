//config.h 

#ifndef CONFIG_H
#define CONFIG_H

#include <xc.h>
#include <stdint.h>

// --- INTERRUPTOR DE ENTORNO ---
// 1 = Modo Simulación (Proteus a 8MHz)
// 0 = Modo Hardware Real (Cristal de 20MHz)
#define SIMULATION_MODE 0

// --- Definición de Frecuencia Condicional ---
#if SIMULATION_MODE == 1
    #define _XTAL_FREQ 8000000UL
#else
    #define _XTAL_FREQ 20000000UL
#endif

#define P1 PORTJbits.RJ7
#define P2 PORTHbits.RH5
#define P3 PORTHbits.RH6
#define P4 PORTHbits.RH7

void PIC_Init(void);

#endif // CONFIG_H