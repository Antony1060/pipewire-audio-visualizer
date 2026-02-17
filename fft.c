#include<stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<stdint.h>
#include<assert.h>
#include<string.h>
#include<math.h>

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

complex_arr_t complex_arr_new(complex_t *items, size_t size) {
    return (complex_arr_t) {
        items, size
    };
}

void fft(complex_arr_t arr) {
    assert(__builtin_popcount(arr.size) == 1);

    size_t n = arr.size;

    if (n == 1)
        return;

    size_t half = n / 2;

    complex_t even_buf[half];
    complex_t odd_buf[half];

    for (size_t i = 0; i < half; i++) {
        even_buf[i] = arr.items[i * 2];
        odd_buf[i] = arr.items[i * 2 + 1];
    }

    fft(complex_arr_new(even_buf, half));
    fft(complex_arr_new(odd_buf, half));

    float angle = 2 * M_PI / n;
    complex_t w = { .real = 1 };
    complex_t wn = { cosf(angle), -sinf(angle) };
    for (size_t i = 0; i < half; i++) {
        complex_t e_curr = even_buf[i];
        complex_t o_curr = odd_buf[i];

        complex_t m = complex_mul(&w, &o_curr);

        arr.items[i] = complex_add(&e_curr, &m);
        arr.items[i + half] = complex_sub(&e_curr, &m);

        w = complex_mul(&w, &wn);
    }
}

void fft_samples(float *samples, float *fft_out, float *fft_imag_out, size_t n_samples) {
    complex_t buf[n_samples];

    for (size_t i = 0; i < n_samples; i++)
        buf[i] = (complex_t) { samples[i], 0 };

    complex_arr_t in = complex_arr_new(buf, n_samples);

    fft(in);

    for (size_t i = 0; i < in.size; i++) {
        fft_out[i] = in.items[i].real;
        fft_imag_out[i] = in.items[i].imag;
    }
}
