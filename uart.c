//uart.c
#include <string.h>
#include <stdio.h>
#include "uart.h"
#include "config.h"
#include "eeprom.h"
#include "rtc.h"
#include "scheduler.h"
#include "sequence_engine.h"

// --- Buffers para comunicación asíncrona ---
#define UART_TX_BUFFER_SIZE 128
#define UART_RX_BUFFER_SIZE 64

static volatile uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint8_t tx_head = 0;
static volatile uint8_t tx_tail = 0;

static volatile uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t uart_rx_index = 0;
static volatile bool g_frame_received = false;

static volatile uint8_t uart2_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t uart2_rx_index = 0;
static volatile bool g_uart2_frame_received = false;
static volatile bool g_uart2_processing_lock = false;

// Prototipo de la función que construye y envía una trama
static void UART_Send_Frame(uint8_t cmd, uint8_t* payload, uint8_t len);

// Prototipo de la función que construye y envía una trama
static void UART_Send_Frame(uint8_t cmd, uint8_t* payload, uint8_t len);

#define UART_RESPONSE_BUFFER_SIZE 128
//static char uart_response_buffer[UART_RESPONSE_BUFFER_SIZE];

static void UART_HandleCompleteFrame(uint8_t *buffer, uint8_t length);

static void UART2_HandleCompleteFrame(uint8_t *buffer, uint8_t length);

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

void UART2_Init(uint32_t baudrate) {
    // Pines de UART2 en PIC18F8720: RG1 (TX2) y RG2 (RX2)
    TRISGbits.TRISG1 = 0; // TX2 como salida
    TRISGbits.TRISG2 = 1; // RX2 como entrada
    
    TXSTA2 = 0x24;        // TXEN=1 (Transmit enable), BRGH=1 (High speed)
    RCSTA2 = 0x90;        // SPEN=1 (Serial port enable), CREN=1 (Continuous receive)
    
    // Cálculo de Baud Rate (igual que UART1 para 9600 baud @ 20MHz)
    // (20,000,000 / (16 * 9600)) - 1 = 129.2
    SPBRG2 = 129; 
    
    PIE3bits.RC2IE = 1; // Habilitar interrupción de recepción de UART2
    IPR3bits.RC2IP = 1; // Asignar alta prioridad
}


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

//funci+on para monitoreo y reporte
void UART_Send_Monitoring_Report(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ) {
    uint8_t payload[5];
    
    // Construir el byte de estado peatonal combinando los 4 bits inferiores de H y J
    uint8_t pedestrian_status = (portH & 0x0F) | ((portJ & 0x0F) << 4);

    payload[0] = EEPROM_ReadControllerID();
    payload[1] = portD;
    payload[2] = portE;
    payload[3] = portF;
    payload[4] = pedestrian_status;
    
    UART_Send_Frame(CMD_MONITOR_STATUS_REPORT, payload, 5);
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

void UART2_Task(void) {
    if (!g_uart2_frame_received) {
        return;
    }

    // Ponemos el cerrojo para que la ISR no modifique el buffer de UART2.
    g_uart2_processing_lock = true;

    // Procesamos la trama directamente desde el buffer global de UART2.
    UART2_HandleCompleteFrame((uint8_t*)uart2_rx_buffer, uart2_rx_index);

    // Reseteamos la bandera de trama recibida.
    g_uart2_frame_received = false;

    // Quitamos el cerrojo para permitir que la ISR reciba nuevas tramas.
    g_uart2_processing_lock = false;
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


/**
 * @brief Procesa un byte de UART2 (llamada desde la ISR).
 * @details Esta es una copia de la lógica de UART1, pero usa las variables
 * y banderas de UART2.
 */
void UART2_ProcessReceivedByte(uint8_t byte) {
    // Si el bucle principal (UART2_Task) está procesando, ignoramos.
    if (g_uart2_processing_lock) {
        return;
    }

    // Lógica de recepción para la MMU
    // (Asumimos el mismo formato de trama por ahora, esto se puede cambiar)
    static uint8_t stx_sequence[3] = {0x43, 0x53, 0x4F}; // <--- ¿Usa MMU el mismo STX?
    static uint8_t stx_counter = 0;
    static bool receiving_in_progress = false;

    if (!receiving_in_progress) {
        if (byte == stx_sequence[stx_counter]) {
            if (++stx_counter >= 3) {
                receiving_in_progress = true;
                uart2_rx_index = 0; // Usa buffer UART2
                stx_counter = 0;
            }
        } else {
            stx_counter = 0;
        }
    } else {
        if (uart2_rx_index < UART_RX_BUFFER_SIZE) {
            uart2_rx_buffer[uart2_rx_index++] = byte; // Usa buffer UART2
            if (uart2_rx_index >= 2) {
                uint8_t payload_len = uart2_rx_buffer[1];
                uint16_t total_expected_bytes = payload_len + 5;
                if (uart2_rx_index >= total_expected_bytes) {
                    if (uart2_rx_buffer[uart2_rx_index - 2] == 0x03 && uart2_rx_buffer[uart2_rx_index - 1] == 0xFF) {
                        g_uart2_frame_received = true; // <--- Bandera de UART2
                        uart2_rx_index -= 2;
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
            
            // Se construye el payload con el dato leído
            uint8_t payload[1];
            payload[0] = id;
            
            // Se envía la trama de respuesta binaria, no texto.
            UART_Send_Frame(RESP_CONTROLLER_ID, payload, 1);
            break;
        }

        // --- Comandos de RTC (requieren bloqueo con semáforo) ---
        case 0x21: { // Consultar Hora
            if(len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            
            g_rtc_access_in_progress = true;
            RTC_Time rtc;
            RTC_GetTime(&rtc);
            g_rtc_access_in_progress = false;
            
            // El payload contiene los 7 bytes de la estructura RTC_Time
            uint8_t payload[7];
            payload[0] = rtc.hour;
            payload[1] = rtc.minute;
            payload[2] = rtc.second;
            payload[3] = rtc.day;
            payload[4] = rtc.month;
            payload[5] = rtc.year;
            payload[6] = rtc.dayOfWeek;

            UART_Send_Frame(RESP_RTC_TIME, payload, 7);
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
            
            // --- INICIO DE LA CORRECCIÓN ---
            // 1. Confirmar INMEDIATAMENTE que se recibió el comando.
            UART_Send_ACK(cmd);
            
            // 2. Ahora, ejecutar la operación de guardado.
            EEPROM_SaveMovement(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], &buffer[8]);
            // --- FIN DE LA CORRECCIÓN ---

            // El ACK ya se envió, por lo que la siguiente línea se elimina o comenta.
            // UART_Send_ACK(cmd); 
            break;
        }

        
        case 0x24: { // Leer Movimiento
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t index = buffer[2];
            uint8_t portD, portE, portF, portH, portJ; 
            uint8_t times[5];
    
            EEPROM_ReadMovement(index, &portD, &portE, &portF, &portH, &portJ, times);
    
            // Se comprueba si el movimiento es válido. Si no lo es, se envía un NACK.
            if(!EEPROM_IsMovementValid(portD, portE, portF, portH, portJ, times)){
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                // Si es válido, se construye el payload con los datos binarios.
                uint8_t payload[11];
                payload[0] = index; // Se devuelve el índice para confirmación
                payload[1] = portD;
                payload[2] = portE;
                payload[3] = portF;
                payload[4] = portH;
                payload[5] = portJ;
                for(uint8_t i = 0; i < 5; i++) payload[6+i] = times[i];
                
                UART_Send_Frame(RESP_MOVEMENT_DATA, payload, 11);
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
            // Payload: 1(idx) + 1(tipo) + 1(pos_ancla) + 1(num_mov) + 12(índices) = 16 bytes
            if (len != 16) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            
            // buffer[4] ahora es la POSICIÓN del ancla (0-11)
            EEPROM_SaveSequence(buffer[2], buffer[3], buffer[4], buffer[5], &buffer[6]);
            Scheduler_ReloadCache();
            UART_Send_ACK(cmd);
            break;
        }
        
        case 0x31: { // Leer Secuencia
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t sec_index = buffer[2];
            uint8_t type, anchor_step_index, num_movements;
            uint8_t movements_indices[12];

            EEPROM_ReadSequence(sec_index, &type, &anchor_step_index, &num_movements, movements_indices);

            // Si no hay movimientos, se considera dato inválido y se envía NACK.
            if(num_movements == 0){
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                // Se construye un payload de tamaño fijo (16 bytes).
                uint8_t payload[16];
                payload[0] = sec_index;
                payload[1] = type;
                payload[2] = anchor_step_index;
                payload[3] = num_movements;
                for(uint8_t i = 0; i < 12; i++){
                    // Se rellenan los índices usados y el resto con 0xFF.
                    payload[4+i] = (i < num_movements) ? movements_indices[i] : 0xFF;
                }
                
                UART_Send_Frame(RESP_SEQUENCE_DATA, payload, 16);
            }
            break;
        }
        
        case 0x40: { // Guardar Plan
            if(len != 6) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            
            // --- INICIO DE LA CORRECCIÓN ---
            // 1. Confirmar INMEDIATAMENTE que se recibió el comando.
            UART_Send_ACK(cmd);

            // 2. Ejecutar la operación de guardado y recarga de caché.
            EEPROM_SavePlan(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
            Scheduler_ReloadCache();
            // --- FIN DE LA CORRECCIÓN ---
            
            // El ACK ya se envió, por lo que la siguiente línea se elimina o comenta.
            // UART_Send_ACK(cmd);
            break;
        }
        
        case 0x41: { // Leer Plan
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t plan_index = buffer[2];
            uint8_t id_tipo_dia, sec_index, time_sel, hour, minute;

            EEPROM_ReadPlan(plan_index, &id_tipo_dia, &sec_index, &time_sel, &hour, &minute);
            
            // 0xFF es el valor por defecto de una EEPROM borrada.
            if (id_tipo_dia == 0xFF) {
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                uint8_t payload[6];
                payload[0] = plan_index;
                payload[1] = id_tipo_dia;
                payload[2] = sec_index;
                payload[3] = time_sel;
                payload[4] = hour;
                payload[5] = minute;
                UART_Send_Frame(RESP_PLAN_DATA, payload, 6);
            }
            break;
        }
        
        case 0x50: { // Guardar Intermitencia
            if (len != 6) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveIntermittence(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
            UART_Send_ACK(cmd);
            break;
        }

        case 0x51: { // Leer Bloque de Intermitencia
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t slot_index = buffer[2];
            uint8_t id_plan, mov_idx, mask_d, mask_e, mask_f;

            EEPROM_ReadIntermittence(slot_index, &id_plan, &mov_idx, &mask_d, &mask_e, &mask_f);

            if (id_plan == 0xFF) {
                 UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                uint8_t payload[6];
                payload[0] = slot_index;
                payload[1] = id_plan;
                payload[2] = mov_idx;
                payload[3] = mask_d;
                payload[4] = mask_e;
                payload[5] = mask_f;
                UART_Send_Frame(RESP_INTERMIT_DATA, payload, 6);
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
        
        case 0x61: { // Leer UN Feriado
            if(len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t index = buffer[2];
            
            // Para leer todos, el software deberá iterar desde el índice 0 hasta MAX_HOLIDAYS-1.
            if (index >= MAX_HOLIDAYS) {
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
                break;
            }
            
            uint8_t day, month;
            EEPROM_ReadHoliday(index, &day, &month);
            
            if (day == 0xFF || month == 0xFF) {
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                uint8_t payload[3];
                payload[0] = index;
                payload[1] = day;
                payload[2] = month;
                UART_Send_Frame(RESP_HOLIDAY_DATA, payload, 3);
            }
            break;
        }
        
        case 0x70: { // Guardar Regla de Flujo
            // Payload: 1(rule_idx) + 1(sec_idx) + 1(orig_mov) + 1(type) + 1(mask) + 1(dest_mov) = 6 bytes
            if (len != 6) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            EEPROM_SaveFlowRule(buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
            UART_Send_ACK(cmd);
            break;
        }

        case 0x71: { // Leer Regla de Flujo
            if (len != 1) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            uint8_t rule_index = buffer[2];
            uint8_t sec_idx, orig_mov, rule_type, mask, dest_mov;

            EEPROM_ReadFlowRule(rule_index, &sec_idx, &orig_mov, &rule_type, &mask, &dest_mov);

            if (sec_idx == 0xFF) {
                UART_Send_NACK(cmd, ERROR_INVALID_DATA);
            } else {
                uint8_t payload[6];
                payload[0] = rule_index;
                payload[1] = sec_idx;
                payload[2] = orig_mov;
                payload[3] = rule_type;
                payload[4] = mask;
                payload[5] = dest_mov;
                UART_Send_Frame(RESP_FLOW_RULE_DATA, payload, 6);
            }
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
            
            Sequence_Engine_EnterFallback();
            
            // --- FIN DE LA CORRECCIÓN ---
            break;
        }
        
        case 0x80: { // 0x80
            if (len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            g_monitoring_active = true;
            UART_Send_ACK(cmd);
            break;
        }

        case 0x81: { // 0x81
            if (len != 0) { UART_Send_NACK(cmd, ERROR_INVALID_LENGTH); break; }
            g_monitoring_active = false;
            UART_Send_ACK(cmd);
            break;
        }
        
        default:
            UART_Send_NACK(cmd, ERROR_UNKNOWN_CMD);
            break;
    }
}


static void UART2_HandleCompleteFrame(uint8_t *buffer, uint8_t length) {
    uint8_t cmd = buffer[0];
    uint8_t len = buffer[1];
    
    // (Podemos añadir validación de checksum si es necesario)
    
    char debug_msg[64];
    sprintf(debug_msg, "UART2: Trama recibida! CMD=0x%02X, LEN=%d\r\n", cmd, len);
    UART1_SendString(debug_msg);

    // Futura implementación:
    // switch(cmd) {
    //     case CMD_MMU_STATUS:
    //         // ...
    //         break;
    //     case CMD_MMU_ERROR:
    //         // ...
    //         break;
    // }
}