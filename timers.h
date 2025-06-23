//timer.h 
#ifndef TIMERS_H
#define TIMERS_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

// Bandera para el tick de 1 segundo (usada por el Scheduler)
extern volatile bool g_one_second_flag;

// --- NUEVA BANDERA ---
// Bandera para el tick de 0.5 segundos (usada por el Sequence Engine)
extern volatile bool g_half_second_flag;

void Timers_Init(void);

#endif // TIMERS_H
