//uart.h
#ifndef UART_H
#define UART_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

#define UART_BUFFER_SIZE 64 // Se puede ajustar si se necesitan tramas m�s largas

// --- Definiciones para el Protocolo ACK/NACK ---
#define CMD_ACK 0x06 // C�digo para una respuesta de confirmaci�n exitosa
#define CMD_NACK 0x15 // C�digo para una respuesta de error

// --- C�digos de Error para el Payload del NACK ---
#define ERROR_CHECKSUM_INVALID 0x01
#define ERROR_UNKNOWN_CMD 0x02
#define ERROR_INVALID_LENGTH 0x03
#define ERROR_INVALID_DATA 0x04
#define ERROR_EXECUTION_FAIL 0x05

// <<< NUEVOS COMANDOS PARA MONITOREO >>>
#define CMD_MONITOR_ENABLE 0x80
#define CMD_MONITOR_DISABLE 0x81
#define CMD_MONITOR_STATUS_REPORT 0x82

// --- Prototipos de las nuevas funciones de respuesta ---
void UART_Send_ACK(uint8_t original_cmd);
void UART_Send_NACK(uint8_t original_cmd, uint8_t error_code);

// --- Funciones P�blicas Est�ndar ---
void UART1_Init(uint32_t baudrate);
void UART2_Init(uint32_t baudrate);
void UART1_SendString(const char *str); // Ahora es no bloqueante
void UART_Task(void);                   // Tarea de procesamiento para el bucle principal
void UART_Send_Monitoring_Report(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ);
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
