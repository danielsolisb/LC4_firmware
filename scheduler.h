// scheduler.h
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "eeprom.h" // Incluido para MAX_PLANS

// --- BANDERAS DE DEMANDA PEATONAL/VEHICULAR ---
// Banderas globales para registrar la activación de las entradas P1 a P4.
// 'extern' indica que están definidas en otro archivo (main.c).
extern volatile bool g_demand_flags[4];

extern volatile bool g_monitoring_active;
// --- FUNCIONES DE GESTIÓN DE DEMANDAS ---
/**
 * @brief Pone a cero todas las banderas de demanda.
 * @details Se llamará desde el motor de secuencias después de evaluar un Punto de Decisión.
 */
void Demands_ClearAll(void);


// --- LÓGICA DEL PLANIFICADOR (Scheduler) ---

// Bandera para controlar el acceso al RTC
extern volatile bool g_rtc_access_in_progress;

// Estructura para el caché de planes en RAM
typedef struct {
    uint8_t id_tipo_dia;
    uint8_t id_secuencia;
    uint8_t time_sel;
    uint8_t hour;
    uint8_t minute;
} Plan;

void Scheduler_Init(void);
void Scheduler_Task(void);

/**
 * @brief Fuerza al scheduler a recargar el caché de planes desde la EEPROM.
 */
void Scheduler_ReloadCache(void);

#endif // SCHEDULER_H
