#include "weather_countries.h"

const char *const countryLabels[] = {
    "Vietnam (VN)", "United States (US)", "Japan (JP)", "Germany (DE)", "India (IN)",
    "France (FR)", "Canada (CA)", "United Kingdom (GB)", "Australia (AU)", "Brazil (BR)", "Custom"};

const char *const countryCodes[] = {
    "VN", "US", "JP", "DE", "IN", "FR", "CA", "GB", "AU", "BR", ""};

const int countryCount = sizeof(countryLabels) / sizeof(countryLabels[0]);

