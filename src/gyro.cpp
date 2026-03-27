#include "gyro.h"

// Konstruktør: lagrer I2C-adressen og setter standard sensitivitet (131 = ±250 dps)
ICM20948Gyro::ICM20948Gyro(uint8_t addr)
    : _addr(addr), _sensitivity(131.0f) {}

// init() – Starter og konfigurerer gyroskopet
bool ICM20948Gyro::init(GyroFullScale fs) {
    // Start I2C-kommunikasjon (bruker standard SDA/SCL-pinner på ESP32)
    Wire.begin();

    // Bytt til Bank 0 og les WHO_AM_I-registeret for å bekrefte at brikken svarer.
    // ICM-20948 skal alltid returnere 0xEA. Hvis ikke, er noe galt med koblingen.
    selectBank(0);
    uint8_t whoAmI = readReg(B0_WHO_AM_I);
    if (whoAmI != 0xEA) {
        Serial.print("ICM-20948 ikke funnet. WHO_AM_I = 0x");
        Serial.println(whoAmI, HEX);
        return false;
    }

    // ICM-20948 starter i sleep-modus etter oppstart.
    // PWR_MGMT_1 = 0x01 → våkn opp og velg auto klokkekilde (anbefalt av databladet).
    writeReg(B0_PWR_MGMT_1, 0x01);
    delay(30); // Vent på at klokken stabiliserer seg

    // PWR_MGMT_2 = 0x00 → aktiver alle tre gyroakser (X, Y, Z).
    // Setter vi denne til 0x07 ville alle gyroaksene blitt deaktivert.
    writeReg(B0_PWR_MGMT_2, 0x00);

    // Bytt til Bank 2 for å konfigurere gyroskopet
    selectBank(2);

    // GYRO_SMPLRT_DIV setter hvor ofte sensoren tar en ny måling.
    // Formel: sample rate = 1100 Hz / (1 + divisor)
    // Med divisor = 10 → 1100 / 11 ≈ 100 Hz (én ny måling hvert 10. ms)
    writeReg(B2_GYRO_SMPLRT_DIV, 10);

    // GYRO_CONFIG_1 konfigurerer to ting:
    //   - Full-scale range (bits [5:3]): bestemmer måleområde (±250 / 500 / 1000 / 2000 °/s)
    //   - Digital lavpassfilter DLPF (bit [2] = FCHOICE=1 aktiverer filteret, bits [1:0] = DLPFCFG=3)
    // DLPFCFG=3 gir ca. 120 Hz båndbredde – fjerner høyfrekvent støy uten å forsinke signalet for mye.
    // Bitmønster: [fs << 3] plasserer full-scale i riktige bits, 0x07 setter FCHOICE + DLPFCFG.
    uint8_t config1 = (uint8_t)((fs << 3) | 0x07);
    writeReg(B2_GYRO_CONFIG_1, config1);

    // GYRO_CONFIG_2 = 0x00 → ingen ekstra decimering eller selvtest
    writeReg(B2_GYRO_CONFIG_2, 0x00);

    // Gå tilbake til Bank 0 (der sensordata-registrene ligger)
    selectBank(0);

    // Lagre riktig sensitivitetsfaktor basert på valgt full-scale.
    // rawVerdi / sensitivitet = vinkelhastighet i °/s
    switch (fs) {
        case GYRO_FS_250DPS:  _sensitivity = 131.0f; break;
        case GYRO_FS_500DPS:  _sensitivity = 65.5f;  break;
        case GYRO_FS_1000DPS: _sensitivity = 32.8f;  break;
        case GYRO_FS_2000DPS: _sensitivity = 16.4f;  break;
    }

    Serial.println("ICM-20948 gyro initialisert.");
    return true;
}

// readGyro() – Leser vinkelhastighet på alle tre akser
void ICM20948Gyro::readGyro(float &gx, float &gy, float &gz) {
    // Les 6 bytes sekvensielt fra GYRO_XOUT_H (registrene ligger i rekkefølge):
    //   buf[0..1] = X, buf[2..3] = Y, buf[4..5] = Z
    // Hver akse er 16-bit (2 bytes): høybyte først, lavbyte etterpå.
    uint8_t buf[6];
    readBytes(B0_GYRO_XOUT_H, buf, 6);

    // Slå sammen høybyte og lavbyte til et 16-bit fortegnet heltall (int16_t).
    // << 8 betyr "flytt høybyten 8 plasser til venstre" for å få riktig verdi.
    int16_t rawX = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t rawY = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t rawZ = (int16_t)((buf[4] << 8) | buf[5]);

    // Del råverdiene på sensitivitetsfaktoren for å få °/s
    gx = rawX / _sensitivity;
    gy = rawY / _sensitivity;
    gz = rawZ / _sensitivity;
}

// Private hjelpefunksjoner for I2C-kommunikasjon

// selectBank() – Bytter aktiv registerbank (0–3).
// ICM-20948 har 4 banker fordi den har for mange registre til én enkelt adresserom.
// Banken velges ved å sende en verdi til REG_BANK_SEL (0x7F).
// Bank-bitsene sitter på plass [5:4], så vi shifter (bank & 0x03) << 4.
void ICM20948Gyro::selectBank(uint8_t bank) {
    Wire.beginTransmission(_addr);
    Wire.write(REG_BANK_SEL);
    Wire.write((bank & 0x03) << 4);
    Wire.endTransmission();
}

// writeReg() – Skriver én byte til et register via I2C.
// I2C-protokoll: [START] → [adresse+W] → [register] → [verdi] → [STOP]
void ICM20948Gyro::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// readReg() – Leser én byte fra et register via I2C.
// endTransmission(false) = hold I2C-linjen oppe (repeated start) før vi spør om data.
uint8_t ICM20948Gyro::readReg(uint8_t reg) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false); // Repeated start – ikke slipp linjen
    Wire.requestFrom(_addr, (uint8_t)1);
    return Wire.read();
}

// readBytes() – Leser flere bytes sekvensielt fra et startregister.
// ICM-20948 øker registeradressen automatisk for hver byte vi leser (auto-increment),
// så vi kan lese alle 6 gyrobytes i én operasjon fremfor 6 separate kall.
void ICM20948Gyro::readBytes(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.endTransmission(false); // Repeated start
    Wire.requestFrom(_addr, len);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
}
