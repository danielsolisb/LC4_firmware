//scheduler.c
#include "scheduler.h"
#include "rtc.h"
#include "eeprom.h"
#include "timers.h"
#include "uart.h"
#include "sequence_engine.h"
#include <stdio.h>

volatile bool g_rtc_access_in_progress = false;

static Plan g_plan_cache[MAX_PLANS];
static int8_t g_active_plan_index = -1;

// --- Prototipos de Funciones Internas ---
static void Scheduler_LoadPlansToCache(void);
static void Scheduler_UpdateAndExecutePlan(void);
static uint8_t Scheduler_GetDayType(RTC_Time* now, bool* is_holiday);

//==============================================================================
// --- FUNCIONES PÚBLICAS (sin cambios) ---
//==============================================================================
void Scheduler_Init(void) {
    Scheduler_LoadPlansToCache();
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_ForceReevaluation(void) {
    Scheduler_LoadPlansToCache();
    Scheduler_UpdateAndExecutePlan();
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
// --- FUNCIONES PRIVADAS (Lógica de Planificación Mejorada) ---
//==============================================================================
static void Scheduler_LoadPlansToCache(void) {
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        EEPROM_ReadPlan(i, &g_plan_cache[i].id_tipo_dia, &g_plan_cache[i].id_secuencia,
                        &g_plan_cache[i].time_sel, &g_plan_cache[i].hour, &g_plan_cache[i].minute);
    }
}

static uint8_t Scheduler_GetDayType(RTC_Time* now, bool* is_holiday) {
    *is_holiday = false;
    for (uint8_t i = 0; i < MAX_HOLIDAYS; i++) {
        uint8_t f_day, f_month;
        EEPROM_ReadHoliday(i, &f_day, &f_month);
        if (f_day != 0xFF && f_day == now->day && f_month == now->month) {
            *is_holiday = true;
            return TIPO_DIA_FERIADO;
        }
    }
    uint8_t agenda[7];
    EEPROM_ReadWeeklyAgenda(agenda);
    if (now->dayOfWeek >= 1 && now->dayOfWeek <= 7) {
        return agenda[now->dayOfWeek - 1];
    }
    return TIPO_DIA_LABORAL;
}

/**
 * @brief FUNCIÓN CENTRAL (VERSIÓN FINAL Y COMPLETA). Implementa la lógica jerárquica
 * para decidir qué plan debe estar activo.
 */
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // --- Paso 1: Determinar contexto ---
    bool is_today_holiday;
    uint8_t id_tipo_hoy = Scheduler_GetDayType(&now, &is_today_holiday);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    // --- Paso 2: Búsqueda jerárquica para planes de HOY ---
    int8_t p_today_specific_idx = -1, p_today_no_holiday_idx = -1, p_today_all_days_idx = -1;
    uint16_t t_today_specific = 0, t_today_no_holiday = 0, t_today_all_days = 0;

    // --- NUEVA LÓGICA MEJORADA para la búsqueda de AYER ---
    int8_t p_yesterday_specific_idx = -1, p_yesterday_no_holiday_idx = -1, p_yesterday_all_days_idx = -1;
    uint16_t t_yesterday_specific = 0, t_yesterday_no_holiday = 0, t_yesterday_all_days = 0;

    bool any_plan_exists = false;

    // Determinar contexto de AYER
    RTC_Time yesterday_struct = now;
    // Lógica simple para manejar el día anterior (se puede mejorar para fin de mes/año)
    if(yesterday_struct.day > 1) yesterday_struct.day--; else yesterday_struct.day = 31;
    
    bool is_yesterday_holiday;
    uint8_t id_tipo_ayer = Scheduler_GetDayType(&yesterday_struct, &is_yesterday_holiday);

    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia == 0xFF) continue;
        any_plan_exists = true;

        uint16_t plan_time = p->hour * 60 + p->minute;
        
        // Búsqueda para planes de HOY (que ya ocurrieron)
        if (plan_time <= current_time_in_minutes) {
            if (p->id_tipo_dia == id_tipo_hoy) { // Nivel 1 (Hoy, Específico)
                if (p_today_specific_idx == -1 || plan_time >= t_today_specific) {
                    t_today_specific = plan_time; p_today_specific_idx = (int8_t)i;
                }
            } else if (p->id_tipo_dia == TIPO_DIA_TODOS_NO_FERIADO && !is_today_holiday) { // Nivel 2 (Hoy, No Feriado)
                if (p_today_no_holiday_idx == -1 || plan_time >= t_today_no_holiday) {
                    t_today_no_holiday = plan_time; p_today_no_holiday_idx = (int8_t)i;
                }
            } else if (p->id_tipo_dia == TIPO_DIA_TODOS) { // Nivel 3 (Hoy, Todos)
                if (p_today_all_days_idx == -1 || plan_time >= t_today_all_days) {
                    t_today_all_days = plan_time; p_today_all_days_idx = (int8_t)i;
                }
            }
        }
        
        // Búsqueda para el plan más tardío de AYER (para continuidad)
        if (p->id_tipo_dia == id_tipo_ayer) { // Nivel 4.1 (Ayer, Específico)
            if (p_yesterday_specific_idx == -1 || plan_time >= t_yesterday_specific) {
                t_yesterday_specific = plan_time; p_yesterday_specific_idx = i;
            }
        } else if (p->id_tipo_dia == TIPO_DIA_TODOS_NO_FERIADO && !is_yesterday_holiday) { // Nivel 4.2 (Ayer, No Feriado)
            if (p_yesterday_no_holiday_idx == -1 || plan_time >= t_yesterday_no_holiday) {
                t_yesterday_no_holiday = plan_time; p_yesterday_no_holiday_idx = i;
            }
        } else if (p->id_tipo_dia == TIPO_DIA_TODOS) { // Nivel 4.3 (Ayer, Todos)
             if (p_yesterday_all_days_idx == -1 || plan_time >= t_yesterday_all_days) {
                t_yesterday_all_days = plan_time; p_yesterday_all_days_idx = i;
            }
        }
    }

    // --- Paso 3: Lógica de Decisión Final ---
    int8_t new_plan_index = -1;

    // Prioridad para HOY
    if (p_today_specific_idx != -1) new_plan_index = p_today_specific_idx;
    else if (p_today_no_holiday_idx != -1) new_plan_index = p_today_no_holiday_idx;
    else if (p_today_all_days_idx != -1) new_plan_index = p_today_all_days_idx;
    else {
        // Si no hay plan para hoy, buscamos el mejor de AYER para continuidad
        if (p_yesterday_specific_idx != -1) new_plan_index = p_yesterday_specific_idx;
        else if (p_yesterday_no_holiday_idx != -1) new_plan_index = p_yesterday_no_holiday_idx;
        else if (p_yesterday_all_days_idx != -1) new_plan_index = p_yesterday_all_days_idx;
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
        Sequence_Engine_Stop(); // Hay planes, pero ninguno aplica
    } else {
        g_active_plan_index = -1;
        Sequence_Engine_EnterFallback(); // No hay ningún plan configurado
    }
}