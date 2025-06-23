//sequence_engine.c
#include "sequence_engine.h"
#include "eeprom.h"
#include "timers.h"
#include "config.h" 
#include <xc.h>

// Definición del tipo de dato para los estados del motor.
typedef enum {
    STATE_INACTIVE,
    STATE_RUNNING_SEQUENCE,
    STATE_FALLBACK_MODE
} EngineState_t;

// Definiciones de las máscaras de luces rojas para el modo de fallo.
#define RED_MASK_D   0x92
#define RED_MASK_E   0x49
#define RED_MASK_F   0x24
#define RED_MASK_H   0x12
#define RED_MASK_J   0x14

// Variables estáticas (internas) del módulo.
// Aquí se define e inicializa la variable de estado correctamente.
static EngineState_t engine_state = STATE_INACTIVE;
static uint8_t current_time_selector;
static uint16_t movement_countdown_s;
static uint8_t half_second_tick_count = 0;

static struct {
    uint8_t num_movements;
    uint8_t movement_indices[12];
} active_sequence;
static uint8_t active_sequence_step;


void Sequence_Engine_Init(void) {
    engine_state = STATE_INACTIVE;
    active_sequence_step = 0;
    movement_countdown_s = 1;
    half_second_tick_count = 0;

    // Apagar todas las luces al iniciar
    LATD = 0x00;
    LATE = 0x00;
    LATF = 0x00;
    LATH = 0x00;
    LATJ = 0x00;
}

void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel) {
    if (sec_index >= MAX_SEQUENCES) {
        engine_state = STATE_FALLBACK_MODE;
        return;
    }

    EEPROM_ReadSequence(sec_index, &active_sequence.num_movements, active_sequence.movement_indices);
    
    if (active_sequence.num_movements > 0 && active_sequence.num_movements <= 12) {
        current_time_selector = time_sel;
        active_sequence_step = 0;
        movement_countdown_s = 1; 
        engine_state = STATE_RUNNING_SEQUENCE;
        half_second_tick_count = 0;
    } else {
        engine_state = STATE_FALLBACK_MODE;
    }
}

void Sequence_Engine_Stop(void) {
    engine_state = STATE_INACTIVE;
    LATD = 0x00;
    LATE = 0x00;
    LATF = 0x00;
    LATH = 0x00;
    LATJ = 0x00;
}

void Sequence_Engine_Run(void) {
    static bool red_flash_on = false;

    switch (engine_state) {
        case STATE_INACTIVE:
            // No hacer nada, las luces ya están apagadas.
            break;
            
        case STATE_RUNNING_SEQUENCE:
            half_second_tick_count++;
            if (half_second_tick_count >= 2) {
                half_second_tick_count = 0;
                if (movement_countdown_s > 0) {
                    movement_countdown_s--;
                }
            }
            
            if (movement_countdown_s == 0) {
                if (active_sequence.num_movements == 0) {
                    engine_state = STATE_FALLBACK_MODE;
                    break;
                }

                uint8_t mov_idx = active_sequence.movement_indices[active_sequence_step];
                if (mov_idx >= MAX_MOVEMENTS) {
                    engine_state = STATE_FALLBACK_MODE;
                    break;
                }

                uint8_t pD, pE, pF, pH, pJ;
                uint8_t times[5];
                EEPROM_ReadMovement(mov_idx, &pD, &pE, &pF, &pH, &pJ, times);

                LATD = pD; LATE = pE; LATF = pF;
                LATH = pH; LATJ = pJ;

                if (current_time_selector >= 5) {
                    current_time_selector = 0;
                }
                movement_countdown_s = times[current_time_selector];
                if (movement_countdown_s == 0) movement_countdown_s = 1;

                active_sequence_step = (active_sequence_step + 1) % active_sequence.num_movements;
            }
            break;

        case STATE_FALLBACK_MODE:
        default:
             red_flash_on = !red_flash_on;
             if (red_flash_on) {
                 LATD = RED_MASK_D; LATE = RED_MASK_E; LATF = RED_MASK_F;
                 LATH = RED_MASK_H; LATJ = RED_MASK_J;
             } else {
                 LATD = 0x00; LATE = 0x00; LATF = 0x00;
                 LATH = 0x00; LATJ = 0x00;
             }
            break;
    }
}

void Sequence_Engine_RunStartupSequence(void) {
    bool lights_on = false;
    uint16_t num_cycles = (STARTUP_SEQUENCE_DURATION_S * 1000) / (STARTUP_FLASH_DELAY_MS * 2);

    for (uint16_t i = 0; i < num_cycles; i++) {
        // >>> CORRECCIÓN: Limpiar el Watchdog en cada ciclo <<<
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
            // >>> CORRECCIÓN: Limpiar el Watchdog también en el delay interno <<<
            CLRWDT(); 
            __delay_ms(1);
        }
    }
    
    LATD = 0x00; LATE = 0x00; LATF = 0x00; LATH = 0x00; LATJ = 0x00;
}

void Sequence_Engine_EnterFallback(void) {
    engine_state = STATE_FALLBACK_MODE;
}