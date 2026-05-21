#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#endif


#include <mkl.h>

#if defined(__AVX2__) || defined(_M_AVX2)
    #include <immintrin.h>
    #define HAS_AVX2_CODE 1
#else
    #define HAS_AVX2_CODE 0
#endif

#if HAS_AVX2_CODE && (defined(__FMA__) || defined(_MSC_VER))
    #define HAS_FMA_CODE 1
#else
    #define HAS_FMA_CODE 0
#endif

struct ComplexFloat {
    float re;
    float im;
};

static_assert(sizeof(ComplexFloat) == 2 * sizeof(float), "ComplexFloat должен занимать ровно 8 байт");

struct Config {
    int n = 1024;
    int threads = 0;
    bool skip_naive = false;
};

struct RunResult {
    std::string name;
    double seconds = 0.0;
    double mflops = 0.0;
    double max_error_to_mkl = 0.0;
};

void setup_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::system("chcp 65001 > nul");
#endif
}

void print_usage(const char* exe) {
    std::cout
        << "Использование:\n"
        << "  " << exe << " [--n 1024] [--threads <число>] [--skip-naive]\n\n"
        << "Примеры:\n"
        << "  " << exe << "\n"
        << "  " << exe << " --n 512 --threads 8\n"
        << "  " << exe << " --n 1024 --skip-naive\n";
}

Config parse_args(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--n") {
            if (i + 1 >= argc) {
                throw std::runtime_error("После --n должно быть число");
            }
            cfg.n = std::stoi(argv[++i]);
        } else if (arg == "--threads") {
            if (i + 1 >= argc) {
                throw std::runtime_error("После --threads должно быть число");
            }
            cfg.threads = std::stoi(argv[++i]);
        } else if (arg == "--skip-naive") {
            cfg.skip_naive = true;
        } else {
            throw std::runtime_error("Неизвестный аргумент: " + arg);
        }
    }

    if (cfg.n <= 0) {
        throw std::runtime_error("Размерность матрицы должна быть положительной");
    }
    if (cfg.threads < 0) {
        throw std::runtime_error("Число потоков не может быть отрицательным");
    }
    return cfg;
}

int g_custom_thread_count = 1;

void configure_threads(int requested_threads) {
    int threads = requested_threads;
    if (threads == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        // По умолчанию берем до 8 потоков; другое значение можно задать через --threads.
        threads = hw == 0 ? 1 : std::min(8, static_cast<int>(hw));
    }

    g_custom_thread_count = std::max(1, threads);

    mkl_set_dynamic(0);
    mkl_set_num_threads(g_custom_thread_count);
}

int active_threads() {
    return g_custom_thread_count;
}

void generate_matrix(std::vector<ComplexFloat>& a, int n, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& x : a) {
        x.re = dist(rng);
        x.im = dist(rng);
    }
}

void clear_matrix(std::vector<ComplexFloat>& c) {
    std::fill(c.begin(), c.end(), ComplexFloat{0.0f, 0.0f});
}

void multiply_naive_formula(const std::vector<ComplexFloat>& a,
                            const std::vector<ComplexFloat>& b,
                            std::vector<ComplexFloat>& c,
                            int n) {
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            float sum_re = 0.0f;
            float sum_im = 0.0f;
            for (int k = 0; k < n; ++k) {
                const ComplexFloat av = a[static_cast<std::size_t>(i) * n + k];
                const ComplexFloat bv = b[static_cast<std::size_t>(k) * n + j];
                sum_re += av.re * bv.re - av.im * bv.im;
                sum_im += av.re * bv.im + av.im * bv.re;
            }
            c[static_cast<std::size_t>(i) * n + j] = ComplexFloat{sum_re, sum_im};
        }
    }
}

void multiply_mkl_cblas(const std::vector<ComplexFloat>& a,
                        const std::vector<ComplexFloat>& b,
                        std::vector<ComplexFloat>& c,
                        int n) {
    const ComplexFloat alpha{1.0f, 0.0f};
    const ComplexFloat beta{0.0f, 0.0f};

    cblas_cgemm(CblasRowMajor,
                CblasNoTrans,
                CblasNoTrans,
                static_cast<MKL_INT>(n),
                static_cast<MKL_INT>(n),
                static_cast<MKL_INT>(n),
                &alpha,
                a.data(),
                static_cast<MKL_INT>(n),
                b.data(),
                static_cast<MKL_INT>(n),
                &beta,
                c.data(),
                static_cast<MKL_INT>(n));
}

inline ComplexFloat complex_mul_add_scalar(const ComplexFloat& sum,
                                           const ComplexFloat& a,
                                           const ComplexFloat& b) {
    return ComplexFloat{
        sum.re + a.re * b.re - a.im * b.im,
        sum.im + a.re * b.im + a.im * b.re
    };
}

ComplexFloat dot_complex_scalar(const ComplexFloat* a, const ComplexFloat* bt_row, int n) {
    ComplexFloat sum{0.0f, 0.0f};
    for (int k = 0; k < n; ++k) {
        sum = complex_mul_add_scalar(sum, a[k], bt_row[k]);
    }
    return sum;
}

#if HAS_AVX2_CODE
inline __m256 complex_mul_4_avx2_from_ar_ai(__m256 ar, __m256 ai, __m256 b_values) {
    // ar = [ar0 ar0 ar1 ar1 ar2 ar2 ar3 ar3]
    // ai = [ai0 ai0 ai1 ai1 ai2 ai2 ai3 ai3]
    // b_values = [br0 bi0 br1 bi1 br2 bi2 br3 bi3]
    const __m256 b_swapped = _mm256_permute_ps(b_values, 0xB1);   // [bi0 br0 bi1 br1 ...]
    const __m256 ar_times_b = _mm256_mul_ps(ar, b_values);
    const __m256 ai_times_b_swapped = _mm256_mul_ps(ai, b_swapped);
    return _mm256_addsub_ps(ar_times_b, ai_times_b_swapped);
}

inline __m256 complex_fmadd_4_avx2_from_ar_ai(__m256 acc, __m256 ar, __m256 ai, __m256 b_values) {
#if HAS_FMA_CODE
    const __m256 sign_re_negative = _mm256_castsi256_ps(
        _mm256_setr_epi32(static_cast<int>(0x80000000u), 0,
                          static_cast<int>(0x80000000u), 0,
                          static_cast<int>(0x80000000u), 0,
                          static_cast<int>(0x80000000u), 0));
    const __m256 b_swapped = _mm256_permute_ps(b_values, 0xB1);
    const __m256 b_swapped_signed = _mm256_xor_ps(b_swapped, sign_re_negative);

    acc = _mm256_fmadd_ps(ar, b_values, acc);
    acc = _mm256_fmadd_ps(ai, b_swapped_signed, acc);
    return acc;
#else
    return _mm256_add_ps(acc, complex_mul_4_avx2_from_ar_ai(ar, ai, b_values));
#endif
}

inline ComplexFloat horizontal_sum_complex_4_avx2(__m256 acc) {
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, acc);
    return ComplexFloat{
        tmp[0] + tmp[2] + tmp[4] + tmp[6],
        tmp[1] + tmp[3] + tmp[5] + tmp[7]
    };
}

ComplexFloat dot_complex_avx2(const ComplexFloat* a, const ComplexFloat* bt_row, int n) {
    __m256 acc = _mm256_setzero_ps();
    int k = 0;
    const int vec_step = 4;
    const int n_vec = n - (n % vec_step);

    const float* af = reinterpret_cast<const float*>(a);
    const float* bf = reinterpret_cast<const float*>(bt_row);

    for (; k < n_vec; k += vec_step) {
        const __m256 av = _mm256_loadu_ps(af + 2 * k);
        const __m256 ar = _mm256_moveldup_ps(av);
        const __m256 ai = _mm256_movehdup_ps(av);
        const __m256 bv = _mm256_loadu_ps(bf + 2 * k);
        acc = complex_fmadd_4_avx2_from_ar_ai(acc, ar, ai, bv);
    }

    ComplexFloat sum = horizontal_sum_complex_4_avx2(acc);
    for (; k < n; ++k) {
        sum = complex_mul_add_scalar(sum, a[k], bt_row[k]);
    }
    return sum;
}

void dot4_complex_avx2(const ComplexFloat* a,
                       const ComplexFloat* bt0,
                       const ComplexFloat* bt1,
                       const ComplexFloat* bt2,
                       const ComplexFloat* bt3,
                       int n,
                       ComplexFloat& r0,
                       ComplexFloat& r1,
                       ComplexFloat& r2,
                       ComplexFloat& r3) {
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    __m256 acc2 = _mm256_setzero_ps();
    __m256 acc3 = _mm256_setzero_ps();

    int k = 0;
    const int vec_step = 4;
    const int n_vec = n - (n % vec_step);

    const float* af  = reinterpret_cast<const float*>(a);
    const float* b0f = reinterpret_cast<const float*>(bt0);
    const float* b1f = reinterpret_cast<const float*>(bt1);
    const float* b2f = reinterpret_cast<const float*>(bt2);
    const float* b3f = reinterpret_cast<const float*>(bt3);

    for (; k < n_vec; k += vec_step) {
        const __m256 av = _mm256_loadu_ps(af + 2 * k);
        const __m256 ar = _mm256_moveldup_ps(av);
        const __m256 ai = _mm256_movehdup_ps(av);

        const __m256 bv0 = _mm256_loadu_ps(b0f + 2 * k);
        const __m256 bv1 = _mm256_loadu_ps(b1f + 2 * k);
        const __m256 bv2 = _mm256_loadu_ps(b2f + 2 * k);
        const __m256 bv3 = _mm256_loadu_ps(b3f + 2 * k);

        acc0 = complex_fmadd_4_avx2_from_ar_ai(acc0, ar, ai, bv0);
        acc1 = complex_fmadd_4_avx2_from_ar_ai(acc1, ar, ai, bv1);
        acc2 = complex_fmadd_4_avx2_from_ar_ai(acc2, ar, ai, bv2);
        acc3 = complex_fmadd_4_avx2_from_ar_ai(acc3, ar, ai, bv3);
    }

    r0 = horizontal_sum_complex_4_avx2(acc0);
    r1 = horizontal_sum_complex_4_avx2(acc1);
    r2 = horizontal_sum_complex_4_avx2(acc2);
    r3 = horizontal_sum_complex_4_avx2(acc3);

    for (; k < n; ++k) {
        r0 = complex_mul_add_scalar(r0, a[k], bt0[k]);
        r1 = complex_mul_add_scalar(r1, a[k], bt1[k]);
        r2 = complex_mul_add_scalar(r2, a[k], bt2[k]);
        r3 = complex_mul_add_scalar(r3, a[k], bt3[k]);
    }
}
#endif

template <class Fn>
void parallel_for_rows(int n, Fn&& fn) {
    const int threads = std::max(1, std::min(g_custom_thread_count, n));
    if (threads == 1) {
        for (int i = 0; i < n; ++i) {
            fn(i);
        }
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads));

    const int base = n / threads;
    const int rem = n % threads;
    int begin = 0;

    for (int t = 0; t < threads; ++t) {
        const int count = base + (t < rem ? 1 : 0);
        const int start = begin;
        const int finish = start + count;
        begin = finish;

        workers.emplace_back([start, finish, &fn]() {
            for (int i = start; i < finish; ++i) {
                fn(i);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }
}

void transpose_b(const std::vector<ComplexFloat>& b, std::vector<ComplexFloat>& bt, int n) {
    parallel_for_rows(n, [&](int i) {
        for (int j = 0; j < n; ++j) {
            bt[static_cast<std::size_t>(j) * n + i] = b[static_cast<std::size_t>(i) * n + j];
        }
    });
}

void multiply_optimized_own(const std::vector<ComplexFloat>& a,
                            const std::vector<ComplexFloat>& b,
                            std::vector<ComplexFloat>& c,
                            int n) {
    std::vector<ComplexFloat> bt(static_cast<std::size_t>(n) * n);
    transpose_b(b, bt, n);

    parallel_for_rows(n, [&](int i) {
        const ComplexFloat* a_row = &a[static_cast<std::size_t>(i) * n];
        int j = 0;

#if HAS_AVX2_CODE
        for (; j + 3 < n; j += 4) {
            ComplexFloat r0, r1, r2, r3;
            dot4_complex_avx2(a_row,
                              &bt[static_cast<std::size_t>(j + 0) * n],
                              &bt[static_cast<std::size_t>(j + 1) * n],
                              &bt[static_cast<std::size_t>(j + 2) * n],
                              &bt[static_cast<std::size_t>(j + 3) * n],
                              n,
                              r0, r1, r2, r3);
            c[static_cast<std::size_t>(i) * n + (j + 0)] = r0;
            c[static_cast<std::size_t>(i) * n + (j + 1)] = r1;
            c[static_cast<std::size_t>(i) * n + (j + 2)] = r2;
            c[static_cast<std::size_t>(i) * n + (j + 3)] = r3;
        }
#endif

        for (; j < n; ++j) {
#if HAS_AVX2_CODE
            c[static_cast<std::size_t>(i) * n + j] = dot_complex_avx2(a_row, &bt[static_cast<std::size_t>(j) * n], n);
#else
            c[static_cast<std::size_t>(i) * n + j] = dot_complex_scalar(a_row, &bt[static_cast<std::size_t>(j) * n], n);
#endif
        }
    });
}

template <class Fn>
double measure_seconds(Fn&& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

double assigned_complexity(int n) {
    const double nd = static_cast<double>(n);
    return 2.0 * nd * nd * nd;
}

double calc_mflops(double operations, double seconds) {
    return operations / seconds * 1.0e-6;
}

double max_abs_difference(const std::vector<ComplexFloat>& x,
                          const std::vector<ComplexFloat>& y) {
    if (x.size() != y.size()) {
        throw std::runtime_error("Матрицы для сравнения имеют разный размер");
    }

    double max_diff = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dr = static_cast<double>(x[i].re) - static_cast<double>(y[i].re);
        const double di = static_cast<double>(x[i].im) - static_cast<double>(y[i].im);
        const double diff = std::sqrt(dr * dr + di * di);
        if (diff > max_diff) {
            max_diff = diff;
        }
    }
    return max_diff;
}

void print_result_row(const RunResult& r, bool print_error) {
    std::cout << std::left << std::setw(36) << r.name
              << std::right << std::setw(14) << std::fixed << std::setprecision(6) << r.seconds
              << std::setw(18) << std::fixed << std::setprecision(3) << r.mflops;
    if (print_error) {
        std::cout << std::setw(20) << std::scientific << std::setprecision(3) << r.max_error_to_mkl;
    } else {
        std::cout << std::setw(20) << "-";
    }
    std::cout << '\n';
}

int main(int argc, char** argv) {
    setup_utf8_console();

    try {
        const Config cfg = parse_args(argc, argv);
        configure_threads(cfg.threads);

        const int n = cfg.n;
        const std::size_t matrix_size = static_cast<std::size_t>(n) * n;
        const double c = assigned_complexity(n);

        std::vector<ComplexFloat> a(matrix_size);
        std::vector<ComplexFloat> b(matrix_size);
        std::vector<ComplexFloat> c_naive(matrix_size);
        std::vector<ComplexFloat> c_mkl(matrix_size);
        std::vector<ComplexFloat> c_opt(matrix_size);

        generate_matrix(a, n, 12345u);
        generate_matrix(b, n, 67890u);

        std::cout << "Перемножение квадратных матриц single complex\n";
        std::cout << "n = " << n << "\n";
        std::cout << "Формула сложности задания: c = 2*n^3 = "
                  << std::fixed << std::setprecision(0) << c << " операций\n";
        std::cout << "Потоки собственного алгоритма: " << active_threads()
                  << ", потоки MKL: " << mkl_get_max_threads() << "\n";
#if HAS_AVX2_CODE
        std::cout << "Оптимизированный вариант: транспонирование B + std::thread + AVX2 dot4\n\n";
#else
        std::cout << "Оптимизированный вариант: транспонирование B + std::thread, AVX2 не включен\n\n";
#endif

        RunResult mkl_result;
        mkl_result.name = "2) MKL cblas_cgemm";
        clear_matrix(c_mkl);
        mkl_result.seconds = measure_seconds([&] { multiply_mkl_cblas(a, b, c_mkl, n); });
        mkl_result.mflops = calc_mflops(c, mkl_result.seconds);

        RunResult naive_result;
        if (!cfg.skip_naive) {
            naive_result.name = "1) Формула линейной алгебры";
            clear_matrix(c_naive);
            naive_result.seconds = measure_seconds([&] { multiply_naive_formula(a, b, c_naive, n); });
            naive_result.mflops = calc_mflops(c, naive_result.seconds);
            naive_result.max_error_to_mkl = max_abs_difference(c_naive, c_mkl);
        }

        RunResult opt_result;
        opt_result.name = "3) Собственный оптимизированный";
        clear_matrix(c_opt);
        opt_result.seconds = measure_seconds([&] { multiply_optimized_own(a, b, c_opt, n); });
        opt_result.mflops = calc_mflops(c, opt_result.seconds);
        opt_result.max_error_to_mkl = max_abs_difference(c_opt, c_mkl);

        std::cout << std::left << std::setw(36) << "Вариант"
                  << std::right << std::setw(14) << "Время, с"
                  << std::setw(18) << "MFlops"
                  << std::setw(20) << "max |Δ| к MKL" << '\n';
        std::cout << std::string(88, '-') << '\n';
        if (!cfg.skip_naive) {
            print_result_row(naive_result, true);
        } else {
            std::cout << "1) Формула линейной алгебры: пропущено (--skip-naive)\n";
        }
        print_result_row(mkl_result, false);
        print_result_row(opt_result, true);

        const double ratio = (mkl_result.mflops > 0.0) ? (opt_result.mflops / mkl_result.mflops * 100.0) : 0.0;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\nПроизводительность 3-го варианта относительно MKL: " << ratio << "%\n";
        if (ratio >= 30.0) {
            std::cout << "Требование >= 30% от MKL выполнено.\n";
        } else {
            std::cout << "Требование >= 30% от MKL не выполнено на этой машине/сборке. "
                      << "Проверьте Release-сборку, AVX2, число потоков и PATH к oneMKL.\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    }
}
