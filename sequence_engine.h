//sequence_engine.h

#ifndef SEQUENCE_ENGINE_H
#define SEQUENCE_ENGINE_H

#include <stdint.h>

void Sequence_Engine_Init(void);
void Sequence_Engine_Start(uint8_t sec_index, uint8_t time_sel);
void Sequence_Engine_Stop(void);
void Sequence_Engine_Run(void);

/**
 * @brief Ejecuta una secuencia de flasheo al inicio.
 * @details Es una función bloqueante diseñada para ser llamada una sola vez
 * al arrancar el sistema para dar feedback visual.
 */
void Sequence_Engine_RunStartupSequence(void);
/**
 * @brief Fuerza la entrada al modo de fallo (flasheo rojo).
 * @details Usado cuando el sistema no tiene configuración o detecta un error crítico.
 */
void Sequence_Engine_EnterFallback(void);

#endif // SEQUENCE_ENGINE_H