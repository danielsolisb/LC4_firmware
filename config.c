//config.c

#include <xc.h>
#include "config.h"

#pragma config OSC = HS
#pragma config WDT = OFF
#pragma config LVP = OFF
#pragma config PWRT = ON
void PIC_Init(void){
    
    // Configura todos los puertos analógicos como digitales
    ADCON1 = 0x0F; 
    
     // 1. Deshabilitar el módulo SPI (libera RC3 y RC4)
    SSPCON1bits.SSPEN = 0;

    // 2. Deshabilitar el módulo CCP1 (libera RC2)
    CCP1CON = 0x00;

    // 3. (Buena práctica) Establecer la dirección inicial del puerto C
    // Los pines del RTC se configurarán individualmente en rtc.c,
    // pero establecer un estado conocido es seguro.
    TRISC = 0b10010001; // RX1(RC7) y SDA(RC4) como entradas, el resto salidas

    
    // Configuración de puertos de salida
    TRISD = 0x00; 
    TRISE = 0x00; 
    TRISF = 0x00; 
    TRISH = 0x00;
    TRISJ = 0x00; 
    
    // LEDs de depuración en Puerto A
    TRISAbits.TRISA1 = 0; 
    TRISAbits.TRISA2 = 0;
    TRISAbits.TRISA3 = 0;
    TRISAbits.TRISA4 = 0;
}