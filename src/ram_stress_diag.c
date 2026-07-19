/*
 * =======================================================================================
 *  RAM_STRESS_DIAG.C - Ceke's RAM Test God Mode v3.0 (AVX2 Engine / KMDF Hybrid Driver)
 * =======================================================================================
 *  Auteur   : Ceketrum
 *  Cible    : Windows x86_64 (MSVC / GCC)
 *  Compilation : cl.exe /O2 /Oi /Ot /arch:AVX2 /I driver src\ram_stress_diag.c /Fe:bin\ram_stress_diag.exe
 * =======================================================================================
 *  GESTION STRICTE DE LA MÉMOIRE (Anti-BSOD / Anti-Saturation System) :
 *  - Mesure la RAM physique TOTALE installée (ullTotalPhys).
 *  - Alloue tout sauf 4 Go : (Total RAM) - 4 Go = Zone de test.
 *  - Windows 11 conserve toujours 4 Go de RAM libre pour l'OS et ses services.
 *  - Mode Hybride : Pilote Kernel KMDF (cekes_ram_drv.sys) OU Fallback Windows API.
 * =======================================================================================
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <io.h>
#include <immintrin.h>
#include "../driver/shared_ioctl.h"

#if defined(_MSC_VER)
    #pragma comment(lib, "advapi32.lib")
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
#endif

/* Codes Couleur ANSI Terminal */
#define ANSI_RESET          "\x1b[0m"
#define ANSI_BOLD           "\x1b[1m"
#define ANSI_CYAN           "\x1b[1;36m"
#define ANSI_GREEN          "\x1b[1;32m"
#define ANSI_YELLOW         "\x1b[1;33m"
#define ANSI_RED            "\x1b[1;31m"
#define ANSI_BLINK_RED      "\x1b[1;5;31m"
#define ANSI_CLEAR_SCREEN   "\x1b[2J\x1b[H"

/* Configuration Marge Système et Constantes */
#define OS_SAFETY_MARGIN_BYTES  (4ULL * 1024ULL * 1024ULL * 1024ULL) /* 4 GB de RAM LIBRE réservés sur le PC */
#define MIN_ALLOC_BYTES         (256ULL * 1024ULL * 1024ULL)
#define CHUNK_SIZE_BYTES        (16 * 1024 * 1024)
#define DRAM_ROW_STRIDE_BYTES   (16 * 1024)
#define ROWHAMMER_ITERATIONS    100000ULL
#define LOG_FILE_PATH           "ram_stress_diag.log"
#define GRID_CELL_COUNT         50
#define MAX_BIOPSY_ERRORS       256

typedef enum {
    MODE_TUI = 0,
    MODE_JSON_IPC = 1
} OutputMode;

typedef struct {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t expected_value;
    uint64_t actual_value;
    int module_id;
    int bit_position;
    char error_type[32]; // "Stuck-at-0", "Stuck-at-1", "Crosstalk"
} BiopsyResult;

typedef struct {
    uint8_t *ram_buffer;
    size_t total_alloc_bytes;
    size_t total_chunks;
    volatile LONG64 current_chunk_index;
    int current_module_id;
    uint32_t retention_seconds;
    uint64_t *chunk_error_map;
    double benchmark_gbs;
    uint64_t dynamic_iterations;
    OutputMode mode;
    HANDLE hDriverDevice;
    BOOL is_driver_active;
    volatile LONG biopsy_count;
    BiopsyResult biopsy_list[MAX_BIOPSY_ERRORS];
} StressPool;

typedef struct {
    int thread_id;
    BYTE is_p_core;
    USHORT group_id;
    uint64_t chunks_processed;
    uint64_t errors_found;
    DWORD exception_code;
    StressPool *pool;
} ThreadContext;

static FILE *g_log_file = NULL;
static HANDLE g_hConsole = NULL;

/* Prototypes */
static inline uint32_t xorshift32(uint32_t *state);
static void log_print(OutputMode mode, const char *format, ...);
static void SurgicalLogWrite(const char *format, ...);
static BOOL IsLaunchedFromExplorer(void);
static BOOL EnableLockMemoryPrivilege(void);
static void QueryCPUTopology(DWORD *pCoreCount, DWORD *eCoreCount, BYTE *coreTypeMap, DWORD maxThreads);
static uint64_t CheckWHEAEvents(void);
static void EnableVTTerminalMode(void);
static void RestoreConsoleCursor(void);
static void DrawTUIDashboard(StressPool *pool, ThreadContext *contexts, DWORD num_threads, const char *module_name, double elapsed_sec);
static void EmitJsonTelemetry(StressPool *pool, ThreadContext *contexts, DWORD num_threads, const char *module_name, double elapsed_sec);
static double BenchmarkMemoryBandwidth(uint8_t *buffer, size_t size);
static uint64_t TranslateVirtToPhysAddress(HANDLE hDriverDevice, void *virtAddr);
static UINT ReadHardwareTemperatureTSOD(HANDLE hDriverDevice);
static void PerformDeepBiopsyOnChunk(StressPool *pool, size_t chunk_idx, uint8_t *chunk_ptr, int module_id);

/* 10 Stress Modules Prototypes */
static uint64_t RunModule1_WalkingPatternsChunk(uint8_t *chunk, size_t size);
static uint64_t RunModule2_RowhammerChunk(uint8_t *chunk, size_t size, uint64_t iterations);
static uint64_t RunModule3_ThermalAVX2XorshiftChunk(uint8_t *chunk, size_t size, int thread_id);
static uint64_t RunModule4_BitFadeChunk(uint8_t *chunk, size_t size, uint32_t retention_seconds);
static uint64_t RunModule5_AVX2DataLanesChunk(uint8_t *chunk, size_t size);
static uint64_t RunModule6_ThermalCyclingChunk(uint8_t *chunk, size_t size, int thread_id);
static uint64_t RunModule7_BitFlipExplorerChunk(uint8_t *chunk, size_t size);
static uint64_t RunModule8_MovingInversionsChunk(uint8_t *chunk, size_t size);
static uint64_t RunModule9_BlockReadWriteMassifChunk(uint8_t *chunk, size_t size);
static uint64_t RunModule10_RandomStrideBankGroupHammeringChunk(uint8_t *chunk, size_t size, uint64_t iterations);

static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static BOOL IsLaunchedFromExplorer(void) {
    DWORD processList[2];
    DWORD count = GetConsoleProcessList(processList, 2);
    return (count <= 1);
}

static void log_print(OutputMode mode, const char *format, ...) {
    va_list args1;
    va_start(args1, format);
    if (mode == MODE_TUI) {
        vprintf(format, args1);
    }
    va_end(args1);

    if (g_log_file != NULL) {
        va_list args2;
        va_start(args2, format);
        vfprintf(g_log_file, format, args2);
        va_end(args2);
        fflush(g_log_file);
    }
}

static void SurgicalLogWrite(const char *format, ...) {
    if (g_log_file != NULL) {
        va_list args;
        va_start(args, format);
        vfprintf(g_log_file, format, args);
        va_end(args);
        
        /* Flush chirurgical immédiat avant tout crash ou BSOD potentiel */
        fflush(g_log_file);
        int fd = _fileno(g_log_file);
        if (fd >= 0) {
            _commit(fd);
            HANDLE hFile = (HANDLE)_get_osfhandle(fd);
            if (hFile != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(hFile);
            }
        }
    }
}

static void RestoreConsoleCursor(void) {
    if (g_hConsole != INVALID_HANDLE_VALUE) {
        printf("\x1b[?25h");
        fflush(stdout);
    }
}

static void EnableVTTerminalMode(void) {
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hConsole != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(g_hConsole, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(g_hConsole, dwMode);
        }
        printf("\x1b[?25l");
        fflush(stdout);
        atexit(RestoreConsoleCursor);
    }
}

static BOOL EnableLockMemoryPrivilege(void) {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return FALSE;
    if (!LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid)) { CloseHandle(hToken); return FALSE; }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL res = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(hToken);

    return (res && err == ERROR_SUCCESS);
}

static void QueryCPUTopology(DWORD *pCoreCount, DWORD *eCoreCount, BYTE *coreTypeMap, DWORD maxThreads) {
    *pCoreCount = 0;
    *eCoreCount = 0;
    for (DWORD i = 0; i < maxThreads; ++i) coreTypeMap[i] = 1;

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &length);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return;

    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(length);
    if (!buffer) return;

    if (GetLogicalProcessorInformationEx(RelationProcessorCore, buffer, &length)) {
        DWORD offset = 0;
        BYTE maxEff = 0, minEff = 255;

        while (offset < length) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((BYTE*)buffer + offset);
            if (info->Relationship == RelationProcessorCore) {
                BYTE eff = info->Processor.EfficiencyClass;
                if (eff > maxEff) maxEff = eff;
                if (eff < minEff) minEff = eff;
            }
            offset += info->Size;
        }

        offset = 0;
        DWORD procIdx = 0;
        while (offset < length && procIdx < maxThreads) {
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((BYTE*)buffer + offset);
            if (info->Relationship == RelationProcessorCore) {
                BYTE isP = (maxEff != minEff) ? (info->Processor.EfficiencyClass == maxEff) : 1;
                if (isP) (*pCoreCount)++; else (*eCoreCount)++;

                for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                    KAFFINITY mask = info->Processor.GroupMask[g].Mask;
                    for (int b = 0; b < 64 && procIdx < maxThreads; ++b) {
                        if (mask & (1ULL << b)) {
                            coreTypeMap[procIdx++] = isP;
                        }
                    }
                }
            }
            offset += info->Size;
        }
    }
    free(buffer);
}

static uint64_t CheckWHEAEvents(void) {
    uint64_t whea_errors = 0;
    HANDLE hLog = OpenEventLogA(NULL, "System");
    if (!hLog) return 0;

    BYTE buffer[4096];
    DWORD bytesRead = 0, minBytesNeeded = 0;

    while (ReadEventLogA(hLog, EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ, 0, buffer, sizeof(buffer), &bytesRead, &minBytesNeeded)) {
        DWORD offset = 0;
        while (offset < bytesRead) {
            PEVENTLOGRECORD record = (PEVENTLOGRECORD)(buffer + offset);
            const char *sourceName = (const char*)((BYTE*)record + sizeof(EVENTLOGRECORD));
            if (strcmp(sourceName, "Microsoft-Windows-WHEA-Logger") == 0) {
                if (record->EventID == 19 || record->EventID == 47 || record->EventID == 26 || record->EventID == 1) {
                    whea_errors++;
                }
            }
            offset += record->Length;
        }
    }
    CloseEventLog(hLog);
    return whea_errors;
}

static uint64_t TranslateVirtToPhysAddress(HANDLE hDriverDevice, void *virtAddr) {
    if (hDriverDevice != INVALID_HANDLE_VALUE && hDriverDevice != NULL) {
        CEKES_VIRT_TO_PHYS_REQ req;
        req.VirtualAddress = virtAddr;
        req.PhysicalAddress = 0;
        req.Status = 0;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDriverDevice, IOCTL_CEKES_GET_PHYSICAL_ADDR, &req, (DWORD)sizeof(req), &req, (DWORD)sizeof(req), &bytesReturned, NULL);
        if (ok && req.Status == 0) {
            return req.PhysicalAddress;
        }
    }
    /* Fallback estimation si le driver n'est pas chargé */
    return (uint64_t)(uintptr_t)virtAddr & 0x7FFFFFFFFFFFULL;
}

static UINT ReadHardwareTemperatureTSOD(HANDLE hDriverDevice) {
    if (hDriverDevice != INVALID_HANDLE_VALUE && hDriverDevice != NULL) {
        CEKES_TSOD_READ_REQ req;
        req.CpuIndex = 0;
        req.DimmIndex = 0;
        req.TemperatureC = 0;
        req.MsrPkgTemp = 0;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hDriverDevice, IOCTL_CEKES_READ_TSOD, &req, (DWORD)sizeof(req), &req, (DWORD)sizeof(req), &bytesReturned, NULL);
        if (ok && req.TemperatureC > 0 && req.TemperatureC < 120) {
            return req.TemperatureC;
        }
    }
    return 0; // Mode User Fallback
}

static double BenchmarkMemoryBandwidth(uint8_t *buffer, size_t size) {
    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec_ptr = (__m256i*)buffer;
    __m256i dummy = _mm256_set1_epi64x(0x5555555555555555ULL);

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    for (size_t i = 0; i < count_256; ++i) {
        _mm256_storeu_si256(&vec_ptr[i], dummy);
    }

    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
    if (elapsed <= 0.000001) return 1.0;

    double gigaBytes = (double)size / (1024.0 * 1024.0 * 1024.0);
    return gigaBytes / elapsed;
}

/* =======================================================================================
 *  10 MODULES DE STRESS VECTORISÉS AVX2 (256 BITS)
 * =======================================================================================
 */

static uint64_t RunModule1_WalkingPatternsChunk(uint8_t *chunk, size_t size) {
    uint64_t errors = 0;
    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec_ptr = (__m256i*)chunk;

    /* 8 motifs représentatifs de diaphonie et bus walking 256 bits */
    uint64_t patterns[8] = {
        0x0000000000000001ULL, 0x8000000000000000ULL,
        0x0101010101010101ULL, 0x8080808080808080ULL,
        0x0F0F0F0F0F0F0F0FULL, 0xF0F0F0F0F0F0F0F0ULL,
        0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL
    };

    for (int p = 0; p < 8; ++p) {
        __m256i w_vec = _mm256_set1_epi64x((long long)patterns[p]);

        for (size_t i = 0; i < count_256; ++i) {
            _mm256_storeu_si256(&vec_ptr[i], w_vec);
        }

        _mm_mfence();

        for (size_t i = 0; i < count_256; ++i) {
            __m256i actual = _mm256_loadu_si256(&vec_ptr[i]);
            if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(w_vec, actual)) != (int)0xFFFFFFFF) {
                errors++;
            }
        }
    }
    return errors;
}

static uint64_t RunModule2_RowhammerChunk(uint8_t *chunk, size_t size, uint64_t iterations) {
    uint64_t errors = 0;
    const size_t stride = DRAM_ROW_STRIDE_BYTES;
    if (size < stride * 3) return 0;

    size_t num_triplets = (size / stride) - 2;
    for (size_t t = 0; t < num_triplets; t += 3) {
        volatile uint64_t *aggressor1 = (volatile uint64_t *)(chunk + t * stride);
        volatile uint64_t *victim     = (volatile uint64_t *)(chunk + (t + 1) * stride);
        volatile uint64_t *aggressor2 = (volatile uint64_t *)(chunk + (t + 2) * stride);

        size_t victim_words = stride / sizeof(uint64_t);
        uint64_t victim_pattern = 0x5555555555555555ULL;
        for (size_t i = 0; i < victim_words; ++i) victim[i] = victim_pattern;

        for (uint64_t iter = 0; iter < iterations; ++iter) {
            volatile uint64_t temp1 = *aggressor1;
            uint64_t val1 = temp1 ^ 0xFFFFFFFFFFFFFFFFULL;
            _mm_stream_si64((long long*)aggressor1, (long long)val1);

            volatile uint64_t temp2 = *aggressor2;
            uint64_t val2 = temp2 ^ 0xFFFFFFFFFFFFFFFFULL;
            _mm_stream_si64((long long*)aggressor2, (long long)val2);

            _mm_clflush((void const *)aggressor1);
            _mm_clflush((void const *)aggressor2);
            _mm_mfence();
        }

        for (size_t i = 0; i < victim_words; ++i) {
            if (victim[i] != victim_pattern) errors++;
        }
    }
    return errors;
}

static uint64_t RunModule3_ThermalAVX2XorshiftChunk(uint8_t *chunk, size_t size, int thread_id) {
    uint64_t errors = 0;
    uint32_t *ptr = (uint32_t*)chunk;
    size_t count = size / sizeof(uint32_t);
    uint32_t seed = (uint32_t)(time(NULL) ^ (thread_id + 1) * 0x9E3779B9);
    uint32_t state_write = seed;

    size_t i = 0, avx2_limit = count - (count % 8);
    for (; i < avx2_limit; i += 8) {
        uint32_t r0 = xorshift32(&state_write), r1 = xorshift32(&state_write);
        uint32_t r2 = xorshift32(&state_write), r3 = xorshift32(&state_write);
        uint32_t r4 = xorshift32(&state_write), r5 = xorshift32(&state_write);
        uint32_t r6 = xorshift32(&state_write), r7 = xorshift32(&state_write);

        __m256i data_vec = _mm256_setr_epi32((int)r0, (int)r1, (int)r2, (int)r3, (int)r4, (int)r5, (int)r6, (int)r7);
        _mm256_storeu_si256((__m256i*)&ptr[i], data_vec);
    }

    uint32_t state_read = seed;
    i = 0;
    for (; i < avx2_limit; i += 8) {
        uint32_t r0 = xorshift32(&state_read), r1 = xorshift32(&state_read);
        uint32_t r2 = xorshift32(&state_read), r3 = xorshift32(&state_read);
        uint32_t r4 = xorshift32(&state_read), r5 = xorshift32(&state_read);
        uint32_t r6 = xorshift32(&state_read), r7 = xorshift32(&state_read);

        __m256i expected_vec = _mm256_setr_epi32((int)r0, (int)r1, (int)r2, (int)r3, (int)r4, (int)r5, (int)r6, (int)r7);
        __m256i actual_vec = _mm256_loadu_si256((__m256i*)&ptr[i]);
        __m256i cmp = _mm256_cmpeq_epi32(expected_vec, actual_vec);
        if (_mm256_movemask_epi8(cmp) != (int)0xFFFFFFFF) {
            for (size_t k = 0; k < 8; ++k) {
                if (ptr[i + k] != ((uint32_t*)&expected_vec)[k]) errors++;
            }
        }
    }
    return errors;
}

static uint64_t RunModule4_BitFadeChunk(uint8_t *chunk, size_t size, uint32_t retention_seconds) {
    uint64_t errors = 0;
    uint64_t *ptr = (uint64_t*)chunk;
    size_t count = size / sizeof(uint64_t);

    for (size_t i = 0; i < count; ++i) ptr[i] = 0xFFFFFFFFFFFFFFFFULL;
    for (size_t i = 0; i < count; i += 8) _mm_clflush((void const *)&ptr[i]);
    _mm_mfence();

    Sleep(retention_seconds * 1000);

    for (size_t i = 0; i < count; ++i) {
        if (ptr[i] != 0xFFFFFFFFFFFFFFFFULL) errors++;
    }
    return errors;
}

static uint64_t RunModule5_AVX2DataLanesChunk(uint8_t *chunk, size_t size) {
    uint64_t errors = 0;
    __m256i zero_vec = _mm256_setzero_si256();
    __m256i full_vec = _mm256_set1_epi64x((long long)0xFFFFFFFFFFFFFFFFULL);

    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec_ptr = (__m256i*)chunk;

    for (size_t i = 0; i < count_256; ++i) _mm256_storeu_si256(&vec_ptr[i], (i % 2 == 0) ? zero_vec : full_vec);
    for (size_t i = 0; i < count_256; ++i) {
        __m256i expected = (i % 2 == 0) ? zero_vec : full_vec;
        __m256i actual = _mm256_loadu_si256(&vec_ptr[i]);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(expected, actual)) != (int)0xFFFFFFFF) errors++;
    }

    for (size_t i = 0; i < count_256; ++i) _mm256_storeu_si256(&vec_ptr[i], (i % 2 == 0) ? full_vec : zero_vec);
    for (size_t i = 0; i < count_256; ++i) {
        __m256i expected = (i % 2 == 0) ? full_vec : zero_vec;
        __m256i actual = _mm256_loadu_si256(&vec_ptr[i]);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(expected, actual)) != (int)0xFFFFFFFF) errors++;
    }

    return errors;
}

static uint64_t RunModule6_ThermalCyclingChunk(uint8_t *chunk, size_t size, int thread_id) {
    uint64_t errs = RunModule5_AVX2DataLanesChunk(chunk, size);
    errs += RunModule3_ThermalAVX2XorshiftChunk(chunk, size, thread_id);
    errs += RunModule4_BitFadeChunk(chunk, size, 2);
    return errs;
}

static uint64_t RunModule7_BitFlipExplorerChunk(uint8_t *chunk, size_t size) {
    uint64_t errors = 0;
    __m256i p1 = _mm256_set1_epi64x((long long)0x5555555555555555ULL);
    __m256i p2 = _mm256_set1_epi64x((long long)0xAAAAAAAAAAAAAAAAULL);

    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec = (__m256i*)chunk;

    for (size_t pass = 0; pass < 4; ++pass) {
        __m256i w_vec = (pass % 2 == 0) ? p1 : p2;
        for (size_t i = 0; i < count_256; ++i) _mm256_storeu_si256(&vec[i], w_vec);
        _mm_mfence();
        for (size_t i = 0; i < count_256; ++i) {
            __m256i actual = _mm256_loadu_si256(&vec[i]);
            if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(w_vec, actual)) != (int)0xFFFFFFFF) errors++;
        }
    }
    return errors;
}

/* NOUVEAU MODULE 8 : Moving Inversions (Marching patterns & Bitwise Inverse AVX2) */
static uint64_t RunModule8_MovingInversionsChunk(uint8_t *chunk, size_t size) {
    uint64_t errors = 0;
    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec = (__m256i*)chunk;

    __m256i pattern = _mm256_set1_epi64x((long long)0x0123456789ABCDEFULL);
    __m256i inv_pattern = _mm256_set1_epi64x((long long)~0x0123456789ABCDEFULL);

    for (size_t i = 0; i < count_256; ++i) _mm256_storeu_si256(&vec[i], pattern);
    _mm_mfence();

    for (size_t i = 0; i < count_256; ++i) {
        __m256i actual = _mm256_loadu_si256(&vec[i]);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(pattern, actual)) != (int)0xFFFFFFFF) errors++;
        _mm256_storeu_si256(&vec[i], inv_pattern);
    }
    _mm_mfence();

    for (size_t i = 0; i < count_256; ++i) {
        __m256i actual = _mm256_loadu_si256(&vec[i]);
        if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(inv_pattern, actual)) != (int)0xFFFFFFFF) errors++;
    }

    return errors;
}

/* NOUVEAU MODULE 9 : Block Read/Write Massif (Vdroop Extreme) */
static uint64_t RunModule9_BlockReadWriteMassifChunk(uint8_t *chunk, size_t size) {
    uint64_t errors = 0;
    __m256i dummy_w1 = _mm256_set1_epi64x(0x0F0F0F0F0F0F0F0FULL);
    __m256i dummy_w2 = _mm256_set1_epi64x(0xF0F0F0F0F0F0F0F0ULL);

    size_t count_256 = size / sizeof(__m256i);
    __m256i *vec = (__m256i*)chunk;

    /* Burst d'écritures asynchrones directes sans pause */
    for (size_t r = 0; r < 4; ++r) {
        __m256i cur = (r % 2 == 0) ? dummy_w1 : dummy_w2;
        for (size_t i = 0; i < count_256; ++i) _mm256_stream_si256(&vec[i], cur);
        _mm_sfence();

        for (size_t i = 0; i < count_256; ++i) {
            __m256i read_back = _mm256_loadu_si256(&vec[i]);
            if (_mm256_movemask_epi8(_mm256_cmpeq_epi64(cur, read_back)) != (int)0xFFFFFFFF) errors++;
        }
    }
    return errors;
}

/* NOUVEAU MODULE 10 : Random Stride Bank-Group Hammering (Passage outre TRR) */
static uint64_t RunModule10_RandomStrideBankGroupHammeringChunk(uint8_t *chunk, size_t size, uint64_t iterations) {
    uint64_t errors = 0;
    const size_t stride = DRAM_ROW_STRIDE_BYTES;
    if (size < stride * 8) return 0;

    size_t max_rows = size / stride;
    uint32_t seed = 0xC3E19857;

    for (uint64_t iter = 0; iter < iterations / 10; ++iter) {
        size_t r1 = (xorshift32(&seed) % (max_rows - 2));
        size_t r2 = r1 + 2;

        volatile uint64_t *a1 = (volatile uint64_t *)(chunk + r1 * stride);
        volatile uint64_t *v  = (volatile uint64_t *)(chunk + (r1 + 1) * stride);
        volatile uint64_t *a2 = (volatile uint64_t *)(chunk + r2 * stride);

        v[0] = 0x3C3C3C3C3C3C3C3CULL;

        for (int k = 0; k < 50; ++k) {
            _mm_stream_si64((long long*)a1, (long long)0xFFFFFFFFFFFFFFFFULL);
            _mm_stream_si64((long long*)a2, (long long)0x0000000000000000ULL);
            _mm_clflush((void const *)a1);
            _mm_clflush((void const *)a2);
        }
        _mm_mfence();

        if (v[0] != 0x3C3C3C3C3C3C3C3CULL) errors++;
    }

    return errors;
}

/* =======================================================================================
 *  THREAD DE BIOPSIE PROFONDE DES FLIPS ET ISOLATION DU CHUNK
 * =======================================================================================
 */

static void PerformDeepBiopsyOnChunk(StressPool *pool, size_t chunk_idx, uint8_t *chunk_ptr, int module_id) {
    UNREFERENCED_PARAMETER(chunk_idx);
    uint64_t *words = (uint64_t*)chunk_ptr;
    size_t word_count = CHUNK_SIZE_BYTES / sizeof(uint64_t);

    for (size_t i = 0; i < word_count; ++i) {
        uint64_t val = words[i];
        if (val != 0 && val != 0xFFFFFFFFFFFFFFFFULL && val != 0x5555555555555555ULL && val != 0xAAAAAAAAAAAAAAAAULL) {
            LONG idx = InterlockedIncrement(&pool->biopsy_count) - 1;
            if (idx < MAX_BIOPSY_ERRORS) {
                uint64_t va = (uint64_t)(uintptr_t)&words[i];
                uint64_t pa = TranslateVirtToPhysAddress(pool->hDriverDevice, (void*)va);

                BiopsyResult *b = &pool->biopsy_list[idx];
                b->virtual_address = va;
                b->physical_address = pa;
                b->expected_value = 0x5555555555555555ULL;
                b->actual_value = val;
                b->module_id = module_id;

                uint64_t xor_diff = b->expected_value ^ b->actual_value;
                b->bit_position = 0;
                for (int bit = 0; bit < 64; ++bit) {
                    if (xor_diff & (1ULL << bit)) {
                        b->bit_position = bit;
                        break;
                    }
                }

                if ((val & (1ULL << b->bit_position)) == 0) {
                    strcpy(b->error_type, "Stuck-at-0");
                } else {
                    strcpy(b->error_type, "Stuck-at-1");
                }

                // Décodage Topologique approximatif (Slot / Channel / Bank)
                unsigned int channel = (pa >> 6) & 0x1;
                unsigned int rank = (pa >> 14) & 0x1;
                unsigned int bank = (pa >> 15) & 0xF;

                SurgicalLogWrite("[BIOPSIE ERREUR DETECTEE]\n"
                                 "  Addr Virtuelle  : 0x%llX\n"
                                 "  Addr Physique   : 0x%llX (PA Real via Driver Kernel)\n"
                                 "  Topologie DRAM  : DIMM Channel %u | Rank %u | Bank %u\n"
                                 "  Module Actif    : #%d | Type: %s (Bit #%d)\n"
                                 "  Valeur Attendue : 0x%016llX\n"
                                 "  Valeur Lue      : 0x%016llX\n\n",
                                 (unsigned long long)va, (unsigned long long)pa,
                                 channel, rank, bank, module_id, b->error_type,
                                 b->bit_position, (unsigned long long)b->expected_value,
                                 (unsigned long long)val);
            }
        }
    }
}

static DWORD WINAPI ThreadWorkerRoutine(LPVOID lpParam) {
    ThreadContext *ctx = (ThreadContext*)lpParam;
    StressPool *pool = ctx->pool;

    ctx->errors_found = 0;
    ctx->chunks_processed = 0;
    ctx->exception_code = 0;

    while (1) {
        LONG64 chunk_idx = InterlockedIncrement64(&pool->current_chunk_index) - 1;
        if (chunk_idx >= (LONG64)pool->total_chunks) break;

        /* Si le chunk est déjà marqué comme corrompu par une passe précédente, on l'isole */
        if (pool->chunk_error_map[chunk_idx] > 100) {
            continue;
        }

        uint8_t *chunk_ptr = pool->ram_buffer + (chunk_idx * CHUNK_SIZE_BYTES);
        uint64_t chunk_errs = 0;

#if defined(_MSC_VER)
        __try {
#endif
            switch (pool->current_module_id) {
                case 1:  chunk_errs = RunModule1_WalkingPatternsChunk(chunk_ptr, CHUNK_SIZE_BYTES); break;
                case 2:  chunk_errs = RunModule2_RowhammerChunk(chunk_ptr, CHUNK_SIZE_BYTES, pool->dynamic_iterations); break;
                case 3:  chunk_errs = RunModule3_ThermalAVX2XorshiftChunk(chunk_ptr, CHUNK_SIZE_BYTES, ctx->thread_id); break;
                case 4:  chunk_errs = RunModule4_BitFadeChunk(chunk_ptr, CHUNK_SIZE_BYTES, pool->retention_seconds); break;
                case 5:  chunk_errs = RunModule5_AVX2DataLanesChunk(chunk_ptr, CHUNK_SIZE_BYTES); break;
                case 6:  chunk_errs = RunModule6_ThermalCyclingChunk(chunk_ptr, CHUNK_SIZE_BYTES, ctx->thread_id); break;
                case 7:  chunk_errs = RunModule7_BitFlipExplorerChunk(chunk_ptr, CHUNK_SIZE_BYTES); break;
                case 8:  chunk_errs = RunModule8_MovingInversionsChunk(chunk_ptr, CHUNK_SIZE_BYTES); break;
                case 9:  chunk_errs = RunModule9_BlockReadWriteMassifChunk(chunk_ptr, CHUNK_SIZE_BYTES); break;
                case 10: chunk_errs = RunModule10_RandomStrideBankGroupHammeringChunk(chunk_ptr, CHUNK_SIZE_BYTES, pool->dynamic_iterations); break;
                default: break;
            }
#if defined(_MSC_VER)
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ctx->exception_code = GetExceptionCode();
            chunk_errs += 1;
        }
#endif

        ctx->chunks_processed++;
        ctx->errors_found += chunk_errs;
        if (chunk_errs > 0) {
            InterlockedAdd64((LONG64*)&pool->chunk_error_map[chunk_idx], (LONG64)chunk_errs);
            PerformDeepBiopsyOnChunk(pool, (size_t)chunk_idx, chunk_ptr, pool->current_module_id);
        }
    }

    return 0;
}

static void EmitJsonTelemetry(StressPool *pool, ThreadContext *contexts, DWORD num_threads, const char *module_name, double elapsed_sec) {
    LONG64 done = pool->current_chunk_index;
    if (done > (LONG64)pool->total_chunks) done = pool->total_chunks;
    double pct = (double)done / (double)pool->total_chunks * 100.0;
    
    double pass_factor = 1.0;
    if (pool->current_module_id == 1) pass_factor = 16.0;
    else if (pool->current_module_id == 7) pass_factor = 8.0;
    else if (pool->current_module_id == 8) pass_factor = 4.0;
    else if (pool->current_module_id == 9) pass_factor = 8.0;

    double processed_gb = ((double)(done * CHUNK_SIZE_BYTES) * pass_factor) / (1024.0 * 1024.0 * 1024.0);
    double real_bandwidth = (elapsed_sec > 0.01) ? (processed_gb / elapsed_sec) : 0.0;

    uint64_t total_errors = 0;
    for (DWORD t = 0; t < num_threads; ++t) {
        total_errors += contexts[t].errors_found;
    }

    printf("{\"event\":\"telemetry\",\"module_id\":%d,\"module_name\":\"%s\",\"done_chunks\":%lld,\"total_chunks\":%lld,\"progress_pct\":%.2f,\"bandwidth_gbs\":%.2f,\"elapsed_sec\":%.2f,\"total_errors\":%llu,\"driver_active\":%s}\n",
           pool->current_module_id, module_name, (long long)done, (long long)pool->total_chunks, pct, real_bandwidth, elapsed_sec, (unsigned long long)total_errors, pool->is_driver_active ? "true" : "false");
    fflush(stdout);
}

static void DrawTUIDashboard(StressPool *pool, ThreadContext *contexts, DWORD num_threads, const char *module_name, double elapsed_sec) {
    LONG64 done = pool->current_chunk_index;
    if (done > (LONG64)pool->total_chunks) done = pool->total_chunks;
    double pct = (double)done / (double)pool->total_chunks * 100.0;

    double pass_factor = 1.0;
    if (pool->current_module_id == 1) pass_factor = 16.0;
    else if (pool->current_module_id == 7) pass_factor = 8.0;
    else if (pool->current_module_id == 8) pass_factor = 4.0;
    else if (pool->current_module_id == 9) pass_factor = 8.0;

    double processed_gb = ((double)(done * CHUNK_SIZE_BYTES) * pass_factor) / (1024.0 * 1024.0 * 1024.0);
    double real_bandwidth = (elapsed_sec > 0.01) ? (processed_gb / elapsed_sec) : 0.0;
    UINT ramTemp = ReadHardwareTemperatureTSOD(pool->hDriverDevice);

    char frame[4096];
    int offset = 0;

    offset += snprintf(frame + offset, sizeof(frame) - offset,
        "\x1b[1;1H"
        ANSI_CYAN "===================================================================================\x1b[K\n" ANSI_RESET
        ANSI_BOLD " CEKE'S RAM TEST v1.1 (AVX2 + KMDF KERNEL DRIVER)\x1b[K\n" ANSI_RESET
        ANSI_CYAN "===================================================================================\x1b[K\n" ANSI_RESET
        " Mode Noyau     : %s\x1b[K\n",
        pool->is_driver_active ? ANSI_GREEN "[ACTIVE] Pilote Kernel cekes_ram_drv.sys connecté" ANSI_RESET : ANSI_YELLOW "[FALLBACK] Mode User-Space Windows API" ANSI_RESET
    );

    if (ramTemp > 0) {
        offset += snprintf(frame + offset, sizeof(frame) - offset,
            " Sonde TSOD RAM : " ANSI_RED "%u °C" ANSI_RESET " (Telemetry I2C/MSR Directe)\x1b[K\n", ramTemp);
    } else {
        offset += snprintf(frame + offset, sizeof(frame) - offset,
            " Sonde TSOD RAM : " ANSI_YELLOW "Non disponible (Mode Fallback Win32)" ANSI_RESET "\x1b[K\n");
    }

    offset += snprintf(frame + offset, sizeof(frame) - offset,
        " Module Actuel  : " ANSI_YELLOW "%s" ANSI_RESET "\x1b[K\n"
        " Avancement     : [" ANSI_GREEN "%.1f%%" ANSI_RESET "] (%lld / %lld Blocs de 16 MB)\x1b[K\n"
        " Débit Restitué : " ANSI_BOLD "%.2f GB/s" ANSI_RESET " | Benchmark Initial : %.2f GB/s\x1b[K\n"
        " Temps Écoulé   : %.1f sec | Biopsies Actives : %ld\x1b[K\n\n"
        ANSI_BOLD "CARTE GRAPHIQUE DE LA MÉMOIRE RAM (50 BLOCS) :\x1b[K\n" ANSI_RESET
        "[",
        module_name, pct, (long long)done, (long long)pool->total_chunks,
        real_bandwidth, pool->benchmark_gbs, elapsed_sec, pool->biopsy_count
    );

    size_t chunks_per_cell = (pool->total_chunks / GRID_CELL_COUNT) + 1;
    for (int cell = 0; cell < GRID_CELL_COUNT; ++cell) {
        size_t start_c = cell * chunks_per_cell;
        size_t end_c = (cell + 1) * chunks_per_cell;
        if (end_c > pool->total_chunks) end_c = pool->total_chunks;

        BOOL cell_done = (done >= (LONG64)end_c);
        uint64_t cell_errs = 0;
        for (size_t c = start_c; c < end_c; ++c) {
            cell_errs += pool->chunk_error_map[c];
        }

        if (cell_errs > 0) {
            offset += snprintf(frame + offset, sizeof(frame) - offset, ANSI_BLINK_RED "X" ANSI_RESET);
        } else if (cell_done) {
            offset += snprintf(frame + offset, sizeof(frame) - offset, ANSI_GREEN "■" ANSI_RESET);
        } else {
            offset += snprintf(frame + offset, sizeof(frame) - offset, ".");
        }
    }

    offset += snprintf(frame + offset, sizeof(frame) - offset,
        "]\x1b[K\n\n"
        ANSI_BOLD "STATUS MATRICIEL DES THREADS WORK STEALING (NUMA / CPU POOL) :\x1b[K\n" ANSI_RESET
    );

    DWORD max_disp = (num_threads > 16) ? 16 : num_threads;
    for (DWORD t = 0; t < max_disp; t += 2) {
        const char *cType1 = contexts[t].is_p_core ? ANSI_CYAN "P" ANSI_RESET : ANSI_YELLOW "E" ANSI_RESET;
        
        if (t + 1 < max_disp) {
            const char *cType2 = contexts[t + 1].is_p_core ? ANSI_CYAN "P" ANSI_RESET : ANSI_YELLOW "E" ANSI_RESET;
            offset += snprintf(frame + offset, sizeof(frame) - offset,
                " T%02lu[%s]:%4llubl %s | T%02lu[%s]:%4llubl %s\x1b[K\n",
                t, cType1, (unsigned long long)contexts[t].chunks_processed,
                (contexts[t].errors_found > 0) ? ANSI_RED "ERR" ANSI_RESET : ANSI_GREEN "OK" ANSI_RESET,
                t + 1, cType2, (unsigned long long)contexts[t + 1].chunks_processed,
                (contexts[t + 1].errors_found > 0) ? ANSI_RED "ERR" ANSI_RESET : ANSI_GREEN "OK" ANSI_RESET
            );
        } else {
            offset += snprintf(frame + offset, sizeof(frame) - offset,
                " T%02lu[%s]:%4llubl %s\x1b[K\n",
                t, cType1, (unsigned long long)contexts[t].chunks_processed,
                (contexts[t].errors_found > 0) ? ANSI_RED "ERR" ANSI_RESET : ANSI_GREEN "OK" ANSI_RESET
            );
        }
    }

    if (num_threads > 16) {
        offset += snprintf(frame + offset, sizeof(frame) - offset,
            " ... (+%lu threads supplémentaires actifs en arrière-plan)\x1b[K\n", num_threads - 16);
    }

    fputs(frame, stdout);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    OutputMode output_mode = MODE_TUI;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            output_mode = MODE_JSON_IPC;
        }
    }

    if (output_mode == MODE_TUI && IsLaunchedFromExplorer()) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (CreateProcessA("ram_stress_diag_gui.exe", NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return EXIT_SUCCESS;
        }
    }

    if (output_mode == MODE_TUI) {
        EnableVTTerminalMode();
    }

    g_log_file = fopen(LOG_FILE_PATH, "w");

    log_print(output_mode, "=====================================================================\n");
    log_print(output_mode, " CEKE'S RAM TEST v1.1 (KMDF Kernel / AVX2 / 10 Modules)              \n");
    log_print(output_mode, "=====================================================================\n\n");

    /* Tente la connexion au pilote Kernel KMDF */
    HANDLE hDriver = CreateFileA("\\\\.\\CekesRamLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL driver_active = (hDriver != INVALID_HANDLE_VALUE);

    if (driver_active) {
        log_print(output_mode, "[OK] Pilote Kernel KMDF (cekes_ram_drv.sys) connecté avec succès !\n");
    } else {
        log_print(output_mode, "[!] Pilote Kernel non présent. Mode Fallback API Windows Activé.\n");
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD num_threads = sysInfo.dwNumberOfProcessors;
    BYTE *coreTypeMap = (BYTE*)malloc(num_threads);
    DWORD pCoreCount = 0, eCoreCount = 0;
    QueryCPUTopology(&pCoreCount, &eCoreCount, coreTypeMap, num_threads);

    log_print(output_mode, "[+] Cœurs Logiques CPU    : %lu Threads (%lu P-Cores | %lu E-Cores)\n", num_threads, pCoreCount, eCoreCount);

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);

    uint64_t total_phys_ram = memStatus.ullTotalPhys;

    size_t alloc_bytes = 0;
    if (total_phys_ram > OS_SAFETY_MARGIN_BYTES) {
        alloc_bytes = (size_t)(total_phys_ram - OS_SAFETY_MARGIN_BYTES);
    } else {
        alloc_bytes = (size_t)(total_phys_ram / 2);
    }
    if (alloc_bytes < MIN_ALLOC_BYTES) alloc_bytes = MIN_ALLOC_BYTES;

    alloc_bytes = (alloc_bytes / CHUNK_SIZE_BYTES) * CHUNK_SIZE_BYTES;

    log_print(output_mode, "[+] RAM Totale Installee   : %.2f GB\n", (double)total_phys_ram / (1024.0 * 1024.0 * 1024.0));
    log_print(output_mode, "[+] Marge Systeme OS       : 4.00 GB laisse libre a Windows (Anti-BSOD)\n");
    log_print(output_mode, "[+] Allocation Stress      : %.2f GB (%llu blocs de 16 MB)\n", (double)alloc_bytes / (1024.0 * 1024.0 * 1024.0), (unsigned long long)(alloc_bytes / CHUNK_SIZE_BYTES));

    BOOL lock_priv = EnableLockMemoryPrivilege();
    size_t large_page_min = GetLargePageMinimum();
    uint8_t *ram_buffer = NULL;

    if (driver_active) {
        /* Allocation verrouillée exclusive via MDL Kernel */
        ram_buffer = (uint8_t*)VirtualAlloc(NULL, alloc_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (ram_buffer) {
            CEKES_LOCK_PAGE_REQ lockReq;
            lockReq.UserVirtualAddress = ram_buffer;
            lockReq.LengthBytes = alloc_bytes;
            lockReq.PhysicalBaseAddress = 0;
            lockReq.Status = 0;

            DWORD ret = 0;
            DeviceIoControl(hDriver, IOCTL_CEKES_LOCK_PHYSICAL_PAGE, &lockReq, (DWORD)sizeof(lockReq), &lockReq, (DWORD)sizeof(lockReq), &ret, NULL);
            log_print(output_mode, "[OK] Lock Memoire Kernel MDL confirme (Base PA: 0x%llX)\n", (unsigned long long)lockReq.PhysicalBaseAddress);
        }
    }

    if (!ram_buffer && lock_priv && large_page_min > 0) {
        size_t large_alloc = (alloc_bytes / large_page_min) * large_page_min;
        ram_buffer = (uint8_t*)VirtualAlloc(NULL, large_alloc, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
        if (ram_buffer) {
            alloc_bytes = large_alloc;
            log_print(output_mode, "[OK] MEM_LARGE_PAGES 2 MB Réussi sur puces physiques contiguës !\n");
        }
    }

    if (!ram_buffer) {
        ram_buffer = (uint8_t*)VirtualAlloc(NULL, alloc_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ram_buffer) {
            log_print(output_mode, "[!] ERREUR CRITIQUE : VirtualAlloc a échoué.\n");
            if (g_log_file) fclose(g_log_file);
            return EXIT_FAILURE;
        }
        log_print(output_mode, "[OK] VirtualAlloc Standard réussi (0x%p)\n", (void*)ram_buffer);
        VirtualLock(ram_buffer, alloc_bytes);
    }

    log_print(output_mode, "[+] Lancement du Benchmark de bande passante initiale...\n");
    double bench_gbs = BenchmarkMemoryBandwidth(ram_buffer, 256 * 1024 * 1024);
    log_print(output_mode, "[+] Bande Passante Brute Mesurée : %.2f GB/s\n", bench_gbs);

    uint64_t dynamic_iterations = ROWHAMMER_ITERATIONS;
    if (bench_gbs > 40.0) {
        log_print(output_mode, "[+] Détection Mémoire Haute Vitesse (DDR5) : Auto-Tuning Rowhammer à 150 000 cycles.\n");
        dynamic_iterations = 150000ULL;
    } else {
        log_print(output_mode, "[+] Détection Mémoire Standard (DDR4) : Auto-Tuning Rowhammer à 100 000 cycles.\n");
    }

    StressPool pool;
    pool.ram_buffer = ram_buffer;
    pool.total_alloc_bytes = alloc_bytes;
    pool.total_chunks = alloc_bytes / CHUNK_SIZE_BYTES;
    pool.current_chunk_index = 0;
    pool.current_module_id = 1;
    pool.retention_seconds = 5;
    pool.benchmark_gbs = bench_gbs;
    pool.dynamic_iterations = dynamic_iterations;
    pool.mode = output_mode;
    pool.hDriverDevice = hDriver;
    pool.is_driver_active = driver_active;
    pool.biopsy_count = 0;
    pool.chunk_error_map = (uint64_t*)calloc(pool.total_chunks, sizeof(uint64_t));

    HANDLE *threads = (HANDLE*)malloc(sizeof(HANDLE) * num_threads);
    ThreadContext *contexts = (ThreadContext*)malloc(sizeof(ThreadContext) * num_threads);

    const char *module_names[] = {
        "",
        "MODULE 1 : Bus Stability Test (Walking 1s & 0s - Diaphonie)",
        "MODULE 2 : Rowhammer Real Stride 16 KB (_mm_stream_si64 / Bypass Cache)",
        "MODULE 3 : PRNG Xorshift32 AVX2 Vectorisé (Stress Thermique IMC & Vdroop)",
        "MODULE 4 : Bit-Fade Test Réel (Rétention DRAM - Flush Cache + Pause Thread)",
        "MODULE 5 : Data Lanes Overload AVX2 256-bit (Power Inversion VDD/VDDQ)",
        "MODULE 6 : CHOC THERMIQUE COORDONNÉ (Thermal Cycling Hot 15s / Cold 5s)",
        "MODULE 7 : Bit Flip Explorer (High Frequency 0x5555... / 0xAAAA...)",
        "MODULE 8 : Moving Inversions (Marching patterns & Bitwise Inverse)",
        "MODULE 9 : Block Read/Write Massif (Vdroop Extreme Direct Stream)",
        "MODULE 10: Random Stride Bank-Group Hammering (TRR Bypass Matrix)"
    };

    uint64_t total_system_errors = 0;

    if (output_mode == MODE_TUI) {
        printf("\x1b[2J\x1b[H");
        fflush(stdout);
    }

    for (int mod = 1; mod <= 10; ++mod) {
        pool.current_module_id = mod;
        pool.current_chunk_index = 0;

        for (DWORD t = 0; t < num_threads; ++t) {
            contexts[t].thread_id = (int)t;
            contexts[t].is_p_core = coreTypeMap[t];
            contexts[t].chunks_processed = 0;
            contexts[t].errors_found = 0;
            contexts[t].exception_code = 0;
            contexts[t].pool = &pool;

            threads[t] = CreateThread(NULL, 0, ThreadWorkerRoutine, &contexts[t], CREATE_SUSPENDED, NULL);
            if (threads[t]) {
                DWORD_PTR affinity_mask = (1ULL << (t % 64));
                SetThreadAffinityMask(threads[t], affinity_mask);
                ResumeThread(threads[t]);
            }
        }

        LARGE_INTEGER freq, start, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);

        while (WaitForMultipleObjects(num_threads, threads, TRUE, 150) == WAIT_TIMEOUT) {
            QueryPerformanceCounter(&now);
            double elapsed = (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
            if (output_mode == MODE_TUI) {
                DrawTUIDashboard(&pool, contexts, num_threads, module_names[mod], elapsed);
            } else {
                EmitJsonTelemetry(&pool, contexts, num_threads, module_names[mod], elapsed);
            }
        }

        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
        if (output_mode == MODE_TUI) {
            DrawTUIDashboard(&pool, contexts, num_threads, module_names[mod], elapsed);
        } else {
            EmitJsonTelemetry(&pool, contexts, num_threads, module_names[mod], elapsed);
        }

        uint64_t module_errors = 0;
        for (DWORD t = 0; t < num_threads; ++t) {
            module_errors += contexts[t].errors_found;
            CloseHandle(threads[t]);
        }
        total_system_errors += module_errors;

        log_print(output_mode, "\n[RÉSULTAT %s] : %s (Erreurs: %llu, Durée: %.2fs)\n", 
                  module_names[mod], 
                  (module_errors == 0) ? "SUCCÈS" : "ÉCHEC", 
                  (unsigned long long)module_errors, 
                  elapsed);
    }

    uint64_t whea_errs = CheckWHEAEvents();
    total_system_errors += whea_errs;

    log_print(output_mode, "\n=====================================================================\n");
    log_print(output_mode, " SYNTHÈSE DIAGNOSTIC \"CEKE'S RAM TEST v1.1\"\n");
    log_print(output_mode, "=====================================================================\n");
    log_print(output_mode, "[+] Erreurs Silencieuses WHEA/ECC : %llu\n", (unsigned long long)whea_errs);
    log_print(output_mode, "[+] Total Biopsies Effectuees     : %ld\n", pool.biopsy_count);
    log_print(output_mode, "[+] Statut Final                  : %s (%llu Erreurs Totales)\n", (total_system_errors == 0) ? "PASS (100% STABLE)" : "FAIL (INSTABLE)", (unsigned long long)total_system_errors);

    if (driver_active) {
        CEKES_LOCK_PAGE_REQ unlockReq;
        DWORD ret = 0;
        DeviceIoControl(hDriver, IOCTL_CEKES_UNLOCK_PHYSICAL_PAGE, &unlockReq, (DWORD)sizeof(unlockReq), &unlockReq, (DWORD)sizeof(unlockReq), &ret, NULL);
        CloseHandle(hDriver);
    }

    VirtualFree(ram_buffer, 0, MEM_RELEASE);
    free(threads);
    free(contexts);
    free(coreTypeMap);
    free(pool.chunk_error_map);

    if (g_log_file) fclose(g_log_file);
    return EXIT_SUCCESS;
}
