// scheduler.h
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "eeprom.h" // Incluido para MAX_PLANS

// Bandera global para controlar el acceso al RTC
extern volatile bool g_rtc_access_in_progress;

// --- NUEVA ESTRUCTURA PARA EL CACHÉ DE PLANES EN RAM ---
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
 * @details Debe llamarse después de que la GUI guarda un nuevo plan,
 * una nueva agenda semanal o nuevos feriados.
 */
void Scheduler_ForceReevaluation(void);

#endif // SCHEDULER_H
