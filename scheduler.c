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
 * @brief FUNCIÓN CENTRAL UNIFICADA. Contiene toda la lógica para decidir qué
 * plan debe estar activo. Se llama al inicio y al re-evaluar.
 */
static void Scheduler_UpdateAndExecutePlan(void) {
    RTC_Time now;
    g_rtc_access_in_progress = true;
    RTC_GetTime(&now);
    g_rtc_access_in_progress = false;

    uint8_t id_tipo_hoy = Scheduler_GetDayType(&now);
    uint16_t current_time_in_minutes = now.hour * 60 + now.minute;
    
    int8_t new_plan_index = -1;

    // Búsqueda 1: Último plan aplicable en el DÍA ACTUAL
    int8_t best_plan_today_idx = -1;
    uint16_t best_plan_today_time = 0;
    for (uint8_t i = 0; i < MAX_PLANS; i++) {
        Plan* p = &g_plan_cache[i];
        if (p->id_tipo_dia == id_tipo_hoy) {
            uint16_t plan_time = p->hour * 60 + p->minute;
            if (plan_time <= current_time_in_minutes) {
                if (best_plan_today_idx == -1 || plan_time >= best_plan_today_time) {
                    best_plan_today_time = plan_time;
                    best_plan_today_idx = (int8_t)i;
                }
            }
        }
    }

    if (best_plan_today_idx != -1) {
        new_plan_index = best_plan_today_idx;
    } else {
        // Búsqueda 2: Si no hay plan para hoy, buscar el último plan de AYER
        uint8_t agenda[7];
        EEPROM_ReadWeeklyAgenda(agenda);
        uint8_t yesterday_dow = (now.dayOfWeek == 1) ? 7 : now.dayOfWeek - 1;
        
        // TODO: Esta lógica de feriados para el día de ayer puede mejorarse
        uint8_t id_tipo_ayer = agenda[yesterday_dow - 1];
        
        int8_t best_plan_yesterday_idx = -1;
        uint16_t best_plan_yesterday_time = 0;
        for (uint8_t i = 0; i < MAX_PLANS; i++) {
            Plan* p = &g_plan_cache[i];
            if (p->id_tipo_dia == id_tipo_ayer) {
                uint16_t plan_time = p->hour * 60 + p->minute;
                if (best_plan_yesterday_idx == -1 || plan_time >= best_plan_yesterday_time) {
                    best_plan_yesterday_time = plan_time;
                    best_plan_yesterday_idx = (int8_t)i;
                }
            }
        }

        if (best_plan_yesterday_idx != -1) {
            new_plan_index = best_plan_yesterday_idx;
        } else {
            // Búsqueda 3: Como último recurso, buscar el plan más tardío de todos
            int8_t absolute_last_plan_idx = -1;
            uint16_t absolute_last_plan_time = 0;
            bool any_plan_exists = false;
            for (uint8_t i = 0; i < MAX_PLANS; i++) {
                Plan* p = &g_plan_cache[i];
                if (p->id_tipo_dia != 0xFF) {
                    any_plan_exists = true;
                    uint16_t plan_time = p->hour * 60 + p->minute;
                    if (absolute_last_plan_idx == -1 || plan_time >= absolute_last_plan_time) {
                        absolute_last_plan_time = plan_time;
                        absolute_last_plan_idx = (int8_t)i;
                    }
                }
            }
            if (any_plan_exists) {
                new_plan_index = absolute_last_plan_idx;
            }
        }
    }

    // --- Decisión y Ejecución Final ---
    if (new_plan_index != -1) {
        // Si el plan encontrado es diferente al que está corriendo, lo cambiamos.
        if (new_plan_index != g_active_plan_index) {
            g_active_plan_index = new_plan_index;
            Plan* active_plan = &g_plan_cache[g_active_plan_index];
            Sequence_Engine_Start(active_plan->id_secuencia, active_plan->time_sel);
        }
    } else {
        // Si después de todas las búsquedas no hay ningún plan, entramos en modo Fallback.
        g_active_plan_index = -1;
        Sequence_Engine_EnterFallback();
    }
}
