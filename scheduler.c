//scheduler.c
#include "scheduler.h"
#include "rtc.h"
#include "eeprom.h"
#include "timers.h"
#include "uart.h"
#include "sequence_engine.h"
#include <stdio.h>

// --- Variables Globales y Estáticas ---
volatile bool g_rtc_access_in_progress = false;
static Plan g_plan_cache[MAX_PLANS];
// Esta variable ahora representa el plan que el scheduler *ha solicitado*.
// No es necesariamente el que está corriendo en el motor.
static int8_t g_requested_plan_index = -1;

// --- Prototipos de Funciones Internas ---
static bool Scheduler_IsDateHoliday(RTC_Time* date);
static bool IsPlanValidForDay(uint8_t id_tipo_dia, uint8_t dayOfWeek, bool is_holiday);
static void Scheduler_LoadPlansToCache(void);
static void Scheduler_UpdateAndExecutePlan(void);
static void Scheduler_GetYesterdayContext(RTC_Time* today, uint8_t* yesterday_dow, bool* is_yesterday_holiday);
static bool IsLeapYear(uint8_t year_yy);

//==============================================================================
// --- FUNCIONES PÚBLICAS ---
//==============================================================================
void Scheduler_Init(void) {
    Scheduler_LoadPlansToCache();
    // La primera evaluación de plan se hace aquí para arrancar con el plan correcto
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_ReloadCache(void) {
    Scheduler_LoadPlansToCache();
}

void Scheduler_Task(void) {
    if (g_rtc_access_in_progress) return;
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // La tarea se sigue ejecutando cada minuto en el segundo 0
    if (now.second == 0) {
        Scheduler_UpdateAndExecutePlan();
    }
}

// ELIMINADA: La función Scheduler_GetActivePlanID() ya no existe aquí.

//==============================================================================
// --- IMPLEMENTACIÓN DE LA LÓGICA DE PLANIFICACIÓN ---
//==============================================================================
static void Scheduler_LoadPlansToCache(void) {
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        EEPROM_ReadPlan(i, &g_plan_cache[i].id_tipo_dia, &g_plan_cache[i].id_secuencia,
                        &g_plan_cache[i].time_sel, &g_plan_cache[i].hour, &g_plan_cache[i].minute);
    }
}

// Lógica de cálculo de fecha no cambia
static bool IsLeapYear(uint8_t year_yy) {
    return (year_yy % 4 == 0);
}

static void Scheduler_GetYesterdayContext(RTC_Time* today, uint8_t* yesterday_dow, bool* is_yesterday_holiday) {
    RTC_Time yesterday = *today;
    *yesterday_dow = (today->dayOfWeek == 1) ? 7 : today->dayOfWeek - 1;
    if (yesterday.day > 1) {
        yesterday.day--;
    } else {
        if (yesterday.month == 1) {
            yesterday.month = 12;
            yesterday.day = 31;
            yesterday.year--;
        } else {
            yesterday.month--;
            const uint8_t days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            yesterday.day = days_in_month[yesterday.month];
            if (yesterday.month == 2 && IsLeapYear(yesterday.year)) {
                yesterday.day = 29;
            }
        }
    }
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

// --- FUNCIÓN CENTRAL ACTUALIZADA ---
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    bool is_today_holiday = Scheduler_IsDateHoliday(&now);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    uint8_t yesterday_dow;
    bool is_yesterday_holiday;
    Scheduler_GetYesterdayContext(&now, &yesterday_dow, &is_yesterday_holiday);
    
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

        if (IsPlanValidForDay(p->id_tipo_dia, now.dayOfWeek, is_today_holiday)) {
            if (plan_time <= current_time_in_minutes) {
                if (best_candidate_for_today == -1 || plan_time >= time_of_best_candidate_today) {
                    time_of_best_candidate_today = plan_time;
                    best_candidate_for_today = (int8_t)i;
                }
            }
        }
        
        if (IsPlanValidForDay(p->id_tipo_dia, yesterday_dow, is_yesterday_holiday)) {
            if (best_candidate_for_yesterday == -1 || plan_time >= time_of_best_candidate_yesterday) {
                time_of_best_candidate_yesterday = plan_time;
                best_candidate_for_yesterday = (int8_t)i;
            }
        }
    }

    int8_t new_plan_index = -1;
    if (best_candidate_for_today != -1) {
        new_plan_index = best_candidate_for_today;
    } else {
        new_plan_index = best_candidate_for_yesterday;
    }

    // --- LÓGICA DE EJECUCIÓN MODIFICADA ---
    if (new_plan_index != -1) {
        // ¿El plan que DEBERÍA estar activo es diferente al que solicitamos la última vez?
        if (new_plan_index != g_requested_plan_index) {
            g_requested_plan_index = new_plan_index;
            Plan* active_plan = &g_plan_cache[g_requested_plan_index];
            
            // Si el motor está inactivo, lo iniciamos directamente.
            // Si ya está corriendo, solicitamos un cambio controlado.
            if (Sequence_Engine_GetRunningPlanID() == -1) {
                Sequence_Engine_Start(active_plan->id_secuencia, active_plan->time_sel, g_requested_plan_index);
            } else {
                Sequence_Engine_RequestPlanChange(active_plan->id_secuencia, active_plan->time_sel, g_requested_plan_index);
            }
        }
    } else if (any_plan_exists) {
        // Hay planes pero ninguno aplica. Detener el motor.
        if (g_requested_plan_index != -1) {
             g_requested_plan_index = -1;
             Sequence_Engine_Stop();
        }
    } else {
        // No hay ningún plan en la EEPROM. Entrar en modo Fallback.
        if (g_requested_plan_index != -1) {
            g_requested_plan_index = -1;
            Sequence_Engine_EnterFallback();
        }
    }
}
