//uart.h
#ifndef UART_H
#define UART_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define UART_BUFFER_SIZE 64 // Se puede ajustar si se necesitan tramas m�s largas

// --- Funciones P�blicas Est�ndar ---
void UART1_Init(uint32_t baudrate);
void UART2_Init(uint32_t baudrate);
void UART1_SendString(const char *str); // Ahora es no bloqueante
void UART_Task(void);                   // Tarea de procesamiento para el bucle principal

// =============================================================================
// --- PROTOTIPOS PARA LA ISR (LA CORRECCI�N EST� AQU�) ---
// =============================================================================
// Estas funciones son llamadas desde la ISR global en timers.c y deben ser
// visibles para ese archivo, por eso se declaran aqu�.

/**
 * @brief Procesa un byte reci�n llegado por la UART. Llamada desde la ISR de RX.
 * @param byte El byte le�do del registro RCREG1.
 */
void UART_ProcessReceivedByte(uint8_t byte);

/**
 * @brief Env�a el siguiente byte del buffer de transmisi�n. Llamada desde la ISR de TX.
 */
void UART_Transmit_ISR(void);

#endif /* UART_H */
