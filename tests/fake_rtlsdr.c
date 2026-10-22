/* Hardware adapter test double. Never shipped beside the receiver executable. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct { uint32_t rate; } device;
static device fake;
uint32_t rtlsdr_get_device_count(void) { return 1; }
const char* rtlsdr_get_device_name(uint32_t index) { (void)index; return "TEST DOUBLE (no hardware)"; }
int rtlsdr_open(device** out, uint32_t index) { if (index) return -1; *out = &fake; return 0; }
int rtlsdr_close(device* d) { (void)d; return 0; }
int rtlsdr_set_sample_rate(device* d, uint32_t rate) { d->rate = rate; return 0; }
uint32_t rtlsdr_get_sample_rate(device* d) { return d->rate; }
int rtlsdr_set_center_freq(device* d, uint32_t hz) { (void)d; return hz == 350950000 ? 0 : -1; }
int rtlsdr_set_freq_correction(device* d, int ppm) { (void)d; return ppm == 2 ? 0 : -1; }
int rtlsdr_set_tuner_gain_mode(device* d, int mode) { (void)d; (void)mode; return 0; }
int rtlsdr_set_agc_mode(device* d, int mode) { (void)d; (void)mode; return 0; }
int rtlsdr_reset_buffer(device* d) { (void)d; return 0; }
int rtlsdr_get_tuner_gains(device* d, int* gains) { (void)d; if (gains) { gains[0] = 0; gains[1] = 200; gains[2] = 400; } return 3; }
int rtlsdr_set_tuner_gain(device* d, int gain) { (void)d; return gain == 200 ? 0 : -1; }
int rtlsdr_read_sync(device* d, void* bytes, int count, int* received) {
    (void)d;
    if (getenv("T98_TEST_USB_FAILURE")) return -7;
    memset(bytes, 127, (size_t)count); *received = count; return 0;
}
