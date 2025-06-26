//config.c

#include <xc.h>
#include "config.h"

// Configuraci�n del Oscilador: Cristal de alta velocidad
#pragma config OSC = HS

// Power-up Timer: Habilitado (da tiempo a que la fuente se estabilice)
#pragma config PWRT = ON

// Brown-out Reset: HABILITADO. Nivel de voltaje a 4.2V.
// ESTA ES LA CORRECCI�N M�S IMPORTANTE.
#pragma config BOR = ON
#pragma config BORV = 42

// Watchdog Timer: HABILITADO. Con un postscaler de 1:32768.
// Esto da un tiempo de ~4 segundos antes de un reinicio si el sistema se cuelga.
#pragma config WDT = ON
#pragma config WDTPS = 32768

// Low-Voltage Programming: Deshabilitado
#pragma config LVP = OFF

/*
#pragma config OSC = HS
#pragma config WDT = OFF
#pragma config LVP = OFF
#pragma config PWRT = ON
*/
void PIC_Init(void){
    
    // Configura todos los puertos anal�gicos como digitales
    ADCON1 = 0x0F; 
    
     // 1. Deshabilitar el m�dulo SPI (libera RC3 y RC4)
    SSPCON1bits.SSPEN = 0;

    // 2. Deshabilitar el m�dulo CCP1 (libera RC2)
    CCP1CON = 0x00;

    // 3. (Buena pr�ctica) Establecer la direcci�n inicial del puerto C
    // Los pines del RTC se configurar�n individualmente en rtc.c,
    // pero establecer un estado conocido es seguro.
    TRISC = 0b10010001; // RX1(RC7) y SDA(RC4) como entradas, el resto salidas

    
    // Configuraci�n de puertos de salida
    TRISD = 0x00; 
    TRISE = 0x00; 
    TRISF = 0x00; 
    //TRISH = 0x00;
    // Configura Puerto H: H7,H6,H5 como entradas (1), el resto como salidas (0)
    TRISH = 0b11100100;
    //TRISJ = 0x00; 
    // Configura Puerto J: J7,J6,J5 como entradas (1), el resto como salidas (0)
    TRISJ = 0b11100001;
    
    // LEDs de depuraci�n en Puerto A
    TRISAbits.TRISA1 = 0; 
    TRISAbits.TRISA2 = 0;
    TRISAbits.TRISA3 = 0;
    TRISAbits.TRISA4 = 0;
}