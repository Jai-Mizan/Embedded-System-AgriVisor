#pragma once

typedef struct {
    float humidity;
    int soil_moisture;
    int nitrogen;
    int phosphorus;
    int potassium;
} sensor_data_t;

void init_sensors();
void read_sensors(sensor_data_t *data);