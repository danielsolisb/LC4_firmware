//uart.h
#ifndef UART_H
#define UART_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define UART_BUFFER_SIZE 64 // Se puede ajustar si se necesitan tramas más largas

// --- Definiciones para el Protocolo ACK/NACK ---
#define CMD_ACK 0x06 // Código para una respuesta de confirmación exitosa
#define CMD_NACK 0x15 // Código para una respuesta de error

// --- Códigos de Error para el Payload del NACK ---
#define ERROR_CHECKSUM_INVALID 0x01
#define ERROR_UNKNOWN_CMD 0x02
#define ERROR_INVALID_LENGTH 0x03
#define ERROR_INVALID_DATA 0x04
#define ERROR_EXECUTION_FAIL 0x05

// --- Prototipos de las nuevas funciones de respuesta ---
void UART_Send_ACK(uint8_t original_cmd);
void UART_Send_NACK(uint8_t original_cmd, uint8_t error_code);

// --- Funciones Públicas Estándar ---
void UART1_Init(uint32_t baudrate);
void UART2_Init(uint32_t baudrate);
void UART1_SendString(const char *str); // Ahora es no bloqueante
void UART_Task(void);                   // Tarea de procesamiento para el bucle principal

// =============================================================================
// --- PROTOTIPOS PARA LA ISR (LA CORRECCIÓN ESTÁ AQUÍ) ---
// =============================================================================
// Estas funciones son llamadas desde la ISR global en timers.c y deben ser
// visibles para ese archivo, por eso se declaran aquí.

/**
 * @brief Procesa un byte recién llegado por la UART. Llamada desde la ISR de RX.
 * @param byte El byte leído del registro RCREG1.
 */
void UART_ProcessReceivedByte(uint8_t byte);

/**
 * @brief Envía el siguiente byte del buffer de transmisión. Llamada desde la ISR de TX.
 */
void UART_Transmit_ISR(void);

#endif /* UART_H */
