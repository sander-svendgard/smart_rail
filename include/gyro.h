#ifndef GYRO_H
#define GYRO_H

#include <Arduino.h>
#include <Wire.h>

// Dette er et manuelt skrevet bibliotek for ICM-20948 gyroskop-funksjonen.
// ICM-20948 er en 9-akse IMU (akselerometer + gyro + magnetometer).
// Her bruker vi kun gyroskop-delen, som måler rotasjonshastighet i grader/sekund.
// Kommunikasjon skjer via I2C (to ledninger: SDA og SCL).

// I2C-ADRESSE
// ICM-20948 har to mulige I2C-adresser:
//   0x69 = standard (ADR-pinne ikke tilkoblet / koblet til VCC)
//   0x68 = alternativ (ADR-pinne koblet til GND)
#define ICM20948_ADDR       0x69

// REGISTER BANK-VELGER
// ICM-20948 deler registrene sine over 4 banker (0–3).
// For å lese/skrive i riktig bank må vi først sende bankvalget
// til dette registeret. Det finnes i alle banker.
#define REG_BANK_SEL        0x7F

// BANK 0 – Statusregistre og sensordata
#define B0_WHO_AM_I         0x00   // ID-register. Alltid 0xEA for ICM-20948 – brukes til å verifisere at brikken svarer
#define B0_PWR_MGMT_1       0x06   // Strømstyring: sleep, reset, klokkekilde
#define B0_PWR_MGMT_2       0x07   // Skru av/på enkeltakser i akselerometer og gyro
#define B0_GYRO_XOUT_H      0x33   // Gyro X-akse – høybyte (mest signifikante 8 bit)
#define B0_GYRO_XOUT_L      0x34   // Gyro X-akse – lavbyte (minst signifikante 8 bit)
#define B0_GYRO_YOUT_H      0x35   // Gyro Y-akse – høybyte
#define B0_GYRO_YOUT_L      0x36   // Gyro Y-akse – lavbyte
#define B0_GYRO_ZOUT_H      0x37   // Gyro Z-akse – høybyte
#define B0_GYRO_ZOUT_L      0x38   // Gyro Z-akse – lavbyte

// BANK 2 – Gyroskop-konfigurasjon
#define B2_GYRO_SMPLRT_DIV  0x00   // Samplingsrate-divisor: sample rate = 1100 Hz / (1 + verdi)
#define B2_GYRO_CONFIG_1    0x01   // Full-scale range + digital lavpassfilter (DLPF)
#define B2_GYRO_CONFIG_2    0x02   // Ekstra filtrering og decimering

// GYRO FULL-SCALE (måleområde)
// Full-scale bestemmer hvor store vinkelhastigheter sensoren kan måle.
// Lavere range = høyere presisjon (for rolige bevegelser som tog).
// Høyere range = tåler kraftigere rotasjon, men lavere oppløsning.
//
// Sensitivitet (LSB per °/s) viser hvor mange "råtall" som tilsvarer 1 grad/sekund:
typedef enum {
    GYRO_FS_250DPS  = 0,  // ±250 °/s  → 131.0 LSB/(°/s)  – høyest presisjon
    GYRO_FS_500DPS  = 1,  // ±500 °/s  →  65.5 LSB/(°/s)
    GYRO_FS_1000DPS = 2,  // ±1000 °/s →  32.8 LSB/(°/s)
    GYRO_FS_2000DPS = 3   // ±2000 °/s →  16.4 LSB/(°/s)  – lavest presisjon
} GyroFullScale;

// KLASSE: ICM20948Gyro
// Brukes slik i main.cpp:
//   ICM20948Gyro gyro;
//   gyro.init(GYRO_FS_250DPS);
//   float gx, gy, gz;
//   gyro.readGyro(gx, gy, gz);
class ICM20948Gyro {
public:
    // Konstruktør – tar I2C-adressen som parameter (standard 0x69)
    ICM20948Gyro(uint8_t addr = ICM20948_ADDR);

    // Starter Wire (I2C), sjekker WHO_AM_I og konfigurerer gyroskopet.
    // Returnerer true hvis alt gikk bra, false hvis brikken ikke svarte.
    bool init(GyroFullScale fs = GYRO_FS_250DPS);

    // Leser rådata fra sensoren og konverterer til grader/sekund.
    // gx = rotasjon rundt X-aksen, gy = Y-aksen, gz = Z-aksen.
    void readGyro(float &gx, float &gy, float &gz);

private:
    uint8_t _addr;         // I2C-adresse til brikken
    float   _sensitivity;  // Konverteringsfaktor: råverdi / sensitivitet = °/s

    // Velger hvilken registerbank vi skal lese/skrive i (0–3)
    void    selectBank(uint8_t bank);

    // Skriver én byte til et register
    void    writeReg(uint8_t reg, uint8_t value);

    // Leser én byte fra et register
    uint8_t readReg(uint8_t reg);

    // Leser flere bytes sekvensielt fra et startregister (brukes for å lese alle 6 gyro-bytes på én gang)
    void    readBytes(uint8_t reg, uint8_t *buf, uint8_t len);
};

#endif
