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
    
    // --- Paso 2: Búsqueda jerárquica en una sola pasada ---
    int8_t p1_specific_idx = -1, p2_no_holiday_idx = -1, p3_all_days_idx = -1;
    uint16_t p1_time = 0, p2_time = 0, p3_time = 0;
    
    // Nivel 4: Para la lógica de continuidad, necesitamos encontrar el último plan de ayer.
    // Lo hacemos aquí mismo para optimizar.
    int8_t p4_yesterday_idx = -1;
    uint16_t p4_time = 0;

    bool any_plan_exists = false;

    // Determinar el tipo de día de ayer
    bool is_yesterday_holiday; // No la usamos por ahora, pero podría ser útil
    RTC_Time yesterday = now; // Simplificación: no manejamos cambio de mes/año para feriados de ayer
    if (yesterday.day > 1) yesterday.day--; else yesterday.day = 31; // Simplificación
    Scheduler_GetDayType(&yesterday, &is_yesterday_holiday);
    
    uint8_t agenda[7];
    EEPROM_ReadWeeklyAgenda(agenda);
    uint8_t yesterday_dow = (now.dayOfWeek == 1) ? 7 : now.dayOfWeek - 1;
    uint8_t id_tipo_ayer = agenda[yesterday_dow - 1];
    // TODO: La lógica para determinar si ayer fue feriado puede mejorarse

    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia == 0xFF) continue;
        any_plan_exists = true;

        uint16_t plan_time = p->hour * 60 + p->minute;
        
        // Búsqueda para planes de HOY (Niveles 1, 2, 3)
        if (plan_time <= current_time_in_minutes) {
            if (p->id_tipo_dia == id_tipo_hoy) { // Nivel 1
                if (p1_specific_idx == -1 || plan_time >= p1_time) {
                    p1_time = plan_time; p1_specific_idx = (int8_t)i;
                }
            } else if (p->id_tipo_dia == TIPO_DIA_TODOS_NO_FERIADO && !is_today_holiday) { // Nivel 2
                if (p2_no_holiday_idx == -1 || plan_time >= p2_time) {
                    p2_time = plan_time; p2_no_holiday_idx = (int8_t)i;
                }
            } else if (p->id_tipo_dia == TIPO_DIA_TODOS) { // Nivel 3
                if (p3_all_days_idx == -1 || plan_time >= p3_time) {
                    p3_time = plan_time; p3_all_days_idx = (int8_t)i;
                }
            }
        }
        
        // Búsqueda para plan de AYER (Nivel 4)
        if (p->id_tipo_dia == id_tipo_ayer) {
            if (p4_yesterday_idx == -1 || plan_time >= p4_time) {
                p4_time = plan_time; p4_yesterday_idx = (int8_t)i;
            }
        }
    }

    // --- Paso 3: Lógica de Decisión Final ---
    int8_t new_plan_index = -1;
    if (p1_specific_idx != -1) {
        new_plan_index = p1_specific_idx;
    } else if (p2_no_holiday_idx != -1) {
        new_plan_index = p2_no_holiday_idx;
    } else if (p3_all_days_idx != -1) {
        new_plan_index = p3_all_days_idx;
    } else {
        // Nivel 4: Lógica de continuidad (último plan de ayer)
        // Si ninguna de las reglas anteriores aplica, usamos el mejor candidato de ayer.
        new_plan_index = p4_yesterday_idx;
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
