#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<stdint.h>
#include<assert.h>
#include<string.h>
#include<math.h>

#define SAMPLES 2048
#define SAMPLE_RATE 44100

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

complex_t complex_sub(complex_t *l, complex_t *r) {
    return (complex_t) {
        .real = l->real - r->real,
        .imag = l->imag - r->imag
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

void fft(complex_arr_t *in, complex_arr_t *out) {
    assert(in->size == out->size);
    assert(__builtin_popcount(in->size) == 1);

    size_t n = in->size;

    if (n == 1) {
        memcpy(out->items, in->items, n * sizeof(complex_t));
        return;
    }

    size_t half = n / 2;
    complex_t even_buf[half];
    complex_t odd_buf[half];
    complex_t even_res_buf[half];
    complex_t odd_res_buf[half];

    for (size_t i = 0; i < half; i++)
        even_buf[i] = in->items[i * 2];

    for (size_t i = 0; i < half; i++)
        odd_buf[i] = in->items[i * 2 + 1];

    fft(
        &(complex_arr_t) { even_buf, half },
        &(complex_arr_t) { even_res_buf, half}
    );

    fft(
        &(complex_arr_t) { odd_buf, half },
        &(complex_arr_t) { odd_res_buf, half}
    );

    float angle = 2 * M_PI / n;
    complex_t w = { .real = 1 };
    complex_t wn = { cosf(angle), -sinf(angle) };
    for (size_t i = 0; i < half; i++) {
        complex_t e_curr = even_res_buf[i];
        complex_t o_curr = odd_res_buf[i];

        complex_t m = complex_mul(&w, &o_curr);

        out->items[i] = complex_add(&e_curr, &m);
        out->items[i + half] = complex_sub(&e_curr, &m);

        w = complex_mul(&w, &wn);
    }
}

void fft_samples(float *samples, float *fft_out, size_t n_samples) {
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
        .size = n_samples
    };

    fft(&in, &out);

    for (size_t i = 0; i < out.size; i++) {
        complex_t curr = out.items[i];

        fft_out[i] = curr.real;
    }
}
