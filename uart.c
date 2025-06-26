//uart.c
#include <string.h>
#include <stdio.h>
#include "uart.h"
#include "config.h"
#include "eeprom.h"
#include "rtc.h"
#include "scheduler.h"

// --- Buffers para comunicación asíncrona ---
#define UART_TX_BUFFER_SIZE 128
#define UART_RX_BUFFER_SIZE 64

static volatile uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint8_t tx_head = 0;
static volatile uint8_t tx_tail = 0;

static volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t uart_rx_index = 0;
static volatile bool g_frame_received = false;

// Prototipo de la función que construye y envía una trama
static void UART_Send_Frame(uint8_t cmd, uint8_t* payload, uint8_t len);

#define UART_RESPONSE_BUFFER_SIZE 128
static char uart_response_buffer[UART_RESPONSE_BUFFER_SIZE];

static void UART_HandleCompleteFrame(uint8_t *buffer, uint8_t length);

// >>> NUEVO CERROJO (LOCK) POR SOFTWARE <<<
static volatile bool g_uart_processing_lock = false;

void UART1_Init(uint32_t baudrate) {
    TRISCbits.TRISC6 = 0; 
    TRISCbits.TRISC7 = 1; 
    TXSTA1 = 0x24;        
    RCSTA1 = 0x90;        
    SPBRG1 = 129; 
    
    PIE1bits.RC1IE = 1;
}

void UART2_Init(uint32_t baudrate) { /* No implementado */ }

void UART_Transmit_ISR(void) {
    if (tx_head != tx_tail) {
        TXREG1 = uart_tx_buffer[tx_tail];
        tx_tail = (tx_tail + 1) % UART_TX_BUFFER_SIZE;
    } else {
        PIE1bits.TX1IE = 0;
    }
}

void UART1_SendString(const char *str) {
    while (*str) {
        uart_tx_buffer[tx_head] = *str++;
        tx_head = (tx_head + 1) % UART_TX_BUFFER_SIZE;
    }
    PIE1bits.TX1IE = 1;
}

/**
 * @brief Construye y envía una trama de confirmación (ACK).
 * @param original_cmd El comando que se está confirmando.
 */
void UART_Send_ACK(uint8_t original_cmd) {
    uint8_t payload[1];
    payload[0] = original_cmd;
    UART_Send_Frame(CMD_ACK, payload, 1);
}

/**
 * @brief Construye y envía una trama de error (NACK).
 * @param original_cmd El comando que falló.
 * @param error_code El código que especifica la razón del fallo.
 */
void UART_Send_NACK(uint8_t original_cmd, uint8_t error_code) {
    uint8_t payload[2];
    payload[0] = original_cmd;
    payload[1] = error_code;
    UART_Send_Frame(CMD_NACK, payload, 2);
}

/**
 * @brief Función interna para construir y encolar cualquier trama de respuesta.
 */
static void UART_Send_Frame(uint8_t cmd, uint8_t* payload, uint8_t len) {
    uint8_t frame[UART_TX_BUFFER_SIZE];
    uint8_t frame_idx = 0;
    uint8_t checksum = cmd + len;

    // Encabezado
    frame[frame_idx++] = 0x43;
    frame[frame_idx++] = 0x53;
    frame[frame_idx++] = 0x4F;
    
    // Comando y Longitud
    frame[frame_idx++] = cmd;
    frame[frame_idx++] = len;

    // Payload y cálculo de Checksum
    for(uint8_t i = 0; i < len; i++) {
        frame[frame_idx++] = payload[i];
        checksum += payload[i];
    }

    // Checksum y Fin de Trama
    frame[frame_idx++] = checksum;
    frame[frame_idx++] = 0x03;
    frame[frame_idx++] = 0xFF;

    // Enviar la trama al buffer de transmisión
    for(uint8_t i = 0; i < frame_idx; i++) {
        uart_tx_buffer[tx_head] = frame[i];
        tx_head = (tx_head + 1) % UART_TX_BUFFER_SIZE;
    }
    PIE1bits.TX1IE = 1; // Habilitar interrupción de transmisión
}

// =============================================================================
// >>> FUNCIÓN UART_Task MODIFICADA (Usa el cerrojo en lugar de deshabilitar la ISR) <<<
// =============================================================================
void UART_Task(void) {
    if (!g_frame_received) {
        return;
    }

    // Ponemos el cerrojo para que la ISR no modifique el buffer mientras lo leemos.
    g_uart_processing_lock = true;

    // Procesamos la trama directamente desde el buffer global.
    // Es seguro porque la ISR no escribirá en él gracias al cerrojo.
    UART_HandleCompleteFrame((uint8_t*)uart_rx_buffer, uart_rx_index);

    // Reseteamos la bandera de trama recibida.
    g_frame_received = false;

    // Quitamos el cerrojo para permitir que la ISR reciba nuevas tramas.
    g_uart_processing_lock = false;
}


// =============================================================================
// >>> FUNCIÓN UART_ProcessReceivedByte MODIFICADA (Usa el cerrojo) <<<
// =============================================================================
void UART_ProcessReceivedByte(uint8_t byte) {
    // Si el bucle principal está procesando, ignoramos los bytes nuevos.
    if (g_uart_processing_lock) {
        return;
    }

    // La lógica inteligente de recepción se mantiene igual.
    static uint8_t stx_sequence[3] = {0x43, 0x53, 0x4F};
    static uint8_t stx_counter = 0;
    static bool receiving_in_progress = false;

    if (!receiving_in_progress) {
        if (byte == stx_sequence[stx_counter]) {
            if (++stx_counter >= 3) {
                receiving_in_progress = true;
                uart_rx_index = 0;
                stx_counter = 0;
            }
        } else {
            stx_counter = 0;
        }
    } else {
        if (uart_rx_index < UART_RX_BUFFER_SIZE) {
            uart_rx_buffer[uart_rx_index++] = byte;
            if (uart_rx_index >= 2) {
                uint8_t payload_len = uart_rx_buffer[1];
                uint16_t total_expected_bytes = payload_len + 5;
                if (uart_rx_index >= total_expected_bytes) {
                    if (uart_rx_buffer[uart_rx_index - 2] == 0x03 && uart_rx_buffer[uart_rx_index - 1] == 0xFF) {
                        g_frame_received = true;
                        uart_rx_index -= 2;
                    }
                    receiving_in_progress = false;
                }
            }
        } else {
            receiving_in_progress = false;
        }
    }
}


// =============================================================================
// --- FUNCIÓN INTERNA PARA MANEJAR TRAMAS ---
// =============================================================================
// Esta función procesa una trama completa recibida por la UART.
// Se hace 'static' porque es una función interna de este módulo.
static void UART_HandleCompleteFrame(uint8_t *buffer, uint8_t length) {
    uint8_t cmd = buffer[0];
    uint8_t len = buffer[1];
    if (length != (len + 3)) {
        UART1_SendString("ERROR: Inconsistencia en longitud\r\n");
        return;
    }
    uint8_t chk_calc = cmd + len;
    for (uint8_t i = 0; i < len; i++) chk_calc += buffer[2 + i];
    uint8_t chk_received = buffer[2 + len];
    if (chk_calc != chk_received) {
        UART_Send_NACK(cmd, ERROR_CHECKSUM_INVALID);
        return;
    }

    // --- Estructura de comandos ---
    switch(cmd){
        
        // --- Comandos de EEPROM (no requieren bloqueo de RTC) ---
        case 0x10: { // Guardar ID
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveControllerID(buffer[2]);
            UART_Send_ACK(cmd);
            break;
        }
        case 0x11: { // Leer ID de controlador
            if(len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t id = EEPROM_ReadControllerID();
            //char msg[64];
            sprintf(uart_response_buffer, "ID:%02X\r\n", id);
            UART1_SendString(uart_response_buffer);
            break;
        }
        // ... Aquí irían todos tus otros case para EEPROM: 0x23, 0x24, 0x30, etc. ...


        // --- Comandos de RTC (requieren bloqueo con semáforo) ---
        case 0x21: { // Consultar Hora
            if(len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            
            g_rtc_access_in_progress = true; // <<-- BLOQUEAR ACCESO
            
            RTC_Time rtc;
            RTC_GetTime(&rtc);
            
            // Usamos el buffer estático que definimos para evitar problemas de memoria
            sprintf(uart_response_buffer, "Hora: %02d:%02d:%02d - Fecha: %02d/%02d/%02d (DoW: %d)\r\n", 
                    rtc.hour, rtc.minute, rtc.second, rtc.day, rtc.month, rtc.year, rtc.dayOfWeek);
            
            UART1_SendString(uart_response_buffer);
            
            g_rtc_access_in_progress = false; // <<-- LIBERAR ACCESO
            break;
        }
        
        case 0x22: { // Guardar Fecha/Hora
            if (len != 7) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            RTC_Time new_time;
            new_time.hour = buffer[2]; new_time.minute = buffer[3]; new_time.second = buffer[4];
            new_time.day = buffer[5]; new_time.month = buffer[6]; new_time.year = buffer[7];
            new_time.dayOfWeek = buffer[8];
            
            g_rtc_access_in_progress = true;
            RTC_SetTime(&new_time);
            g_rtc_access_in_progress = false;
            Scheduler_ReloadCache();
            UART_Send_ACK(cmd);
            break;
        }
        
        case 0x23: { // Guardar Movimiento
            if (len != 11) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveMovement(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], &buffer[8]);
            UART_Send_ACK(cmd);
            break;
        }
        
        case 0x24: { // Leer Movimiento
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t index = buffer[2];
    
            // <<< PASO 1: Declarar las variables para los nuevos puertos H y J
            uint8_t portD, portE, portF, portH, portJ; 
            uint8_t times[5];
    
            // <<< PASO 2: Pasar las direcciones de H y J a la función
            EEPROM_ReadMovement(index, &portD, &portE, &portF, &portH, &portJ, times);
    
            // <<< PASO 3: Pasar los valores de H y J a la función de validación
            if(!EEPROM_IsMovementValid(portD, portE, portF, portH, portJ, times)){
                UART1_SendString("Movimiento no existe o esta vacio\r\n");
            } else {
                // La cadena de respuesta ya estaba correcta, se mantiene igual
                //char msg[120]; 
                sprintf(uart_response_buffer, "Movimiento[%d]: D:%02X E:%02X F:%02X H:%02X J:%02X T:[%02X %02X %02X %02X %02X]\r\n",
                        index, portD, portE, portF, portH, portJ,
                        times[0], times[1], times[2], times[3], times[4]);
                UART1_SendString(uart_response_buffer);
            }
            break;
        }
        
        case 0x25: { // Prueba directa al RTC
            g_rtc_access_in_progress = true; // <<-- BLOQUEAR ACCESO
            
            UART1_SendString("DEBUG: Ejecutando prueba directa al RTC...\r\n");
            RTC_Time test_time = {12, 34, 56, 15, 11, 24, 5};
            if (RTC_SetTime(&test_time)) {
                UART1_SendString("DEBUG: RTC_SetTime ejecutado. Consulta la hora.\r\n");
            } else {
                UART1_SendString("DEBUG: RTC_SetTime reporto un fallo.\r\n");
            }

            g_rtc_access_in_progress = false; // <<-- LIBERAR ACCESO
            break;
        }
        case 0x26: { // Prueba de RAM del RTC
            g_rtc_access_in_progress = true; // <<-- BLOQUEAR ACCESO
            
            UART1_SendString("DEBUG: Solicitando prueba de RAM al modulo RTC...\r\n");
            if (RTC_TestRAM()) {
                UART1_SendString("EXITO: La prueba de RAM del RTC fue exitosa.\r\n");
            } else {
                UART1_SendString("FALLO: La prueba de RAM del RTC ha fallado.\r\n");
            }
            
            g_rtc_access_in_progress = false; // <<-- LIBERAR ACCESO
            break;
        }
        case 0x27: { // Prueba Visual
            g_rtc_access_in_progress = true; // <<-- BLOQUEAR ACCESO
            
            UART1_SendString("DEBUG: Solicitando prueba visual al modulo RTC...\r\n");
            RTC_PerformVisualTest();
            UART1_SendString("DEBUG: Prueba visual finalizada.\r\n");
            
            g_rtc_access_in_progress = false; // <<-- LIBERAR ACCESO
            break;
        }
        
        case 0x30: { // Guardar Secuencia
            if (len != 14) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveSequence(buffer[2], buffer[3], &buffer[4]);
            Scheduler_ReloadCache();
            UART_Send_ACK(cmd);
            break;
        }
        
        case 0x31: { // Leer Secuencia
            // Leer secuencia de movimientos (LEN = 1: sec_index)
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t sec_index = buffer[2];
            uint8_t num_movements; // Cambiado de num_steps
            uint8_t movements_indices[12]; // Cambiado de steps_indices
            EEPROM_ReadSequence(sec_index, &num_movements, movements_indices);
            if(num_movements == 0){
                UART1_SendString("Secuencia no existe o esta vacia\r\n");
            } else {
                //char msg[80];
                sprintf(uart_response_buffer, "Secuencia[%d]: Movimientos: ", sec_index); // Mensaje cambiado
                UART1_SendString(uart_response_buffer);
                for(uint8_t i = 0; i < num_movements; i++){
                    sprintf(uart_response_buffer, "%d ", movements_indices[i]);
                    UART1_SendString(uart_response_buffer);
                }
                UART1_SendString("\r\n");
            }
            break;
        }
        
        case 0x40: { // Guardar Plan
            if(len != 6) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SavePlan(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
            Scheduler_ReloadCache();
            UART_Send_ACK(cmd);
            break;
        }
        
        // NUEVA FUNCIONALIDAD: Leer un Plan (CMD 0x41)
        case 0x41: {
            // Payload esperado: 1 byte: [plan_index]
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t plan_index = buffer[2];

            // <<< PASO 1: Renombrar 'day' a 'id_tipo_dia' y ajustar el orden
            uint8_t id_tipo_dia, sec_index, time_sel, hour, minute;

            // <<< PASO 2: Llamar a la función con el orden correcto de argumentos
            // La firma es: EEPROM_ReadPlan(index, *id_tipo_dia, *sec_index, *time_sel, *hour, *minute)
            EEPROM_ReadPlan(plan_index, &id_tipo_dia, &sec_index, &time_sel, &hour, &minute);
    
            //char msg[100];

            // <<< PASO 3: Actualizar el mensaje para que sea claro y correcto
            sprintf(uart_response_buffer, "Plan[%d]: TipoDia:%d, Sec:%d, Tsel:%d, Hora:%d, Min:%d\r\n", 
                    plan_index, id_tipo_dia, sec_index, time_sel, hour, minute);
            
            UART1_SendString(uart_response_buffer);
            break;
        }
        
        case 0x50: { // Guardar Intermitencia
            if (len != 6) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveIntermittence(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
            UART_Send_ACK(cmd);
            break;
        }

        case 0x51: { // Leer Bloque de Intermitencia
            // Payload: slot_index(1) = 1 byte
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t slot_index = buffer[2];
            uint8_t id_plan, mov_idx, mask_d, mask_e, mask_f;

            EEPROM_ReadIntermittence(slot_index, &id_plan, &mov_idx, &mask_d, &mask_e, &mask_f);

            // Comprobamos si el bloque está vacío (todo 0xFF)
            if (id_plan == 0xFF && mov_idx == 0xFF && mask_d == 0xFF) {
                 UART1_SendString("Bloque de intermitencia no existe o esta vacio\r\n");
            } else {
                //char msg[120];
                sprintf(uart_response_buffer, "Intermitencia[%d]: PlanID:%d MovID:%d MaskD:0x%02X MaskE:0x%02X MaskF:0x%02X\r\n",
                        slot_index, id_plan, mov_idx, mask_d, mask_e, mask_f);
                UART1_SendString(uart_response_buffer);
            }
            break;
        }
        
        case 0x60: { // Guardar Feriado
            if (len != 3) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveHoliday(buffer[2], buffer[3], buffer[4]);
            Scheduler_ReloadCache();
            UART_Send_ACK(cmd);
            break;
        }
        
        case 0x61: { // Leer TODOS los Feriados
            if(len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            
            UART1_SendString("--- Feriados Guardados ---\r\n");
            uint8_t day, month;
            //char msg[32];
            uint8_t encontrados = 0;
            
            for (uint8_t i = 0; i < MAX_HOLIDAYS; i++) {
                EEPROM_ReadHoliday(i, &day, &month);
                // Un slot vacío en la EEPROM usualmente lee 0xFF
                if (day != 0xFF && month != 0xFF && day <= 31 && month <= 12) {
                    sprintf(uart_response_buffer, "Slot[%d]: %02d/%02d\r\n", i, day, month);
                    UART1_SendString(uart_response_buffer);
                    encontrados++;
                }
            }
            
            if (encontrados == 0) {
                UART1_SendString("No hay feriados configurados.\r\n");
            }
            
            sprintf(uart_response_buffer, "Total: %d/%d\r\n", encontrados, MAX_HOLIDAYS);
            UART1_SendString(uart_response_buffer);
            
            break;
        }

        case 0xF0: { // Restaurar a Fábrica
            if (len != 0) { 
                UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); 
                break; 
            }

            // Paso 1: Confirmar INMEDIATAMENTE que se recibió la orden.
            UART_Send_ACK(cmd);
            
            // Paso 2: Ahora sí, realizar las operaciones largas.
            // La GUI ya recibió su confirmación y no dará timeout.
            EEPROM_EraseAll();
            EEPROM_InitStructure();
            Scheduler_ReloadCache();
            
            // Ya no se envía un ACK aquí, se envió al principio.
            
            // --- FIN DE LA CORRECCIÓN ---
            break;
        }
        
        default:
            UART_Send_NACK(cmd, ERROR_UNKNOWN_CMD);
            break;
    }
}
