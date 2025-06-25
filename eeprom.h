//eeprom.h

#ifndef EEPROM_H
#define EEPROM_H

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

// Tamaño total de la EEPROM para el PIC18F8720 (1KB)
#define EEPROM_SIZE 1024

#define VALID_PINS_H 0x1B
#define VALID_PINS_J 0x1E
#define TIPO_DIA_LABORAL 1
#define TIPO_DIA_SABADO 2
#define TIPO_DIA_DOMINGO 3
#define TIPO_DIA_FERIADO 4
#define TIPO_DIA_TODOS 5           // Nuevo: Se aplica a cualquier día.
#define TIPO_DIA_TODOS_NO_FERIADO 6  // Nuevo: Se aplica a cualquier día que no sea feriado.
#define EEPROM_BASE_MOVEMENTS 0x020
#define MOVEMENT_SIZE 10
#define MAX_MOVEMENTS 60
#define EEPROM_BASE_SEQUENCES 0x280
#define SEQUENCE_SIZE 13
#define MAX_SEQUENCES 8
#define EEPROM_BASE_PLANS 0x2F0
#define PLAN_SIZE 5 // CORREGIDO: El tamaño del plan es 6
#define MAX_PLANS 20
#define EEPROM_BASE_INTERMITENCES 0x360
#define INTERMITTENCE_SIZE 5
#define MAX_INTERMITENCES 20
#define EEPROM_BASE_HOLIDAYS 0x3D0
#define HOLIDAY_SIZE 2
#define MAX_HOLIDAYS 20
//#define EEPROM_BASE_WEEKLY_AGENDA 0x3F8
//#define WEEKLY_AGENDA_SIZE 7

void EEPROM_Init(void);
void EEPROM_Write(uint16_t addr, uint8_t data);
uint8_t EEPROM_Read(uint16_t addr);
/**
 * @brief Borra toda la memoria EEPROM, escribiendo 0xFF en cada dirección.
 */
void EEPROM_EraseAll(void);

void EEPROM_InitStructure(void);
void EEPROM_SaveControllerID(uint8_t id);
uint8_t EEPROM_ReadControllerID(void);
void EEPROM_SaveMovement(uint8_t index, uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times);
void EEPROM_ReadMovement(uint8_t index, uint8_t *portD, uint8_t *portE, uint8_t *portF, uint8_t *portH, uint8_t *portJ, uint8_t *times);
bool EEPROM_IsMovementValid(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times);
void EEPROM_SaveSequence(uint8_t sec_index, uint8_t num_movements, uint8_t *movements_indices);
void EEPROM_ReadSequence(uint8_t sec_index, uint8_t *num_movements, uint8_t *movements_indices);
void EEPROM_SavePlan(uint8_t plan_index, uint8_t id_tipo_dia, uint8_t id_secuencia, uint8_t time_sel, uint8_t hour, uint8_t minute);
void EEPROM_ReadPlan(uint8_t plan_index, uint8_t *id_tipo_dia, uint8_t *id_secuencia, uint8_t *time_sel, uint8_t *hour, uint8_t *minute);
void EEPROM_SaveIntermittence(uint8_t index, uint8_t id_plan, uint8_t indice_mov, uint8_t mask_d, uint8_t mask_e, uint8_t mask_f);
void EEPROM_ReadIntermittence(uint8_t index, uint8_t *id_plan, uint8_t *indice_mov, uint8_t *mask_d, uint8_t *mask_e, uint8_t *mask_f);
void EEPROM_SaveHoliday(uint8_t index, uint8_t day, uint8_t month);
void EEPROM_ReadHoliday(uint8_t index, uint8_t *day, uint8_t *month);
//void EEPROM_SaveWeeklyAgenda(uint8_t agenda[7]);
//void EEPROM_ReadWeeklyAgenda(uint8_t agenda[7]);

#endif // EEPROM_H
