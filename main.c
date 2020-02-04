#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "./windows/include/unicorn/unicorn.h"
#else
#include <unicorn/unicorn.h>
#endif

#include "./header/bridge.h"
#include "./header/debug.h"
#include "./header/fileLib.h"
#include "./header/main.h"
#include "./header/memory.h"
#include "./header/mr_helper.h"
#include "./header/utils.h"

// #define MRPFILE "mr.mrp"
#define MRPFILE "asm.mrp"

static void writeFile(const char *filename, void *data, uint32 length) {
    int fh = mr_open(filename, MR_FILE_CREATE | MR_FILE_RDWR);
    mr_write(fh, data, length);
    mr_close(fh);
}

int extractFile() {
    // char *filename = "start.mr";
    char *filename = "cfunction.ext";
    // char *filename = "game.ext";
    int32 offset, length;
    uint8 *data;
    int32 ret = readMrpFileEx(MRPFILE, filename, &offset, &length, &data);
    if (ret == MR_SUCCESS) {
        LOG("red suc: offset:%d, length:%d", offset, length);
        writeFile(filename, data, length);
    } else {
        LOG("red failed");
    }

    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////

static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    printf(">>> Tracing basic block at 0x%" PRIx64 ", block size = 0x%x\n", address, size);
}

static void hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    hook_code_debug(uc, address);
    if (address >= BRIDGE_TABLE_ADDRESS && address <= BRIDGE_TABLE_ADDRESS + BRIDGE_TABLE_SIZE) {
        bridge(uc, UC_MEM_FETCH, address);
    }
}

static void hook_mem_valid(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data) {
    printf(">>> Tracing mem_valid mem_type:%s at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64 "\n",
           memTypeStr(type), address, size, value);
}

static bool hook_mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data) {
    printf(">>> Tracing mem_invalid mem_type:%s at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64 "\n",
           memTypeStr(type), address, size, value);
    return false;
}

static int32_t loadCode(uc_engine *uc) {
    char *filename = "cfunction.ext";
    uint32_t value, length;
    uint8_t *code;
    int32_t ret = readMrpFileEx(MRPFILE, filename, (int32 *)&value, (int32 *)&length, &code);
    if (ret == MR_FAILED) {
        LOG("load %s failed", filename);
        return ret;
    }
    LOG("load %s suc: offset:%d, length:%d", filename, value, length);

    uc_mem_write(uc, CODE_ADDRESS, code, length);
    free(code);
    return ret;
}

static bool mem_init(uc_engine *uc) {
    uc_err err = uc_mem_map(uc, CODE_ADDRESS, CODE_SIZE, UC_PROT_ALL);
    if (err) {
        printf("Failed mem map CODE_ADDRESS: %u (%s)\n", err, uc_strerror(err));
        return false;
    }

    if (loadCode(uc) == MR_FAILED) {
        return false;
    }

    err = uc_mem_map(uc, STACK_ADDRESS, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    if (err) {
        printf("Failed mem map STACK_ADDRESS: %u (%s)\n", err, uc_strerror(err));
        return false;
    }

    err = uc_mem_map(uc, MEMORY_MANAGER_ADDRESS, MEMORY_MANAGER_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    if (err) {
        printf("Failed mem map MEMORY_MANAGER_ADDRESS: %u (%s)\n", err, uc_strerror(err));
        return err;
    }
    initMemoryManager(MEMORY_MANAGER_ADDRESS, MEMORY_MANAGER_SIZE);

    // unicorn存在BUG，UC_HOOK_MEM_INVALID只能拦截第一次UC_MEM_FETCH_PROT，所以干脆设置成可执行，统一在UC_HOOK_CODE事件中处理
    // err = uc_mem_map(uc, BRIDGE_TABLE_ADDRESS, BRIDGE_TABLE_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    err = uc_mem_map(uc, BRIDGE_TABLE_ADDRESS, BRIDGE_TABLE_SIZE, UC_PROT_ALL);
    if (err) {
        printf("Failed mem map BRIDGE_TABLE_ADDRESS: %u (%s)\n", err, uc_strerror(err));
        return err;
    }
    err = bridge_init(uc, CODE_ADDRESS, BRIDGE_TABLE_ADDRESS);
    if (err) {
        printf("Failed bridge_init(): %u (%s)\n", err, uc_strerror(err));
        return err;
    }

    return true;
}

static void emu() {
    uc_engine *uc;
    uc_err err;
    uc_hook trace;

    printf(">>> CODE_ADDRESS:0x%X, STACK_ADDRESS:0x%X, BRIDGE_TABLE_ADDRESS:0x%X\n", CODE_ADDRESS, STACK_ADDRESS, BRIDGE_TABLE_ADDRESS);

    err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc);
    if (err) {
        printf("Failed on uc_open() with error returned: %u (%s)\n", err, uc_strerror(err));
        return;
    }
    if (!mem_init(uc)) {
        printf("mem_init() fail\n");
        goto end;
    }

    uc_hook_add(uc, &trace, UC_HOOK_BLOCK, hook_block, NULL, 1, 0);
    uc_hook_add(uc, &trace, UC_HOOK_CODE, hook_code, NULL, 1, 0);
    uc_hook_add(uc, &trace, UC_HOOK_MEM_INVALID, hook_mem_invalid, NULL, 1, 0);
    uc_hook_add(uc, &trace, UC_HOOK_MEM_VALID, hook_mem_valid, NULL, 1, 0);

    uint32_t value = STACK_ADDRESS + STACK_SIZE;  // 满递减
    uc_reg_write(uc, UC_ARM_REG_SP, &value);

    value = 1;
    uc_reg_write(uc, UC_ARM_REG_R0, &value);  // 传参数值1
    runCode(uc, CODE_ADDRESS + 8, STOP_ADDRESS, false);

    printf("\n ----------------------------init done.--------------------------------------- \n");

    bridge_mr_init(uc);
end:
    uc_close(uc);
}

int main() {
    // mr_stop();
    // mr_event(MR_MOUSE_DOWN, x, y);
    listMrpFiles(MRPFILE);
    // extractFile();
    // mr_start_dsm(MRPFILE);

    // printf("thumb:\n");
    // emu(TRUE);
    printf("arm:\n");
    emu(FALSE);
    return 0;
}