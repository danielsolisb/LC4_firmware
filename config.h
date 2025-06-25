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

// =============================================================================
// --- NUEVA SECCIÓN: CONFIGURACIÓN DE LA SECUENCIA DE ARRANQUE ---
// =============================================================================
// 1 = Habilita una secuencia de flasheo al encender el equipo
// 0 = Deshabilita la secuencia
#define STARTUP_SEQUENCE_ENABLED 1

// Duración en segundos de la secuencia de arranque
#define STARTUP_SEQUENCE_DURATION_S 3

// Tiempo en milisegundos para cada estado del parpadeo (más bajo = más rápido)
// Un valor de 150ms resulta en un parpadeo rápido y notorio.
#define STARTUP_FLASH_DELAY_MS 150

// Máscaras de los puertos para el flasheo de arranque (luces amarillas)
#define STARTUP_MASK_D 0x49 // A1, A2
#define STARTUP_MASK_E 0x24 // A4, A5
#define STARTUP_MASK_F 0x92 // A7, A8
// =============================================================================


void PIC_Init(void);

#endif // CONFIG_H