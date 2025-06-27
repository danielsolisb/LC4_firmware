//sequence_engine.c
#include "sequence_engine.h"
#include "eeprom.h"
#include "timers.h"
#include "config.h"
#include <xc.h>

typedef enum {
    STATE_INACTIVE,
    STATE_RUNNING_SEQUENCE,
    STATE_FALLBACK_MODE
} EngineState_t;

#define ALL_RED_MASK_D   0x92
#define ALL_RED_MASK_E   0x49
#define ALL_RED_MASK_F   0x24
#define ALL_RED_MASK_H   0x12
#define ALL_RED_MASK_J   0x14

#define ALL_YELLOW_MASK_D 0x49
#define ALL_YELLOW_MASK_E 0x24
#define ALL_YELLOW_MASK_F 0x92
#define ALL_YELLOW_MASK_H 0x00
#define ALL_YELLOW_MASK_J 0x00

static EngineState_t engine_state;
static uint8_t current_time_selector;
static uint16_t movement_countdown_s;
static struct {
    uint8_t num_movements;
    uint8_t movement_indices[12];
} active_sequence;
static uint8_t active_sequence_step;

static bool blink_phase_on = false;
static struct {
    bool active;
    uint8_t mask_d;
    uint8_t mask_e;
    uint8_t mask_f;
} active_intermittence_rule;

static uint8_t current_mov_ports[5];

static bool plan_change_pending = false;
static uint8_t pending_sec_index;
static uint8_t pending_time_sel;
static int8_t pending_plan_id;
static int8_t running_plan_id = -1;

static void apply_light_outputs(void);

// --- NUEVA FUNCIÓN DE RETARDO SEGURA ---
// Crea un retardo en milisegundos, pero limpia el Watchdog Timer
// en cada paso para evitar reinicios durante secuencias largas.
static void Safe_Delay_ms(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        CLRWDT();
        __delay_ms(1);
    }
}

// --- SECUENCIA DE INICIO RESTAURADA Y SEGURA ---
void Sequence_Engine_RunStartupSequence(void) {
    uint8_t i;
    uint8_t mov0_ports[5];
    uint8_t mov0_times[5]; // No se usa, pero la función lo requiere
    
    // 1. Leer el Movimiento 0 de la EEPROM
    EEPROM_ReadMovement(0, &mov0_ports[0], &mov0_ports[1], &mov0_ports[2], &mov0_ports[3], &mov0_ports[4], mov0_times);

    // --- NUEVA VALIDACIÓN DE SEGURIDAD ---
    // Se comprueba si el movimiento leído es válido. Un movimiento inválido
    // (típicamente 0xFF en todos sus bytes) indica que la EEPROM está vacía.
    if (!EEPROM_IsMovementValid(mov0_ports[0], mov0_ports[1], mov0_ports[2], mov0_ports[3], mov0_ports[4], mov0_times)) {
        // Si no es válido, se carga un patrón seguro por defecto (Todos los Rojos)
        // para la primera fase de la secuencia de inicio.
        mov0_ports[0] = ALL_RED_MASK_D;
        mov0_ports[1] = ALL_RED_MASK_E;
        mov0_ports[2] = ALL_RED_MASK_F;
        mov0_ports[3] = ALL_RED_MASK_H;
        mov0_ports[4] = ALL_RED_MASK_J;
    }
    // Si el movimiento es válido, la función continuará usando los datos leídos.
    // --- FIN DE LA VALIDACIÓN ---

    // El resto de la lógica de la secuencia no cambia.
    
    // FASE 1: Parpadeo del Movimiento 0 (o del patrón seguro) por 5 segundos
    for (i = 0; i < 5; i++) {
        LATD = mov0_ports[0]; LATE = mov0_ports[1]; LATF = mov0_ports[2]; LATH = mov0_ports[3]; LATJ = mov0_ports[4];
        Safe_Delay_ms(500);
        LATD = 0; LATE = 0; LATF = 0; LATH = 0; LATJ = 0;
        Safe_Delay_ms(500);
    }

    // FASE 2: Parpadeo de Todos los Rojos (3 segundos)
    for (i = 0; i < 3; i++) {
        LATD = ALL_RED_MASK_D; LATE = ALL_RED_MASK_E; LATF = ALL_RED_MASK_F; LATH = ALL_RED_MASK_H; LATJ = ALL_RED_MASK_J;
        Safe_Delay_ms(500);
        LATD = 0; LATE = 0; LATF = 0; LATH = 0; LATJ = 0;
        Safe_Delay_ms(500);
    }

    // FASE 3: Parpadeo de Todos los Amarillos (3 segundos)
    for (i = 0; i < 3; i++) {
        LATD = ALL_YELLOW_MASK_D; LATE = ALL_YELLOW_MASK_E; LATF = ALL_YELLOW_MASK_F; LATH = ALL_YELLOW_MASK_H; LATJ = ALL_YELLOW_MASK_J;
        Safe_Delay_ms(500);
        LATD = 0; LATE = 0; LATF = 0; LATH = 0; LATJ = 0;
        Safe_Delay_ms(500);
    }
}


void Sequence_Engine_Init(void) {
    engine_state = STATE_FALLBACK_MODE; // Seguro por defecto
    active_sequence_step = 0;
    movement_countdown_s = 0;
    active_intermittence_rule.active = false;
    plan_change_pending = false;
    running_plan_id = -1;
    LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
}

void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel, int8_t plan_id) {
    plan_change_pending = false;
    running_plan_id = plan_id;

    if (sec_index >= MAX_SEQUENCES) {
        engine_state = STATE_FALLBACK_MODE;
        return;
    }
    EEPROM_ReadSequence(sec_index, &active_sequence.num_movements, active_sequence.movement_indices);
    if (active_sequence.num_movements > 0 && active_sequence.num_movements <= 12) {
        engine_state = STATE_RUNNING_SEQUENCE;
        current_time_selector = time_sel;
        active_sequence_step = 0;
        movement_countdown_s = 0;
        active_intermittence_rule.active = false;
    } else {
        engine_state = STATE_FALLBACK_MODE;
    }
}

void Sequence_Engine_RequestPlanChange(uint8_t sec_index, uint8_t time_sel, int8_t new_plan_id) {
    plan_change_pending = true;
    pending_sec_index = sec_index;
    pending_time_sel = time_sel;
    pending_plan_id = new_plan_id;
}

int8_t Sequence_Engine_GetRunningPlanID(void) {
    return running_plan_id;
}

void Sequence_Engine_Stop(void) {
    engine_state = STATE_INACTIVE;
    running_plan_id = -1;
    LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
}

void Sequence_Engine_EnterFallback(void) {
    engine_state = STATE_FALLBACK_MODE;
    running_plan_id = -1;
}

void Sequence_Engine_Run(bool half_second_tick, bool one_second_tick) {
    if (half_second_tick) {
        blink_phase_on = !blink_phase_on;
    }

    switch (engine_state) {
        case STATE_RUNNING_SEQUENCE:
            if (one_second_tick && movement_countdown_s > 0) {
                movement_countdown_s--;
            }

            if (movement_countdown_s == 0) {
                if (active_sequence_step == 0 && active_sequence.num_movements > 0) {
                    if (plan_change_pending) {
                        Sequence_Engine_Start(pending_sec_index, pending_time_sel, pending_plan_id);
                        break;
                    }
                }

                if (active_sequence.num_movements == 0) {
                    engine_state = STATE_FALLBACK_MODE; break;
                }
                uint8_t mov_idx = active_sequence.movement_indices[active_sequence_step];
                if (mov_idx >= MAX_MOVEMENTS) {
                    engine_state = STATE_FALLBACK_MODE; break;
                }
                uint8_t times[5];
                EEPROM_ReadMovement(mov_idx, &current_mov_ports[0], &current_mov_ports[1], &current_mov_ports[2], &current_mov_ports[3], &current_mov_ports[4], times);
                movement_countdown_s = (current_time_selector < 5) ? times[current_time_selector] : 1;
                if (movement_countdown_s == 0) movement_countdown_s = 1;

                active_intermittence_rule.active = false;
                if (running_plan_id != -1) {
                    for (uint8_t i = 0; i < MAX_INTERMITENCES; i++) {
                        uint8_t p_id, m_id, mD, mE, mF;
                        EEPROM_ReadIntermittence(i, &p_id, &m_id, &mD, &mE, &mF);
                        if (p_id == (uint8_t)running_plan_id && m_id == mov_idx) {
                            active_intermittence_rule.active = true;
                            active_intermittence_rule.mask_d = mD; active_intermittence_rule.mask_e = mE; active_intermittence_rule.mask_f = mF;
                            break;
                        }
                    }
                }
                active_sequence_step = (active_sequence_step + 1) % active_sequence.num_movements;
            }
            apply_light_outputs();
            break;

        case STATE_FALLBACK_MODE:
            if (blink_phase_on) {
                LATD = ALL_RED_MASK_D; LATE = ALL_RED_MASK_E; LATF = ALL_RED_MASK_F; LATH = ALL_RED_MASK_H; LATJ = ALL_RED_MASK_J;
            } else {
                LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
            }
            break;

        case STATE_INACTIVE:
            // No hacer nada, luces apagadas.
            break;
    }
}

// --- LÓGICA DE INTERMITENCIA CORREGIDA Y DEFINITIVA ---
static void apply_light_outputs(void) {
    if (active_intermittence_rule.active) {
        uint8_t pD = current_mov_ports[0];
        uint8_t pE = current_mov_ports[1];
        uint8_t pF = current_mov_ports[2];
        uint8_t maskD = active_intermittence_rule.mask_d;
        uint8_t maskE = active_intermittence_rule.mask_e;
        uint8_t maskF = active_intermittence_rule.mask_f;

        if (blink_phase_on) { // Fase ON del parpadeo
            // Se encienden las luces sólidas (no en máscara) Y las luces intermitentes (en máscara)
            LATD = (pD & ~maskD) | maskD;
            LATE = (pE & ~maskE) | maskE;
            LATF = (pF & ~maskF) | maskF;
        } else { // Fase OFF del parpadeo
            // Se encienden SÓLO las luces sólidas. Las intermitentes se apagan.
            LATD = pD & ~maskD;
            LATE = pE & ~maskE;
            LATF = pF & ~maskF;
        }
    } else { // No hay intermitencia, todo es sólido
        LATD = current_mov_ports[0];
        LATE = current_mov_ports[1];
        LATF = current_mov_ports[2];
    }
    
    // Los puertos H y J nunca son intermitentes en este diseño
    LATH = current_mov_ports[3];
    LATJ = current_mov_ports[4];
}
