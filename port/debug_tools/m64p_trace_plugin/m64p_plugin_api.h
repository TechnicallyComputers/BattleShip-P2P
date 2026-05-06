/**
 * m64p_plugin_api.h — Minimal Mupen64Plus plugin API definitions
 *
 * Extracted from the Mupen64Plus-Core project (GPLv2).
 * Types and function signatures needed by GFX and RSP trace plugins.
 *
 * Full API reference: https://github.com/mupen64plus/mupen64plus-core
 */
#ifndef M64P_PLUGIN_API_H
#define M64P_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/*  Types                                                                    */
/* ========================================================================= */

typedef void * m64p_dynlib_handle;
typedef void (*m64p_debug_callback)(void *context, int level, const char *message);

typedef enum {
	M64ERR_SUCCESS = 0,
	M64ERR_NOT_INIT,
	M64ERR_ALREADY_INIT,
	M64ERR_INCOMPATIBLE,
	M64ERR_INPUT_ASSERT,
	M64ERR_INPUT_INVALID,
	M64ERR_INPUT_NOT_FOUND,
	M64ERR_NO_MEMORY,
	M64ERR_FILES,
	M64ERR_INTERNAL,
	M64ERR_INVALID_STATE,
	M64ERR_PLUGIN_FAIL,
	M64ERR_SYSTEM_FAIL,
	M64ERR_UNSUPPORTED,
	M64ERR_WRONG_TYPE
} m64p_error;

typedef enum {
	M64PLUGIN_NULL = 0,
	M64PLUGIN_RSP = 1,
	M64PLUGIN_GFX = 2,
	M64PLUGIN_AUDIO = 3,
	M64PLUGIN_INPUT = 4,
	M64PLUGIN_CORE = 5
} m64p_plugin_type;

/* ========================================================================= */
/*  GFX Plugin Info Structure                                                */
/* ========================================================================= */

typedef struct {
	uint8_t  *RDRAM;
	uint8_t  *DMEM;
	uint8_t  *IMEM;

	uint32_t *MI_INTR_REG;

	uint32_t *DPC_START_REG;
	uint32_t *DPC_END_REG;
	uint32_t *DPC_CURRENT_REG;
	uint32_t *DPC_STATUS_REG;
	uint32_t *DPC_CLOCK_REG;
	uint32_t *DPC_BUFBUSY_REG;
	uint32_t *DPC_PIPEBUSY_REG;
	uint32_t *DPC_TMEM_REG;

	uint32_t *VI_STATUS_REG;
	uint32_t *VI_ORIGIN_REG;
	uint32_t *VI_WIDTH_REG;
	uint32_t *VI_INTR_REG;
	uint32_t *VI_V_CURRENT_LINE_REG;
	uint32_t *VI_TIMING_REG;
	uint32_t *VI_V_SYNC_REG;
	uint32_t *VI_H_SYNC_REG;
	uint32_t *VI_LEAP_REG;
	uint32_t *VI_H_START_REG;
	uint32_t *VI_V_START_REG;
	uint32_t *VI_V_BURST_REG;
	uint32_t *VI_X_SCALE_REG;
	uint32_t *VI_Y_SCALE_REG;

	uint32_t *SP_STATUS_REG;

	void (*CheckInterrupts)(void);
} GFX_INFO;

/* ========================================================================= */
/*  RSP Plugin Info Structure                                                */
/* ========================================================================= */

/**
 * RSP_INFO — layout reverse-engineered from the M64P 2.6.0 x64 binary.
 *
 * This M64P build does NOT have hInst/MemoryBswaped at the start of the struct.
 * The struct begins directly with the memory pointers. Two extra fields appear
 * at the end (possibly hInst/MemoryBswaped relocated, or version-specific).
 *
 * Layout verified by dumping raw struct bytes from InitiateRSP on 2026-04-07.
 */
typedef struct {
	uint8_t  *RDRAM;               /* +0:   heap ptr to 8MB RDRAM */
	uint8_t  *DMEM;                /* +8:   heap ptr to 4KB DMEM */
	uint8_t  *IMEM;                /* +16:  heap ptr to 4KB IMEM (= DMEM+0x1000) */

	uint32_t *MI_INTR_REG;         /* +24 */

	uint32_t *SP_MEM_ADDR_REG;     /* +32 */
	uint32_t *SP_DRAM_ADDR_REG;    /* +40 */
	uint32_t *SP_RD_LEN_REG;       /* +48 */
	uint32_t *SP_WR_LEN_REG;       /* +56 */
	uint32_t *SP_STATUS_REG;       /* +64 */
	uint32_t *SP_DMA_FULL_REG;     /* +72 */
	uint32_t *SP_DMA_BUSY_REG;     /* +80 */
	uint32_t *SP_PC_REG;           /* +88 */
	uint32_t *SP_SEMAPHORE_REG;    /* +96 */

	uint32_t *DPC_START_REG;       /* +104 */
	uint32_t *DPC_END_REG;         /* +112 */
	uint32_t *DPC_CURRENT_REG;     /* +120 */
	uint32_t *DPC_STATUS_REG;      /* +128 */
	uint32_t *DPC_CLOCK_REG;       /* +136 */
	uint32_t *DPC_BUFBUSY_REG;     /* +144 */
	uint32_t *DPC_PIPEBUSY_REG;    /* +152 */
	uint32_t *DPC_TMEM_REG;        /* +160 */

	void (*CheckInterrupts)(void); /* +168 */
	void (*ProcessDList)(void);    /* +176 */
	void (*ProcessAList)(void);    /* +184 */
	void (*ProcessRdpList)(void);  /* +192 */
	void (*ShowCFB)(void);         /* +200 */

	uint64_t  _reserved0;          /* +208: observed value=1 */
	void     *_reserved1;          /* +216: observed=pointer */
} RSP_INFO;

/* ========================================================================= */
/*  OSTask in DMEM — absolute offsets used by M64P RSP HLE                   */
/* ========================================================================= */

#define TASK_TYPE             0xFC0
#define TASK_FLAGS            0xFC4
#define TASK_UCODE_BOOT       0xFC8
#define TASK_UCODE_BOOT_SIZE  0xFCC
#define TASK_UCODE            0xFD0
#define TASK_UCODE_SIZE       0xFD4
#define TASK_UCODE_DATA       0xFD8
#define TASK_UCODE_DATA_SIZE  0xFDC
#define TASK_DRAM_STACK       0xFE0
#define TASK_DRAM_STACK_SIZE  0xFE4
#define TASK_OUTPUT_BUFF      0xFE8
#define TASK_OUTPUT_BUFF_SIZE 0xFEC
#define TASK_DATA_PTR         0xFF0
#define TASK_DATA_SIZE        0xFF4
#define TASK_YIELD_DATA_PTR   0xFF8
#define TASK_YIELD_DATA_SIZE  0xFFC

#define M_GFXTASK  1
#define M_AUDTASK  2

/* SP Status register bits */
#define SP_STATUS_HALT        0x0001
#define SP_STATUS_BROKE       0x0002
#define SP_STATUS_TASKDONE    0x0200
#define SP_STATUS_SIG2        0x0200  /* same as TASKDONE on N64 */

/* ========================================================================= */
/*  Plugin export macros                                                     */
/* ========================================================================= */

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define CALL   __cdecl
#else
#define EXPORT __attribute__((visibility("default")))
#define CALL
#endif

#ifdef __cplusplus
}
#endif

#endif /* M64P_PLUGIN_API_H */
