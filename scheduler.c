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
static bool Scheduler_IsDateHoliday(RTC_Time* date);
static bool IsPlanValidForDay(uint8_t id_tipo_dia, uint8_t dayOfWeek, bool is_holiday);
static void Scheduler_LoadPlansToCache(void);
static void Scheduler_UpdateAndExecutePlan(void);

// --- NUEVA FUNCIÓN DE AYUDA ---
static void Scheduler_GetYesterdayContext(RTC_Time* today, uint8_t* yesterday_dow, bool* is_yesterday_holiday);
static bool IsLeapYear(uint8_t year_yy);

//==============================================================================
// --- FUNCIONES PÚBLICAS (Sin cambios en la interfaz) ---
//==============================================================================
void Scheduler_Init(void) {
    Scheduler_LoadPlansToCache();
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_ReloadCache(void) { // <-- ANTES: Scheduler_ForceReevaluation
    // Ahora, esta función solo recarga los planes a la memoria RAM.
    // NO fuerza una re-evaluación inmediata del plan activo.
    Scheduler_LoadPlansToCache();
}

void Scheduler_Task(void) {
    if (g_rtc_access_in_progress) return;
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

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
static void Scheduler_LoadPlansToCache(void) {
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        EEPROM_ReadPlan(i, &g_plan_cache[i].id_tipo_dia, &g_plan_cache[i].id_secuencia,
                        &g_plan_cache[i].time_sel, &g_plan_cache[i].hour, &g_plan_cache[i].minute);
    }
}

static bool IsLeapYear(uint8_t year_yy) {
    // Válido para el rango de años 2000-2099
    return (year_yy % 4 == 0);
}

static void Scheduler_GetYesterdayContext(RTC_Time* today, uint8_t* yesterday_dow, bool* is_yesterday_holiday) {
    RTC_Time yesterday = *today; // Copia la fecha de hoy para empezar

    // Calcular el día de la semana de ayer
    *yesterday_dow = (today->dayOfWeek == 1) ? 7 : today->dayOfWeek - 1;

    // Calcular la fecha de ayer
    if (yesterday.day > 1) {
        yesterday.day--;
    } else {
        // Es el primer día del mes, retrocedemos un mes
        if (yesterday.month == 1) { // Enero
            yesterday.month = 12;
            yesterday.day = 31;
            yesterday.year--; // Asumimos que no cruzamos el año 2000
        } else {
            yesterday.month--;
            // Array con los días de cada mes (índice 0 no se usa)
            const uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            yesterday.day = days_in_month[yesterday.month];
            
            // Caso especial para Febrero en año bisiesto
            if (yesterday.month == 2 && IsLeapYear(yesterday.year)) {
                yesterday.day = 29;
            }
        }
    }
    
    // Comprobar si la fecha calculada para ayer fue feriado
    *is_yesterday_holiday = Scheduler_IsDateHoliday(&yesterday);
}


static bool Scheduler_IsDateHoliday(RTC_Time* date) {
    for (uint8_t i = 0; i < MAX_HOLIDAYS; i++) {
        uint8_t f_day, f_month;
        EEPROM_ReadHoliday(i, &f_day, &f_month);
        if (f_day != 0xFF && f_day == date->day && f_month == date->month) {
            return true;
        }
    }
    return false;
}

static bool IsPlanValidForDay(uint8_t id_tipo_dia, uint8_t dayOfWeek, bool is_holiday) {
    if (is_holiday) {
        return (id_tipo_dia == 14);
    }
    if (id_tipo_dia == 14) {
        return false;
    }
    switch (id_tipo_dia) {
        case 0:  return (dayOfWeek == 7);
        case 1:  return (dayOfWeek == 1);
        case 2:  return (dayOfWeek == 2);
        case 3:  return (dayOfWeek == 3);
        case 4:  return (dayOfWeek == 4);
        case 5:  return (dayOfWeek == 5);
        case 6:  return (dayOfWeek == 6);
        case 7:  return true;
        case 8:  return (dayOfWeek != 7);
        case 9:  return (dayOfWeek == 6 || dayOfWeek == 7);
        case 10: return (dayOfWeek >= 1 && dayOfWeek <= 5);
        case 11: return (dayOfWeek == 5 || dayOfWeek == 6 || dayOfWeek == 7);
        case 12: return (dayOfWeek >= 1 && dayOfWeek <= 4);
        case 13: return (dayOfWeek == 5 || dayOfWeek == 6);
        default: return false;
    }
}

/**
 * @brief FUNCIÓN CENTRAL ACTUALIZADA.
 * @details Implementa la lógica completa, incluyendo la nueva agenda y
 * la lógica de fecha de continuidad 100% robusta.
 */
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // --- Paso 1: Obtener contexto ---
    bool is_today_holiday = Scheduler_IsDateHoliday(&now);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    uint8_t yesterday_dow;
    bool is_yesterday_holiday;
    Scheduler_GetYesterdayContext(&now, &yesterday_dow, &is_yesterday_holiday);
    
    // --- Paso 2: Búsqueda del mejor plan ---
    int8_t best_candidate_for_today = -1;
    uint16_t time_of_best_candidate_today = 0;

    int8_t best_candidate_for_yesterday = -1;
    uint16_t time_of_best_candidate_yesterday = 0;

    bool any_plan_exists = false;

    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia > 14) continue;
        any_plan_exists = true;

        uint16_t plan_time = p->hour * 60 + p->minute;

        // Búsqueda para HOY
        if (IsPlanValidForDay(p->id_tipo_dia, now.dayOfWeek, is_today_holiday)) {
            if (plan_time <= current_time_in_minutes) {
                if (best_candidate_for_today == -1 || plan_time >= time_of_best_candidate_today) {
                    time_of_best_candidate_today = plan_time;
                    best_candidate_for_today = (int8_t)i;
                }
            }
        }
        
        // Búsqueda para AYER (para continuidad)
        if (IsPlanValidForDay(p->id_tipo_dia, yesterday_dow, is_yesterday_holiday)) {
            if (best_candidate_for_yesterday == -1 || plan_time >= time_of_best_candidate_yesterday) {
                time_of_best_candidate_yesterday = plan_time;
                best_candidate_for_yesterday = (int8_t)i;
            }
        }
    }

    // --- Paso 3: Lógica de Decisión Final ---
    int8_t new_plan_index = -1;
    if (best_candidate_for_today != -1) {
        new_plan_index = best_candidate_for_today;
    } else {
        new_plan_index = best_candidate_for_yesterday;
    }

    // --- Paso 4: Ejecución Final ---
    if (new_plan_index != -1) {
        if (new_plan_index != g_active_plan_index) {
            g_active_plan_index = new_plan_index;
            Plan* active_plan = &g_plan_cache[g_active_plan_index];
            Sequence_Engine_Start(active_plan->id_secuencia, active_plan->time_sel);
        }
    } else if (any_plan_exists) {
        g_active_plan_index = -1;
        Sequence_Engine_Stop();
    } else {
        g_active_plan_index = -1;
        Sequence_Engine_EnterFallback();
    }
}
