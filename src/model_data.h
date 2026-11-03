#pragma once

// Person detection model (96x96 grayscale int8 in, [no_person, person] int8 out).
extern const unsigned char g_model_data[];
extern const unsigned int g_model_data_len;
