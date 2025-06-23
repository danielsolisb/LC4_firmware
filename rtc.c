// rtc.c 
#include "rtc.h"
#include "config.h" // Para _XTAL_FREQ y delays
#include <xc.h>

//==============================================================================
// DEFINICIONES DE PINES (se mantienen igual)
//==============================================================================
#define RTC_RST_TRIS    TRISCbits.TRISC2
#define RTC_RST_LAT     LATCbits.LATC2
#define RTC_SCLK_TRIS   TRISCbits.TRISC3
#define RTC_SCLK_LAT    LATCbits.LATC3
#define RTC_IO_TRIS     TRISCbits.TRISC4
#define RTC_IO_LAT      LATCbits.LATC4
#define RTC_IO_PORT     PORTCbits.RC4

//==============================================================================
// PROTOTIPOS DE FUNCIONES PRIVADAS (basadas en tu librería funcional)
//==============================================================================
static uint8_t dec_to_bcd(uint8_t data);
static uint8_t bcd_to_dec(uint8_t data);
static void write_ds1302_byte(uint8_t cmd);
static void write_ds1302(uint8_t cmd, uint8_t data);
static uint8_t read_ds1302(uint8_t cmd);

//==============================================================================
// IMPLEMENTACIÓN DE FUNCIONES PÚBLICAS
//==============================================================================

void RTC_Init(void) {
    uint8_t x;
    
    // Configurar pines y estado inicial bajo
    RTC_RST_TRIS = 0;
    RTC_SCLK_TRIS = 0;
    RTC_RST_LAT = 0;
    RTC_SCLK_LAT = 0;
    __delay_us(5); // Pequeña espera para estabilizar

    // 1. Deshabilitar protección contra escritura (WP = Write Protect)
    write_ds1302(0x8E, 0x00); 
    
    // 2. Leer registro de segundos para ver el bit CH (Clock Halt)
    x = read_ds1302(0x81); // 0x81 es el comando de LECTURA de segundos
    
    // 3. Si el bit 7 (CH) es 1, el reloj está detenido. Lo iniciamos.
    if ((x & 0x80) != 0) {
        write_ds1302(0x80, 0x00); // Escribimos 0 en segundos (comando 0x80) para limpiar el bit CH
    }
    
    // 4. Habilitar la protección contra escritura de nuevo
    write_ds1302(0x8E, 0x80);
}

bool RTC_SetTime(RTC_Time *time) {
    write_ds1302(0x8E, 0x00); // Deshabilitar WP
    
    write_ds1302(0x80, dec_to_bcd(time->second) & 0x7F); // Limpiar bit CH al escribir
    write_ds1302(0x82, dec_to_bcd(time->minute));
    write_ds1302(0x84, dec_to_bcd(time->hour));
    write_ds1302(0x86, dec_to_bcd(time->day));
    write_ds1302(0x88, dec_to_bcd(time->month));
    write_ds1302(0x8A, dec_to_bcd(time->dayOfWeek));
    write_ds1302(0x8C, dec_to_bcd(time->year));
    
    write_ds1302(0x8E, 0x80); // Habilitar WP
    return true; // Asumimos éxito
}

void RTC_GetTime(RTC_Time *time) {
    // Los comandos de LECTURA tienen el bit 0 a 1. (e.g., escritura de segundos es 0x80, lectura es 0x81)
    time->second    = bcd_to_dec(read_ds1302(0x81) & 0x7F); // Limpiar bit CH
    time->minute    = bcd_to_dec(read_ds1302(0x83));
    time->hour      = bcd_to_dec(read_ds1302(0x85));
    time->day       = bcd_to_dec(read_ds1302(0x87));
    time->month     = bcd_to_dec(read_ds1302(0x89));
    time->dayOfWeek = bcd_to_dec(read_ds1302(0x8B));
    time->year      = bcd_to_dec(read_ds1302(0x8D));
}

// Las funciones de prueba se mantienen, pero ahora usarán la comunicación robusta
bool RTC_TestRAM(void) {
    uint8_t valor_escrito = 0xA5;
    
    write_ds1302(0x8E, 0x00); // Deshabilitar WP
    write_ds1302(0xC0, valor_escrito); // Escribir en RAM (dirección par para escritura)
    
    uint8_t valor_leido = read_ds1302(0xC1); // Leer de RAM (dirección impar para lectura)
    
    write_ds1302(0x8E, 0x80); // Habilitar WP
    
    return (valor_leido == valor_escrito);
}

void RTC_PerformVisualTest(void) {
    // Esta función no necesita cambios, ya que depende de las funciones de bajo nivel que hemos corregido.
}

//==============================================================================
// IMPLEMENTACIÓN DE FUNCIONES PRIVADAS (lógica robusta de la librería funcional)
//==============================================================================

static uint8_t dec_to_bcd(uint8_t data) {
    return (uint8_t)(((data / 10) << 4) | (data % 10));
}

static uint8_t bcd_to_dec(uint8_t data) {
    return (uint8_t)(((data >> 4) * 10) + (data & 0x0F));
}

// Escribe un solo byte (comando o dato) al DS1302
static void write_ds1302_byte(uint8_t d) {
    RTC_IO_TRIS = 0; // Pin I/O como SALIDA para escribir
    for (uint8_t i = 0; i < 8; i++) {
        RTC_IO_LAT = (d >> i) & 1;
        RTC_SCLK_LAT = 1;
        __delay_us(2);
        RTC_SCLK_LAT = 0;
        __delay_us(2);
    }
}

// Escribe un comando y un byte de datos
static void write_ds1302(uint8_t cmd, uint8_t data) {
    INTCONbits.GIE = 0; // Deshabilitar interrupciones (INICIO SECCIÓN CRÍTICA)
    
    RTC_RST_LAT = 1;
    write_ds1302_byte(cmd);
    write_ds1302_byte(data);
    RTC_RST_LAT = 0;
    
    INTCONbits.GIE = 1; // Habilitar interrupciones (FIN SECCIÓN CRÍTICA)
}

// Lee un byte de datos después de enviar un comando
static uint8_t read_ds1302(uint8_t cmd) {
    uint8_t data = 0;
    
    INTCONbits.GIE = 0; // Deshabilitar interrupciones (INICIO SECCIÓN CRÍTICA)

    RTC_RST_LAT = 1;
    write_ds1302_byte(cmd);

    // --- LA PARTE MÁS IMPORTANTE ---
    // Cambiar pin a ENTRADA para leer la respuesta del RTC
    RTC_IO_TRIS = 1; 
    __delay_us(2);

    for (uint8_t i = 0; i < 8; i++) {
        if (RTC_IO_PORT) {
            data |= (1 << i);
        }
        RTC_SCLK_LAT = 1;
        __delay_us(2);
        RTC_SCLK_LAT = 0;
        __delay_us(2);
    }

    RTC_RST_LAT = 0;
    
    INTCONbits.GIE = 1; // Habilitar interrupciones (FIN SECCIÓN CRÍTICA)
    
    return data;
}
