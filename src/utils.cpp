#include <Arduino.h>
#include "utils.h"
#include "math.h"
#include "display.h"

int customRoundString(const char *str) {
    double x = atof(str); // or use strtod
    double fractional = x - floor(x);
    if (fractional < 0.5)
        return (int)floor(x);
    else
        return (int)ceil(x);
}


