//Header fil for ultrasonic sensorer og filtering av data :) 

#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <Arduino.h>

class Ultrasonic {
    public:
        Ultrasonic(int trigpin, int echopin);

        void init();
        float measureDistance(); 

    private:
        int echopin;
        int trigpin; 
        const float SOUND_SPEED = 0.034; 
};

void filtrering();  //Filterer data fra x sensorer rundt banen

#endif

