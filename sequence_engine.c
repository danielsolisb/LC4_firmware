//sequence_engine.c
#include "sequence_engine.h"
#include "eeprom.h"
#include "timers.h"
#include "config.h"
// Se elimina la inclusión de scheduler.h ya que no se usa directamente
#include <xc.h>

// --- Definiciones de Estados y Variables Estáticas ---
typedef enum {
    STATE_INACTIVE,
    STATE_RUNNING_SEQUENCE,
    STATE_FALLBACK_MODE
} EngineState_t;

#define RED_MASK_D   0x92
#define RED_MASK_E   0x49
#define RED_MASK_F   0x24
#define RED_MASK_H   0x12
#define RED_MASK_J   0x14

static EngineState_t engine_state = STATE_INACTIVE;
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

// --- NUEVAS VARIABLES PARA CAMBIO DE PLAN CONTROLADO ---
static bool plan_change_pending = false;
static uint8_t pending_sec_index;
static uint8_t pending_time_sel;
static int8_t pending_plan_id;
static int8_t running_plan_id = -1; // ID del plan actualmente en ejecución

// Prototipo de función interna
static void apply_light_outputs(void);

// --- Funciones de Control ---
void Sequence_Engine_Init(void) {
    // --- CAMBIO CRÍTICO ---
    // El estado inicial ahora es FALLBACK, no INACTIVE.
    // Esto garantiza que si nada más da una orden, el controlador
    // entra en modo seguro (rojo intermitente) por defecto.
    engine_state = STATE_FALLBACK_MODE;

    active_sequence_step = 0;
    movement_countdown_s = 0;
    active_intermittence_rule.active = false;
    plan_change_pending = false;
    running_plan_id = -1;
    LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
}

// MODIFICADA: Ahora recibe y almacena el plan_id
void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel, int8_t plan_id) {
    // Un inicio forzado cancela cualquier cambio que estuviera pendiente
    plan_change_pending = false;
    running_plan_id = plan_id;

    if (sec_index >= MAX_SEQUENCES) {
        engine_state = STATE_FALLBACK_MODE;
        return;
    }
    EEPROM_ReadSequence(sec_index, &active_sequence.num_movements, active_sequence.movement_indices);
    if (active_sequence.num_movements > 0 && active_sequence.num_movements <= 12) {
        current_time_selector = time_sel;
        active_sequence_step = 0;
        movement_countdown_s = 0; // Forzar cambio de movimiento inmediato
        engine_state = STATE_RUNNING_SEQUENCE;
        active_intermittence_rule.active = false;
    } else {
        engine_state = STATE_FALLBACK_MODE;
    }
}

// NUEVA: Almacena la solicitud de cambio para ser procesada al final del ciclo
void Sequence_Engine_RequestPlanChange(uint8_t sec_index, uint8_t time_sel, int8_t new_plan_id) {
    plan_change_pending = true;
    pending_sec_index = sec_index;
    pending_time_sel = time_sel;
    pending_plan_id = new_plan_id;
}

// NUEVA: Permite a otros módulos saber qué plan está corriendo realmente
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

// --- TAREA PRINCIPAL (LÓGICA DE CAMBIO DE PLAN ACTUALIZADA) ---
void Sequence_Engine_Run(bool half_second_tick, bool one_second_tick) {
    if (half_second_tick) {
        blink_phase_on = !blink_phase_on;
    }

    if (one_second_tick) {
        if (engine_state == STATE_RUNNING_SEQUENCE && movement_countdown_s > 0) {
            movement_countdown_s--;
        }
    }

    if (engine_state == STATE_RUNNING_SEQUENCE && movement_countdown_s == 0) {
        // --- LÓGICA DE FIN DE CICLO Y CAMBIO DE PLAN ---
        if (active_sequence_step == 0 && active_sequence.num_movements > 0) { // Indica que un ciclo completo acaba de terminar
            if (plan_change_pending) {
                // Hay un cambio pendiente, lo ejecutamos AHORA.
                Sequence_Engine_Start(pending_sec_index, pending_time_sel, pending_plan_id);
                // La llamada a Start ya resetea la bandera y actualiza el running_plan_id
                // Salimos para que el próximo ciclo de Run() procese el nuevo estado.
                return;
            }
        }

        if (active_sequence.num_movements == 0) {
            engine_state = STATE_FALLBACK_MODE;
            return;
        }

        uint8_t mov_idx = active_sequence.movement_indices[active_sequence_step];
        if (mov_idx >= MAX_MOVEMENTS) {
            engine_state = STATE_FALLBACK_MODE;
            return;
        }

        uint8_t times[5];
        EEPROM_ReadMovement(mov_idx, &current_mov_ports[0], &current_mov_ports[1], &current_mov_ports[2], &current_mov_ports[3], &current_mov_ports[4], times);

        if (current_time_selector >= 5) current_time_selector = 0;
        movement_countdown_s = times[current_time_selector];
        if (movement_countdown_s == 0) movement_countdown_s = 1;

        active_intermittence_rule.active = false;
        // La comprobación de intermitencia ahora usa el ID del plan que realmente corre
        if (running_plan_id != -1) {
            for (uint8_t i = 0; i < MAX_INTERMITENCES; i++) {
                uint8_t p_id, m_id, mD, mE, mF;
                EEPROM_ReadIntermittence(i, &p_id, &m_id, &mD, &mE, &mF);
                if (p_id == (uint8_t)running_plan_id && m_id == mov_idx) {
                    active_intermittence_rule.active = true;
                    active_intermittence_rule.mask_d = mD;
                    active_intermittence_rule.mask_e = mE;
                    active_intermittence_rule.mask_f = mF;
                    break;
                }
            }
        }
        
        // Avanzar al siguiente paso de la secuencia
        active_sequence_step = (active_sequence_step + 1) % active_sequence.num_movements;
    }

    if (engine_state == STATE_RUNNING_SEQUENCE) {
        apply_light_outputs();
    } else if (engine_state == STATE_FALLBACK_MODE) {
        if (blink_phase_on) {
            LATD = RED_MASK_D; LATE = RED_MASK_E; LATF = RED_MASK_F; LATH = RED_MASK_H; LATJ = RED_MASK_J;
        } else {
            LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
        }
    }
}

static void apply_light_outputs(void) {
    if (active_intermittence_rule.active) {
        uint8_t pD = current_mov_ports[0], pE = current_mov_ports[1], pF = current_mov_ports[2];
        uint8_t maskD = active_intermittence_rule.mask_d, maskE = active_intermittence_rule.mask_e, maskF = active_intermittence_rule.mask_f;
        
        LATD = (pD & ~maskD) | (blink_phase_on ? (pD & maskD) : 0);
        LATE = (pE & ~maskE) | (blink_phase_on ? (pE & maskE) : 0);
        LATF = (pF & ~maskF) | (blink_phase_on ? (pF & maskF) : 0);
    } else {
        LATD = current_mov_ports[0]; LATE = current_mov_ports[1]; LATF = current_mov_ports[2];
    }
    LATH = current_mov_ports[3]; LATJ = current_mov_ports[4];
}

void Sequence_Engine_RunStartupSequence(void) {
    bool lights_on = false;
    uint16_t num_cycles = (STARTUP_SEQUENCE_DURATION_S * 1000) / (STARTUP_FLASH_DELAY_MS * 2);

    for (uint16_t i = 0; i < num_cycles; i++) {
        CLRWDT(); 
        lights_on = !lights_on;
        if (lights_on) {
            LATD = STARTUP_MASK_D;
            LATE = STARTUP_MASK_E;
            LATF = STARTUP_MASK_F;
        } else {
            LATD = 0x00;
            LATE = 0x00;
            LATF = 0x00;
        }
        for(uint8_t d=0; d < STARTUP_FLASH_DELAY_MS; d++) {
            CLRWDT(); 
            __delay_ms(1);
        }
    }
    
    LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
}