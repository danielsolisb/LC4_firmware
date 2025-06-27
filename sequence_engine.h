// sequence_engine.h

#ifndef SEQUENCE_ENGINE_H
#define SEQUENCE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

// --- Prototipos de Funciones P�blicas ---

void Sequence_Engine_Init(void);
// MODIFICADA: Ahora necesita saber qu� ID de plan est� iniciando.
void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel, int8_t plan_id);
void Sequence_Engine_Stop(void);

// NUEVA: El Scheduler llamar� a esta funci�n para solicitar un cambio de plan.
void Sequence_Engine_RequestPlanChange(uint8_t sec_index, uint8_t time_sel, int8_t new_plan_id);
// NUEVA: Devuelve el ID del plan que est� realmente en ejecuci�n.
int8_t Sequence_Engine_GetRunningPlanID(void);

void Sequence_Engine_Run(bool half_second_tick, bool one_second_tick);
void Sequence_Engine_RunStartupSequence(void);
void Sequence_Engine_EnterFallback(void);

#endif // SEQUENCE_ENGINE_H
