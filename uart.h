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

// <<< NUEVOS COMANDOS PARA MONITOREO >>>
#define CMD_MONITOR_ENABLE 0x80
#define CMD_MONITOR_DISABLE 0x81
#define CMD_MONITOR_STATUS_REPORT 0x82

// --- Definiciones para los Comandos de Respuesta de Datos ---
// Se sigue la convención de que una respuesta a un comando CMD es (CMD | 0x80)
#define RESP_CONTROLLER_ID 0x91 // Respuesta a 0x11
#define RESP_RTC_TIME      0xA1 // Respuesta a 0x21
#define RESP_MOVEMENT_DATA 0xA4 // Respuesta a 0x24
#define RESP_SEQUENCE_DATA 0xB1 // Respuesta a 0x31
#define RESP_PLAN_DATA     0xC1 // Respuesta a 0x41
#define RESP_INTERMIT_DATA 0xD1 // Respuesta a 0x51
#define RESP_HOLIDAY_DATA  0xE1 // Respuesta a 0x61
#define RESP_FLOW_RULE_DATA 0xF1 // Respuesta a 0x71

// --- Prototipos de las nuevas funciones de respuesta ---
void UART_Send_ACK(uint8_t original_cmd);
void UART_Send_NACK(uint8_t original_cmd, uint8_t error_code);

// --- Funciones Públicas Estándar ---
void UART1_Init(uint32_t baudrate);
void UART2_Init(uint32_t baudrate);
void UART1_SendString(const char *str); // Ahora es no bloqueante
void UART_Task(void);                   // Tarea de procesamiento para el bucle principal
void UART_Send_Monitoring_Report(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ);
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

/**
 * @brief Procesa un byte recién llegado por la UART2. Llamada desde la ISR de RX.
 * @param byte El byte leído del registro RCREG2.
 */
void UART2_ProcessReceivedByte(uint8_t byte);

/**
 * @brief Tarea de procesamiento para el bucle principal (UART2).
 */
void UART2_Task(void);

#endif /* UART_H */
