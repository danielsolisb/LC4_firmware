//scheduler.c
#include "scheduler.h"
#include "rtc.h"
#include "eeprom.h"
#include "timers.h"
#include "uart.h"
#include "sequence_engine.h"
#include <stdio.h>

volatile bool g_rtc_access_in_progress = false;

// Almacenamiento en caché de los planes de la EEPROM
static Plan g_plan_cache[MAX_PLANS];
// Índice del plan que está actualmente en ejecución, -1 si ninguno.
static int8_t g_active_plan_index = -1;

// --- Prototipos de Funciones Internas ---
static void Scheduler_LoadPlansToCache(void);
static void Scheduler_UpdateAndExecutePlan(void); // NUEVA función central
static uint8_t Scheduler_GetDayType(RTC_Time* now);

//==============================================================================
// --- FUNCIONES PÚBLICAS ---
//==============================================================================

void Scheduler_Init(void) {
    Scheduler_LoadPlansToCache();
    // Al iniciar, se llama a la lógica central para determinar el estado inicial correcto.
    Scheduler_UpdateAndExecutePlan();
}

void Scheduler_ForceReevaluation(void) {
    Scheduler_LoadPlansToCache();
    // Al forzar una re-evaluación (desde la GUI), se usa la misma lógica central.
    Scheduler_UpdateAndExecutePlan();
}

/**
 * Tarea principal del scheduler, ejecutada cada segundo.
 * Su responsabilidad se ha reducido drásticamente.
 */
void Scheduler_Task(void) {
    if (g_rtc_access_in_progress) {
        return;
    }

    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // Para máxima robustez, re-evaluamos la lógica completa cada minuto
    // (cuando los segundos son 0). Esto asegura que el sistema siempre
    // converge al estado correcto, incluso si se pierde un evento.
    if (now.second == 0) {
        Scheduler_UpdateAndExecutePlan();
    }
}

//==============================================================================
// --- FUNCIONES PRIVADAS ---
//==============================================================================

static void Scheduler_LoadPlansToCache(void) {
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        EEPROM_ReadPlan(i,
                        &g_plan_cache[i].id_tipo_dia,
                        &g_plan_cache[i].id_secuencia,
                        &g_plan_cache[i].time_sel,
                        &g_plan_cache[i].hour,
                        &g_plan_cache[i].minute);
    }
}

static uint8_t Scheduler_GetDayType(RTC_Time* now) {
    bool es_feriado = false;
    for (uint8_t i = 0; i < MAX_HOLIDAYS; i++) {
        uint8_t f_day, f_month;
        EEPROM_ReadHoliday(i, &f_day, &f_month);
        if (f_day != 0xFF && f_day == now->day && f_month == now->month) {
            es_feriado = true;
            break;
        }
    }

    if (es_feriado) {
        return TIPO_DIA_FERIADO;
    } else {
        uint8_t agenda[7];
        EEPROM_ReadWeeklyAgenda(agenda);
        if (now->dayOfWeek >= 1 && now->dayOfWeek <= 7) {
            return agenda[now->dayOfWeek - 1];
        } else {
            return TIPO_DIA_LABORAL; // Default de seguridad
        }
    }
}


/**
 * @brief FUNCIÓN CENTRAL UNIFICADA (VERSIÓN FINAL). Contiene toda la lógica para decidir qué
 * plan debe estar activo. Se llama al inicio y al re-evaluar.
 */
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    // --- Paso 1: Determinar contexto ---
    uint8_t id_tipo_hoy = Scheduler_GetDayType(&now);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    // --- Paso 2: Búsqueda unificada y más inteligente ---
    int8_t best_plan_today_idx = -1;
    uint16_t best_plan_today_time = 0;
    
    // Encontraremos el último plan para CADA tipo de día, para tomar la mejor decisión
    int8_t last_plan_idx_laboral = -1;
    int8_t last_plan_idx_sabado = -1;
    int8_t last_plan_idx_domingo = -1;
    int8_t last_plan_idx_feriado = -1;

    bool any_plan_exists = false;

    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia == 0xFF) continue;
        any_plan_exists = true;

        // Búsqueda del mejor plan para HOY
        if (p->id_tipo_dia == id_tipo_hoy) {
            uint16_t plan_time = p->hour * 60 + p->minute;
            if (plan_time <= current_time_in_minutes) {
                if (best_plan_today_idx == -1 || plan_time >= best_plan_today_time) {
                    best_plan_today_time = plan_time;
                    best_plan_today_idx = (int8_t)i;
                }
            }
        }
        
        // Buscamos el último plan de cada tipo para la lógica de "ayer"
        switch(p->id_tipo_dia) {
            // CORRECCIÓN: Añadido un cast (int8_t) para eliminar las advertencias.
            case TIPO_DIA_LABORAL: last_plan_idx_laboral = (int8_t)i; break;
            case TIPO_DIA_SABADO:  last_plan_idx_sabado  = (int8_t)i; break;
            case TIPO_DIA_DOMINGO: last_plan_idx_domingo = (int8_t)i; break;
            case TIPO_DIA_FERIADO: last_plan_idx_feriado = (int8_t)i; break;
        }
    }

    // --- Paso 3: Lógica de Decisión Final Mejorada ---
    int8_t new_plan_index = -1;

    if (best_plan_today_idx != -1) {
        // Prioridad 1: Si hay un plan para hoy, ese es.
        new_plan_index = best_plan_today_idx;
    } else {
        // Si no, determinamos cuál fue el tipo de día de ayer y usamos su último plan.
        uint8_t agenda[7];
        EEPROM_ReadWeeklyAgenda(agenda);
        uint8_t yesterday_dow = (now.dayOfWeek == 1) ? 7 : now.dayOfWeek - 1;
        uint8_t id_tipo_ayer = agenda[yesterday_dow - 1];
        // TODO: La lógica para determinar si ayer fue feriado puede mejorarse
        
        switch(id_tipo_ayer) {
            case TIPO_DIA_LABORAL: new_plan_index = last_plan_idx_laboral; break;
            case TIPO_DIA_SABADO:  new_plan_index = last_plan_idx_sabado;  break;
            case TIPO_DIA_DOMINGO: new_plan_index = last_plan_idx_domingo; break;
            case TIPO_DIA_FERIADO: new_plan_index = last_plan_idx_feriado; break;
        }
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
        Sequence_Engine_Stop(); // Espera inactiva
    } else {
        g_active_plan_index = -1;
        Sequence_Engine_EnterFallback(); // Error, no hay configuración
    }
}
