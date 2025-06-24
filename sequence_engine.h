//sequence_engine.h

#ifndef SEQUENCE_ENGINE_H
#define SEQUENCE_ENGINE_H

#include <stdint.h>
#include <stdbool.h> // <<< CORRECCIÓN: Se añade esta línea

// --- Prototipos de Funciones Públicas ---

void Sequence_Engine_Init(void);
void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel);
void Sequence_Engine_Stop(void);

/**
 * @brief Tarea principal del motor, llamada desde el bucle principal.
 * @param half_second_tick true si ha ocurrido un tick de medio segundo.
 * @param one_second_tick true si ha ocurrido un tick de un segundo.
 */
void Sequence_Engine_Run(bool half_second_tick, bool one_second_tick);

void Sequence_Engine_RunStartupSequence(void);
void Sequence_Engine_EnterFallback(void);

#endif // SEQUENCE_ENGINE_H
