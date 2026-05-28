#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <mkl.h>
#include <omp.h>

struct Complex {
    float re;
    float im;
};

static_assert(sizeof(Complex) == 2 * sizeof(float), "Complex должен состоять из двух float");

const int N = 1024;

void setup_console() {
#ifdef _WIN32
    std::system("chcp 65001 > nul");
#endif
}

double operations_count() {
    return 2.0 * N * N * N;
}

double mflops(double seconds) {
    return operations_count() / seconds * 1.0e-6;
}

void generate_matrix(std::vector<Complex>& a) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            int v = i * N + j;
            a[v].re = static_cast<float>((i * 17 + j * 13) % 100) / 100.0f;
            a[v].im = static_cast<float>((i * 11 + j * 19) % 100) / 100.0f;
        }
    }
}

void multiply_formula(const std::vector<Complex>& a,
                      const std::vector<Complex>& b,
                      std::vector<Complex>& c) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            float sum_re = 0.0f;
            float sum_im = 0.0f;

            for (int k = 0; k < N; ++k) {
                const Complex x = a[i * N + k];
                const Complex y = b[k * N + j];
                sum_re += x.re * y.re - x.im * y.im;
                sum_im += x.re * y.im + x.im * y.re;
            }

            c[i * N + j] = {sum_re, sum_im};
        }
    }
}

void multiply_blas(const std::vector<Complex>& a,
                   const std::vector<Complex>& b,
                   std::vector<Complex>& c) {
    const Complex alpha{1.0f, 0.0f};
    const Complex beta{0.0f, 0.0f};

    cblas_cgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                N, N, N,
                &alpha,
                a.data(), N,
                b.data(), N,
                &beta,
                c.data(), N);
}

void transpose(const std::vector<Complex>& b, std::vector<Complex>& bt) {
    #pragma omp parallel for
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            bt[j * N + i] = b[i * N + j];
        }
    }
}

void multiply_optimized(const std::vector<Complex>& a,
                        const std::vector<Complex>& b,
                        std::vector<Complex>& c) {
    std::vector<Complex> bt(N * N);
    transpose(b, bt);

    #pragma omp parallel for
    for (int i = 0; i < N; ++i) {
        const Complex* row_a = &a[i * N];

        for (int j = 0; j < N; j += 4) {
            float r0 = 0.0f, s0 = 0.0f;
            float r1 = 0.0f, s1 = 0.0f;
            float r2 = 0.0f, s2 = 0.0f;
            float r3 = 0.0f, s3 = 0.0f;

            const Complex* row_b0 = &bt[(j + 0) * N];
            const Complex* row_b1 = &bt[(j + 1) * N];
            const Complex* row_b2 = &bt[(j + 2) * N];
            const Complex* row_b3 = &bt[(j + 3) * N];

            #pragma omp simd reduction(+:r0,s0,r1,s1,r2,s2,r3,s3)
            for (int k = 0; k < N; ++k) {
                const float ar = row_a[k].re;
                const float ai = row_a[k].im;

                r0 += ar * row_b0[k].re - ai * row_b0[k].im;
                s0 += ar * row_b0[k].im + ai * row_b0[k].re;

                r1 += ar * row_b1[k].re - ai * row_b1[k].im;
                s1 += ar * row_b1[k].im + ai * row_b1[k].re;

                r2 += ar * row_b2[k].re - ai * row_b2[k].im;
                s2 += ar * row_b2[k].im + ai * row_b2[k].re;

                r3 += ar * row_b3[k].re - ai * row_b3[k].im;
                s3 += ar * row_b3[k].im + ai * row_b3[k].re;
            }

            c[i * N + j + 0] = {r0, s0};
            c[i * N + j + 1] = {r1, s1};
            c[i * N + j + 2] = {r2, s2};
            c[i * N + j + 3] = {r3, s3};
        }
    }
}

template <class Func>
double measure(Func func) {
    auto start = std::chrono::steady_clock::now();
    func();
    auto finish = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(finish - start).count();
}



double max_difference(const std::vector<Complex>& a,
                      const std::vector<Complex>& b) {
    double answer = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i) {
        double dr = static_cast<double>(a[i].re) - b[i].re;
        double di = static_cast<double>(a[i].im) - b[i].im;
        answer = std::max(answer, std::sqrt(dr * dr + di * di));
    }

    return answer;
}

void print_result(const char* name, double seconds, double error) {
    std::cout << std::left << std::setw(32) << name
              << std::right << std::setw(12) << std::fixed << std::setprecision(4) << seconds
              << std::setw(16) << std::fixed << std::setprecision(2) << mflops(seconds)
              << std::setw(18) << std::scientific << std::setprecision(3) << error
              << '\n';
}

int main() {
    setup_console();
    mkl_set_dynamic(0);
    mkl_set_num_threads(omp_get_max_threads());

    std::vector<Complex> a(N * N);
    std::vector<Complex> b(N * N);
    std::vector<Complex> c1(N * N);
    std::vector<Complex> c2(N * N);
    std::vector<Complex> c3(N * N);

    generate_matrix(a);
    generate_matrix(b);

    std::cout << "Размер матриц: " << N << " x " << N << '\n';
    std::cout << "Сложность: c = 2*n^3 = " << std::fixed << std::setprecision(0)
              << operations_count() << '\n';

    double t2 = measure([&] { multiply_blas(a, b, c2); });
    double t1 = measure([&] { multiply_formula(a, b, c1); });
    double t3 = measure([&] { multiply_optimized(a, b, c3); });

    std::cout << std::left << std::setw(32) << "Вариант"
              << std::right << std::setw(12) << "Время, с"
              << std::setw(16) << "MFlops"
              << std::setw(18) << "Макс. ошибка" << '\n';
    std::cout << std::string(78, '-') << '\n';

    print_result("1) Формула", t1, max_difference(c1, c2));
    print_result("2) BLAS cblas_cgemm", t2, 0.0);
    print_result("3) Свой оптимизированный", t3, max_difference(c3, c2));

    double percent = mflops(t3) / mflops(t2) * 100.0;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nСвой оптимизированный / BLAS = " << percent << "%\n";
    std::cout << (percent >= 30.0 ? "Требование выполнено\n" : "Требование не выполнено\n");

    std::cout << "\nДубянская Кира Игоревна 090304-РПИа-о25\n";

    return 0;
}
