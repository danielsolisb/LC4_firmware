// sequence_engine.h

#ifndef SEQUENCE_ENGINE_H
#define SEQUENCE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

void Sequence_Engine_Init(void);
void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel, int8_t plan_id);
void Sequence_Engine_Stop(void);
void Sequence_Engine_RequestPlanChange(uint8_t sec_index, uint8_t time_sel, int8_t new_plan_id);
int8_t Sequence_Engine_GetRunningPlanID(void);
void Sequence_Engine_Run(bool half_second_tick, bool one_second_tick);

// Reintroducimos la función de secuencia de inicio como una función pública y separada
void Sequence_Engine_RunStartupSequence(void);

void Sequence_Engine_EnterFallback(void);

#endif // SEQUENCE_ENGINE_H
