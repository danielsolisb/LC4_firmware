//eeprom.c

#include <stdbool.h>
#include "eeprom.h"

// Definiciones de direcciones básicas:
#define EEPROM_SECUENCIAS_ADDR  0x200   // Usado en funciones antiguas, ahora se utiliza EEPROM_BASE_SEQUENCES
#define EEPROM_CONTROLLER_ID_ADDR  0x001 // Dirección para el ID del controlador

// --- Valores de Fábrica para los Puertos (Todos los rojos) ---
#define FACTORY_DEFAULT_PORTD 0x92 // R1, R2, R3
#define FACTORY_DEFAULT_PORTE 0x49 // R4, R5, R6
#define FACTORY_DEFAULT_PORTF 0x24 // R7, R8

// Funciones básicas de lectura/escritura:
void EEPROM_Init(void){
    EECON1 = 0; // Inicializa el módulo EEPROM
}

void EEPROM_Write(uint16_t addr, uint8_t data){
    EEADR = (addr & 0xFF);
    EEADRH = ((addr >> 8) & 0x03);
    EEDATA = data;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS = 0;
    EECON1bits.WREN = 1;
    INTCONbits.GIE = 0;

    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;

    while(EECON1bits.WR);

    EECON1bits.WREN = 0;
    INTCONbits.GIE = 1;
}

uint8_t EEPROM_Read(uint16_t addr){
    EEADR = (addr & 0xFF);
    EEADRH = ((addr >> 8) & 0x03);
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS = 0;
    EECON1bits.RD = 1;
    NOP();
    return EEDATA;
}

void EEPROM_InitStructure(void){
    // 1. Definir los valores de fábrica para el Movimiento 0
    uint8_t default_times[5] = {1, 2, 3, 4, 5};
    // Los puertos peatonales H y J se dejan vacíos (0x00)
    EEPROM_SaveMovement(0, 
                        FACTORY_DEFAULT_PORTD, 
                        FACTORY_DEFAULT_PORTE, 
                        FACTORY_DEFAULT_PORTF, 
                        0x00, 
                        0x00, 
                        default_times);

    // 2. Definir los valores de fábrica para la Secuencia 0
    // La secuencia contiene 1 movimiento: el índice 0.
    // El resto del array se rellena con 0xFF como es estándar.
    uint8_t default_sequence_indices[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EEPROM_SaveSequence(0, 1, default_sequence_indices);
    
    // 3. Escribir la bandera de inicialización al final para marcar el proceso como completado.
    // Esto asegura que esta función no se vuelva a ejecutar en reinicios posteriores.
    EEPROM_Write(0x000, 0xAA);
}

// --- ID del Controlador ---
void EEPROM_SaveControllerID(uint8_t id) {
    EEPROM_Write(0x001, id);
}

uint8_t EEPROM_ReadControllerID(void) {
    return EEPROM_Read(0x001);
}

/**
 * @brief Escribe 0xFF en toda la memoria EEPROM desde la dirección 0x000
 * hasta el final (EEPROM_SIZE - 1).
 * @details Este proceso puede tomar un momento en completarse.
 */
void EEPROM_EraseAll(void) {
    // Recorremos cada una de las direcciones de la memoria EEPROM
    for (uint16_t i = 0; i < EEPROM_SIZE; i++) {
        // Escribimos el valor 0xFF, que representa un byte borrado.
        EEPROM_Write(i, 0xFF);
    }
}

// --- Tabla de Pasos ---
// Cada paso ocupa 8 bytes en la EEPROM:
// Dirección base = EEPROM_BASE_STEPS; cada paso i se ubica en: EEPROM_BASE_STEPS + i*STEP_SIZE
// Estructura del paso:
//  Byte 0: portD
//  Byte 1: portE
//  Byte 2: portF
//  Bytes 3?7: tiempos[0] a tiempos[4]
void EEPROM_SaveMovement(uint8_t index, uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times) {
    uint16_t addr = EEPROM_BASE_MOVEMENTS + (index * MOVEMENT_SIZE);
    EEPROM_Write(addr,     portD);
    EEPROM_Write(addr + 1, portE);
    EEPROM_Write(addr + 2, portF);
    
    // Guardar puertos H y J aplicando las máscaras para forzar a 0 los pines no usados
    EEPROM_Write(addr + 3, portH & VALID_PINS_H);
    EEPROM_Write(addr + 4, portJ & VALID_PINS_J);
    
    // Escribir los 5 tiempos (ahora con un offset de 5)
    for(uint8_t i = 0; i < 5; i++){
        EEPROM_Write(addr + 5 + i, times[i]);
    }
}

void EEPROM_ReadMovement(uint8_t index, uint8_t *portD, uint8_t *portE, uint8_t *portF, uint8_t *portH, uint8_t *portJ, uint8_t *times) {
    uint16_t addr = EEPROM_BASE_MOVEMENTS + (index * MOVEMENT_SIZE);
    *portD = EEPROM_Read(addr);
    *portE = EEPROM_Read(addr + 1);
    *portF = EEPROM_Read(addr + 2);
    *portH = EEPROM_Read(addr + 3);
    *portJ = EEPROM_Read(addr + 4);

    // Leer los 5 tiempos (ahora con un offset de 5)
    for(uint8_t i = 0; i < 5; i++){
        times[i] = EEPROM_Read(addr + 5 + i);
    }
}

bool EEPROM_IsMovementValid(uint8_t portD, uint8_t portE, uint8_t portF, uint8_t portH, uint8_t portJ, uint8_t *times) {
    // Comprueba si los 10 bytes del movimiento están vacíos (0xFF)
    if(portD == 0xFF && portE == 0xFF && portF == 0xFF && portH == 0xFF && portJ == 0xFF){
        bool allTimesFF = true;
        for(uint8_t i = 0; i < 5; i++){
            if(times[i] != 0xFF){
                allTimesFF = false;
                break;
            }
        }
        if(allTimesFF) return false;
    }
    return true;
}

// --- Tabla de Secuencias ---
// Cada secuencia ocupa 13 bytes:
// Byte 0: Número de pasos (n) de la secuencia (máx. 12)
// Bytes 1 a 12: Lista de índices de pasos (si n < 12, se pueden rellenar con 0xFF)
void EEPROM_SaveSequence(uint8_t sec_index, uint8_t num_movements, uint8_t *movements_indices) {
    uint16_t addr = EEPROM_BASE_SEQUENCES + (sec_index * SEQUENCE_SIZE);
    
    // Guardar el número de movimientos
    EEPROM_Write(addr, num_movements);
    
    // --- LÓGICA CORREGIDA ---
    // La GUI ya envía el array de 12 bytes rellenado con 0xFF.
    // Simplemente escribimos los 12 bytes directamente.
    for(uint8_t i = 0; i < 12; i++){
        EEPROM_Write(addr + 1 + i, movements_indices[i]);
    }
}


void EEPROM_ReadSequence(uint8_t sec_index, uint8_t *num_movements, uint8_t *movements_indices) {
    uint16_t addr = EEPROM_BASE_SEQUENCES + (sec_index * SEQUENCE_SIZE);
    *num_movements = EEPROM_Read(addr);
    // Validamos: si num_movements es mayor a 12 o 0xFF, consideramos la secuencia inexistente
    if((*num_movements == 0xFF) || (*num_movements > 12)){
        *num_movements = 0;
        return;
    }
    for(uint8_t i = 0; i < *num_movements; i++){
        movements_indices[i] = EEPROM_Read(addr + 1 + i);
    }
}

// --- Tabla de Planes ---
// Cada plan ocupa 5 bytes, con la siguiente estructura:
//  Byte 0: sec_index (índice de secuencia a usar, 0?4)
//  Byte 1: time_sel (selección de uno de los 5 tiempos, 0?4)
//  Byte 2: day (agrupación o día de ejecución, 1 byte)
//  Byte 3: hour (hora de inicio, 0?23)
//  Byte 4: minute (minuto de inicio, 0?59)
void EEPROM_SavePlan(uint8_t plan_index, uint8_t id_tipo_dia, uint8_t sec_index, uint8_t time_sel, uint8_t hour, uint8_t minute) {
    // La función usa plan_index para calcular la dirección, pero no lo guarda.
    // Solo guarda los 5 bytes de datos del plan.
    uint16_t addr = EEPROM_BASE_PLANS + (plan_index * PLAN_SIZE);
    EEPROM_Write(addr,     id_tipo_dia);
    EEPROM_Write(addr + 1, sec_index);
    EEPROM_Write(addr + 2, time_sel);
    EEPROM_Write(addr + 3, hour);
    EEPROM_Write(addr + 4, minute);
}


void EEPROM_ReadPlan(uint8_t plan_index, uint8_t *id_tipo_dia, uint8_t *sec_index, uint8_t *time_sel, uint8_t *hour, uint8_t *minute) {
    uint16_t addr = EEPROM_BASE_PLANS + (plan_index * PLAN_SIZE);
    *id_tipo_dia = EEPROM_Read(addr);
    *sec_index  = EEPROM_Read(addr + 1);
    *time_sel  = EEPROM_Read(addr + 2);
    *hour      = EEPROM_Read(addr + 3);
    *minute    = EEPROM_Read(addr + 4);
}

void EEPROM_SaveHoliday(uint8_t index, uint8_t day, uint8_t month) {
    if (index >= MAX_HOLIDAYS) return;
    uint16_t addr = EEPROM_BASE_HOLIDAYS + (index * HOLIDAY_SIZE);
    EEPROM_Write(addr, day);
    EEPROM_Write(addr + 1, month);
}

void EEPROM_ReadHoliday(uint8_t index, uint8_t *day, uint8_t *month) {
    if (index >= MAX_HOLIDAYS) return;
    uint16_t addr = EEPROM_BASE_HOLIDAYS + (index * HOLIDAY_SIZE);
    *day = EEPROM_Read(addr);
    *month = EEPROM_Read(addr + 1);
}


// --- NUEVAS FUNCIONES PARA LA TABLA DE INTERMITENCIAS ---

void EEPROM_SaveIntermittence(uint8_t index, uint8_t id_plan, uint8_t indice_mov, uint8_t mask_d, uint8_t mask_e, uint8_t mask_f) {
    if (index >= MAX_INTERMITENCES) return; // Protección contra desbordamiento

    uint16_t addr = EEPROM_BASE_INTERMITENCES + (index * INTERMITTENCE_SIZE);

    EEPROM_Write(addr,     id_plan);
    EEPROM_Write(addr + 1, indice_mov);
    EEPROM_Write(addr + 2, mask_d);
    EEPROM_Write(addr + 3, mask_e);
    EEPROM_Write(addr + 4, mask_f);
}

void EEPROM_ReadIntermittence(uint8_t index, uint8_t *id_plan, uint8_t *indice_mov, uint8_t *mask_d, uint8_t *mask_e, uint8_t *mask_f) {
    if (index >= MAX_INTERMITENCES) { // Protección
        *id_plan = 0xFF; // Devuelve un valor inválido si el índice está fuera de rango
        return;
    }

    uint16_t addr = EEPROM_BASE_INTERMITENCES + (index * INTERMITTENCE_SIZE);

    *id_plan = EEPROM_Read(addr);
    *indice_mov = EEPROM_Read(addr + 1);
    *mask_d = EEPROM_Read(addr + 2);
    *mask_e = EEPROM_Read(addr + 3);
    *mask_f = EEPROM_Read(addr + 4);
}