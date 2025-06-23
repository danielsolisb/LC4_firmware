// rtc.h
#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

// La estructura de datos se mantiene igual para compatibilidad con tu proyecto
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t dayOfWeek; // 1-7, donde el valor exacto depende de tu convenci�n
} RTC_Time;

// --- Funciones P�blicas ---

// Inicializa el RTC, asegurando que el reloj est� corriendo.
void RTC_Init(void);

// Establece la fecha y hora en el RTC.
bool RTC_SetTime(RTC_Time *time);

// Obtiene la fecha y hora del RTC.
void RTC_GetTime(RTC_Time *time);

// --- Funciones de prueba (�tiles para depuraci�n) ---
bool RTC_TestRAM(void);
void RTC_PerformVisualTest(void);

#endif // RTC_H
