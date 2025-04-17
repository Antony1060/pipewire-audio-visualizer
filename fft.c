#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<stdint.h>
#include<assert.h>
#include<string.h>
#include<math.h>

#define SAMPLES 2048
#define SAMPLE_RATE 44100

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

typedef struct {
    float real;
    float imag;
} complex_t;

complex_t complex_add(complex_t *l, complex_t *r) {
    return (complex_t) {
        .real = l->real + r->real,
        .imag = l->imag + r->imag
    };
}

complex_t complex_mul(complex_t *l, complex_t *r) {
    return (complex_t) {
        .real = l->real * r->real - l->imag * r->imag,
        .imag = l->real * r->imag + l->imag * r->real
    };
}

typedef struct {
    complex_t *items;
    size_t size;
} complex_arr_t;

void dft(complex_arr_t *in, complex_arr_t *out) {
    //assert(in->size == out->size);
    size_t n = in->size;

    for (size_t k = 0; k < out->size; k++) {
        complex_t res = {0};
        
        for (size_t t = 0; t < n; t++) {
            complex_t curr = in->items[t];
        
            float exp = 2 * M_PI * t * k / n;

            complex_t e_part = {
                .real = cosf(exp),
                .imag = -sinf(exp)
            };

            complex_t iter = complex_mul(&curr, &e_part);

            res = complex_add(&res, &iter);
        }
    
        out->items[k] = res;
    }
}

void fft_samples(float *samples, float *fft, size_t n_samples, size_t needed_fft) {
    complex_t in_buf[SAMPLES];
    complex_t out_buf[20000];

    memset(out_buf, 0, n_samples * sizeof(complex_t));

    for (size_t i = 0; i < n_samples; i++) {
        in_buf[i] = (complex_t) {
            .real = samples[i],
            .imag = 0
        };
    }

    complex_arr_t in = {
        .items = in_buf,
        .size = n_samples
    };

    complex_arr_t out = {
        .items = out_buf,
        .size = needed_fft 
    };

    dft(&in, &out);

    for (size_t i = 0; i < out.size; i++) {
        complex_t curr = out.items[i];

        fft[i] = MIN(40, MAX(0, curr.real));
    }
} 
