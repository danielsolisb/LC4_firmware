//eeprom.h

#ifndef EEPROM_H
#define EEPROM_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

// Tamaño total de la EEPROM para el PIC18F8720 (1KB)
#define EEPROM_SIZE 1024

// --- TIPOS DE SECUENCIA ---
#define SEQUENCE_TYPE_AUTOMATIC 0x00
#define SEQUENCE_TYPE_DEMAND    0x01

// --- TIPOS DE REGLA PARA CONTROL DE FLUJO ---
#define RULE_TYPE_GOTO            0x00 // Salto incondicional
#define RULE_TYPE_DECISION_POINT  0x01 // Salto condicional (Punto de Decisión)


// =============================================================================
// --- MAPA DE MEMORIA EEPROM (REVISADO CON MOVIMIENTO ANCLA) ---
// =============================================================================
#define VALID_PINS_H 0x1B
#define VALID_PINS_J 0x1E

// Tabla de Movimientos (60 máx)
#define EEPROM_BASE_MOVEMENTS 0x020
#define MOVEMENT_SIZE 10
#define MAX_MOVEMENTS 60

// Tabla de Secuencias (8 máx)
#define EEPROM_BASE_SEQUENCES 0x280
#define SEQUENCE_SIZE 15  // AUMENTADO: 1(tipo)+1(ancla)+1(num_mov)+12(índices)
#define MAX_SEQUENCES 8

// Tabla de Planes (20 máx)
#define EEPROM_BASE_PLANS 0x2F8 // AJUSTADO
#define PLAN_SIZE 5
#define MAX_PLANS 20

// Tabla de Intermitencias (10 máx)
#define EEPROM_BASE_INTERMITENCES 0x360 // AJUSTADO
#define INTERMITTENCE_SIZE 5
#define MAX_INTERMITENCES 10

// Tabla de Feriados (20 máx)
#define EEPROM_BASE_HOLIDAYS 0x392 // AJUSTADO
#define HOLIDAY_SIZE 2
#define MAX_HOLIDAYS 20

// --- NUEVA TABLA DE CONTROL DE FLUJO ---
#define EEPROM_BASE_FLOW_CONTROL  0x3BC
#define FLOW_CONTROL_RULE_SIZE    6
#define MAX_FLOW_CONTROL_RULES    10

// --- MAPA DE MÁSCARAS DE SALIDA --- 
// (Ubicado en el espacio libre de 2 bytes)
#define EEPROM_MASK_VEHICULAR_ADDR 0x3BA
#define EEPROM_MASK_PEDONAL_ADDR   0x3BB

// =============================================================================
// --- PROTOTIPOS DE FUNCIONES (REVISADOS) ---
// =============================================================================

void EEPROM_Init(void);
void EEPROM_Write(uint16_t addr, uint8_t data);
uint8_t EEPROM_Read(uint16_t addr);
void EEPROM_EraseAll(void);
void EEPROM_InitStructure(void);

void EEPROM_SaveControllerID(uint8_t id);
uint8_t EEPROM_ReadControllerID(void);

void EEPROM_SaveMovement(uint8_t index, uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times);
void EEPROM_ReadMovement(uint8_t index, uint8_t *portD, uint8_t *portE, uint8_t *portF, uint8_t *portH, uint8_t *portJ, uint8_t *times);
bool EEPROM_IsMovementValid(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times);

// MODIFICADAS: Añadido 'type' y 'anchor_mov_index'
void EEPROM_SaveSequence(uint8_t sec_index, uint8_t type, uint8_t anchor_step_index, uint8_t num_movements, uint8_t *movements_indices);
void EEPROM_ReadSequence(uint8_t sec_index, uint8_t *type, uint8_t *anchor_step_index, uint8_t *num_movements, uint8_t *movements_indices);

void EEPROM_SavePlan(uint8_t plan_index, uint8_t id_tipo_dia, uint8_t sec_index, uint8_t time_sel, uint8_t hour, uint8_t minute);
void EEPROM_ReadPlan(uint8_t plan_index, uint8_t *id_tipo_dia, uint8_t *sec_index, uint8_t *time_sel, uint8_t *hour, uint8_t *minute);

void EEPROM_SaveIntermittence(uint8_t index, uint8_t id_plan, uint8_t indice_mov, uint8_t mask_d, uint8_t mask_e, uint8_t mask_f);
void EEPROM_ReadIntermittence(uint8_t index, uint8_t *id_plan, uint8_t *indice_mov, uint8_t *mask_d, uint8_t *mask_e, uint8_t *mask_f);

void EEPROM_SaveHoliday(uint8_t index, uint8_t day, uint8_t month);
void EEPROM_ReadHoliday(uint8_t index, uint8_t *day, uint8_t *month);

// --- NUEVAS FUNCIONES PARA CONTROL DE FLUJO ---
void EEPROM_SaveFlowRule(uint8_t rule_index, uint8_t sec_index, uint8_t origin_mov_index, uint8_t rule_type, uint8_t demand_mask, uint8_t dest_mov_index);
void EEPROM_ReadFlowRule(uint8_t rule_index, uint8_t *sec_index, uint8_t *origin_mov_index, uint8_t *rule_type, uint8_t *demand_mask, uint8_t *dest_mov_index);

// FUNCIONES DE MÁSCARAS DE SALIDA --- 
/**
 * @brief Guarda las máscaras de habilitación de salidas vehiculares y peatonales.
 * @param mask_veh Byte de máscara para salidas vehiculares (G1-G8).
 * @param mask_ped Byte de máscara para salidas peatonales (P1-P8).
 */
void EEPROM_SaveOutputMasks(uint8_t mask_veh, uint8_t mask_ped);

/**
 * @brief Lee las máscaras de habilitación de salidas vehiculares y peatonales.
 * @param mask_veh Puntero para almacenar la máscara vehicular.
 * @param mask_ped Puntero para almacenar la máscara peatonal.
 */
void EEPROM_ReadOutputMasks(uint8_t *mask_veh, uint8_t *mask_ped);


#endif // EEPROM_H
