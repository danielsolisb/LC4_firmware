//scheduler.c

/*
 * File:   scheduler.c
 * Author: Daniel Solis
 *
 * Descripción:
 * Este archivo contiene la lógica del planificador del controlador semafórico.
 * Se ha modificado para implementar un nuevo sistema de agenda flexible basado
 * en un 'id_tipo_dia' que define grupos de días, eliminando la necesidad
 * de la antigua agenda semanal. La lógica prioriza feriados y mantiene
 * la selección del plan más reciente y la continuidad con el día anterior.
 */

#include "scheduler.h"
#include "rtc.h"
#include "eeprom.h"
#include "timers.h"
#include "uart.h"
#include "sequence_engine.h"
#include <stdio.h>

// --- Variables Globales y Estáticas (Sin cambios) ---
volatile bool g_rtc_access_in_progress = false;
static Plan g_plan_cache[MAX_PLANS];
static int8_t g_active_plan_index = -1;


// --- Prototipos de Funciones Internas ---

// Función de ayuda para determinar si hoy es un día feriado.
static bool Scheduler_IsTodayHoliday(RTC_Time* now);

// NUEVA FUNCIÓN CENTRAL: Determina si un plan es válido para un día específico.
static bool IsPlanValidForDay(uint8_t id_tipo_dia, uint8_t dayOfWeek, bool is_holiday);

// Funciones principales del planificador (reestructuradas).
static void Scheduler_LoadPlansToCache(void);
static void Scheduler_UpdateAndExecutePlan(void);


//==============================================================================
// --- FUNCIONES PÚBLICAS (Sin cambios en la interfaz) ---
//==============================================================================

void Scheduler_Init(void) {
    Scheduler_LoadPlansToCache();
    // La primera ejecución determina el plan de arranque correcto.
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_ForceReevaluation(void) {
    Scheduler_LoadPlansToCache();
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_Task(void) {
    // No procesar si otro módulo está accediendo al RTC
    if (g_rtc_access_in_progress) return;

    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // Se evalúa un nuevo plan solo al inicio de cada minuto.
    if (now.second == 0) {
        Scheduler_UpdateAndExecutePlan();
    }
}

int8_t Scheduler_GetActivePlanID(void) {
    return g_active_plan_index;
}


//==============================================================================
// --- IMPLEMENTACIÓN DE LA LÓGICA DE PLANIFICACIÓN ---
//==============================================================================

/**
 * @brief Carga todos los planes desde la EEPROM a un caché en RAM.
 * @details Esto optimiza el rendimiento al evitar lecturas constantes a la EEPROM.
 */
static void Scheduler_LoadPlansToCache(void) {
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        EEPROM_ReadPlan(i, &g_plan_cache[i].id_tipo_dia, &g_plan_cache[i].id_secuencia,
                        &g_plan_cache[i].time_sel, &g_plan_cache[i].hour, &g_plan_cache[i].minute);
    }
}

/**
 * @brief Revisa la lista de feriados en la EEPROM para ver si la fecha coincide.
 * @param now Puntero a la estructura RTC_Time con la fecha y hora actuales.
 * @return true si la fecha actual es un feriado, false de lo contrario.
 */
static bool Scheduler_IsTodayHoliday(RTC_Time* now) {
    for (uint8_t i = 0; i < MAX_HOLIDAYS; i++) {
        uint8_t f_day, f_month;
        EEPROM_ReadHoliday(i, &f_day, &f_month);
        // Comprobar que no sea un slot vacío (0xFF) y que la fecha coincida
        if (f_day != 0xFF && f_day == now->day && f_month == now->month) {
            return true;
        }
    }
    return false;
}

/**
 * @brief NUEVA FUNCIÓN. El cerebro de la nueva agenda.
 * @details Evalúa si un 'id_tipo_dia' de un plan es compatible con el día actual.
 * @param id_tipo_dia El identificador del tipo de día del plan (0-14).
 * @param dayOfWeek El día de la semana actual (1=Lunes, 7=Domingo).
 * @param is_holiday true si el día actual es feriado.
 * @return true si el plan es válido para este día, false de lo contrario.
 */
static bool IsPlanValidForDay(uint8_t id_tipo_dia, uint8_t dayOfWeek, bool is_holiday) {
    // Prioridad 1: Feriados
    if (is_holiday) {
        return (id_tipo_dia == 14); // En feriado, solo son válidos los planes de tipo 14.
    }

    // Si no es feriado, los planes de tipo 14 nunca son válidos.
    if (id_tipo_dia == 14) {
        return false;
    }

    // Prioridad 2: Comprobación del día de la semana
    switch (id_tipo_dia) {
        case 0:  return (dayOfWeek == 7); // Domingo
        case 1:  return (dayOfWeek == 1); // Lunes
        case 2:  return (dayOfWeek == 2); // Martes
        case 3:  return (dayOfWeek == 3); // Miércoles
        case 4:  return (dayOfWeek == 4); // Jueves
        case 5:  return (dayOfWeek == 5); // Viernes
        case 6:  return (dayOfWeek == 6); // Sábado
        case 7:  return true; // Todos los días
        case 8:  return (dayOfWeek != 7); // Todos los días menos los domingos
        case 9:  return (dayOfWeek == 6 || dayOfWeek == 7); // Sábado y Domingo
        case 10: return (dayOfWeek >= 1 && dayOfWeek <= 5); // Todos excepto Sábado y Domingo
        case 11: return (dayOfWeek == 5 || dayOfWeek == 6 || dayOfWeek == 7); // Viernes, Sábado y Domingo
        case 12: return (dayOfWeek >= 1 && dayOfWeek <= 4); // Todos menos Viernes, Sábado y Domingo
        case 13: return (dayOfWeek == 5 || dayOfWeek == 6); // Viernes y Sábado
        default: return false; // ID no reconocido
    }
}

/**
 * @brief FUNCIÓN CENTRAL ACTUALIZADA.
 * @details Implementa la lógica completa para decidir qué plan debe estar activo,
 * incluyendo la nueva agenda, la selección del plan más reciente y la
 * continuidad con el día anterior.
 */
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // --- Paso 1: Obtener contexto del día actual ---
    bool is_today_holiday = Scheduler_IsTodayHoliday(&now);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    // --- Paso 2: Búsqueda jerárquica del mejor plan ---
    int8_t best_candidate_for_today = -1;
    uint16_t time_of_best_candidate_today = 0;

    int8_t best_candidate_for_yesterday = -1;
    uint16_t time_of_best_candidate_yesterday = 0;

    bool any_plan_exists = false;

    // Determinar contexto de AYER para la lógica de continuidad
    RTC_Time yesterday = now;
    // Manejo simple del día anterior (se puede mejorar para fin de mes/año)
    if (yesterday.day > 1) {
        yesterday.day--;
    } else {
        yesterday.day = 31; // Simplificación
    }
    // El día de la semana de ayer
    uint8_t yesterday_dow = (now.dayOfWeek == 1) ? 7 : now.dayOfWeek - 1;
    bool is_yesterday_holiday = Scheduler_IsTodayHoliday(&yesterday);

    // Iterar sobre todos los planes en el caché
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia > 14) continue; // Ignorar planes con ID inválido
        any_plan_exists = true;

        uint16_t plan_time = p->hour * 60 + p->minute;

        // --- Búsqueda para HOY ---
        if (IsPlanValidForDay(p->id_tipo_dia, now.dayOfWeek, is_today_holiday)) {
            // Si el plan es válido para hoy y su hora ya pasó...
            if (plan_time <= current_time_in_minutes) {
                // ...se convierte en candidato. Nos quedamos con el más reciente.
                if (best_candidate_for_today == -1 || plan_time >= time_of_best_candidate_today) {
                    time_of_best_candidate_today = plan_time;
                    best_candidate_for_today = (int8_t)i;
                }
            }
        }
        
        // --- Búsqueda para AYER (para continuidad) ---
        if (IsPlanValidForDay(p->id_tipo_dia, yesterday_dow, is_yesterday_holiday)) {
            // Buscamos el plan más tardío de ayer.
            if (best_candidate_for_yesterday == -1 || plan_time >= time_of_best_candidate_yesterday) {
                time_of_best_candidate_yesterday = plan_time;
                best_candidate_for_yesterday = (int8_t)i;
            }
        }
    }

    // --- Paso 3: Lógica de Decisión Final ---
    int8_t new_plan_index = -1;
    if (best_candidate_for_today != -1) {
        // Si hay un plan válido para hoy, ese tiene la máxima prioridad.
        new_plan_index = best_candidate_for_today;
    } else {
        // Si no hay plan para hoy, usamos el último plan de ayer para dar continuidad.
        new_plan_index = best_candidate_for_yesterday;
    }

    // --- Paso 4: Ejecución Final ---
    if (new_plan_index != -1) {
        // Si el plan seleccionado es diferente al que está corriendo, lo iniciamos.
        if (new_plan_index != g_active_plan_index) {
            g_active_plan_index = new_plan_index;
            Plan* active_plan = &g_plan_cache[g_active_plan_index];
            Sequence_Engine_Start(active_plan->id_secuencia, active_plan->time_sel);
        }
    } else if (any_plan_exists) {
        // Hay planes en la memoria, pero ninguno aplica hoy. Detener el semáforo.
        g_active_plan_index = -1;
        Sequence_Engine_Stop();
    } else {
        // No hay ningún plan configurado en la EEPROM. Entrar en modo de fallo.
        g_active_plan_index = -1;
        Sequence_Engine_EnterFallback();
    }
}
