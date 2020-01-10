#ifndef __VMRP_BRIDGE_H__
#define __VMRP_BRIDGE_H__

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "../windows/include/unicorn/unicorn.h"
#else
#include <unicorn/unicorn.h>
#endif

#include "mr_helper.h"
#include "utils.h"

#define MR_TABLE_SIZE 0x248
#define MR_C_FUNCTION_SIZE 0x14
#define MRC_EXTCHUNK_SIZE 0x34

typedef struct BridgeMap BridgeMap;

typedef bool (*BridgeCB)(struct BridgeMap *o, uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data);

typedef enum BridgeMapType {
    MAP_DATA,
    MAP_FUNC,
} BridgeMapType;

typedef struct BridgeMap {
    char *name;
    uint32_t pos;
    uint32_t size;
    BridgeMapType type;
    BridgeCB fn;
} BridgeMap;

#define BRIDGE_FUNC_MAP(offset, size, mapType, field, func) \
    { #field, offset, size, mapType, func }

bool bridge_exec(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data);
uc_err bridge_init(uc_engine *uc, uint32_t startAddress);

#endif
