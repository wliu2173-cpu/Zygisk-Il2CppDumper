#include <jni.h>
#include <string>
#include <cstring>
#include <android/log.h>
#include "zygisk.hpp"

#define LOG_TAG "WwiseKeyHook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ============ Dobby 函数指针声明 ============
typedef void* (*dlopen_t)(const char* filename, int flag);
static dlopen_t orig_dlopen = nullptr;

typedef void* (*dlsym_t)(void* handle, const char* symbol);
static dlsym_t orig_dlsym = nullptr;

// ============ Hook EVP_DecryptInit_ex ============
typedef int (*EVP_DecryptInit_ex_t)(void* ctx, void* type, void* impl,
                                     const unsigned char* key, const unsigned char* iv);
static EVP_DecryptInit_ex_t orig_EVP_DecryptInit_ex = nullptr;

static int hook_EVP_DecryptInit_ex(void* ctx, void* type, void* impl,
                                    const unsigned char* key, const unsigned char* iv) {
    if (key != nullptr) {
        char key_hex[64] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(key_hex + i * 2, "%02x", key[i]);
        }
        LOGI("===== AES KEY FOUND =====");
        LOGI("KEY: %s", key_hex);
    }
    if (iv != nullptr) {
        char iv_hex[64] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(iv_hex + i * 2, "%02x", iv[i]);
        }
        LOGI("IV:  %s", iv_hex);
    }
    LOGI("=========================");

    return orig_EVP_DecryptInit_ex(ctx, type, impl, key, iv);
}

static void hookCrypto() {
    void* crypto = orig_dlopen("libcrypto.so", RTLD_NOW);
    if (crypto == nullptr) {
        LOGE("libcrypto.so 加载失败");
        return;
    }

    void* func = orig_dlsym(crypto, "EVP_DecryptInit_ex");
    if (func == nullptr) {
        func = orig_dlsym(crypto, "EVP_CipherInit_ex");
    }
    if (func == nullptr) {
        LOGE("找不到 EVP_DecryptInit_ex");
        return;
    }

    LOGI("EVP_DecryptInit_ex 地址: %p", func);
    extern int DobbyHook(void* target, void* replace, void** orig);
    int ret = DobbyHook(func, (void*)hook_EVP_DecryptInit_ex, (void**)&orig_EVP_DecryptInit_ex);
    if (ret == 0) {
        LOGI("EVP_DecryptInit_ex Hook 成功!");
    } else {
        LOGE("DobbyHook 失败, ret=%d", ret);
    }
}

static void* hook_dlopen(const char* filename, int flag) {
    void* handle = orig_dlopen(filename, flag);
    if (filename != nullptr && strstr(filename, "libAkSoundEngine.so")) {
        LOGI("libAkSoundEngine.so 已加载, 开始 Hook 加密函数");
        hookCrypto();
    }
    return handle;
}

static void* hook_dlsym(void* handle, const char* symbol) {
    void* addr = orig_dlsym(handle, symbol);
    if (symbol != nullptr && strcmp(symbol, "EVP_DecryptInit_ex") == 0) {
        LOGI("dlsym 拿到 EVP_DecryptInit_ex = %p", addr);
        if (orig_EVP_DecryptInit_ex == nullptr && addr != nullptr) {
            extern int DobbyHook(void* target, void* replace, void** orig);
            DobbyHook(addr, (void*)hook_EVP_DecryptInit_ex, (void**)&orig_EVP_DecryptInit_ex);
            LOGI("通过 dlsym Hook 成功");
        }
    }
    return addr;
}

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        // 占位符，github action sed 自动替换
        if (strcmp(pkg, "{{PACKAGE_NAME}}") != 0) {
            env->ReleaseStringUTFChars(args->nice_name, pkg);
            return;
        }
        env->ReleaseStringUTFChars(args->nice_name, pkg);
        LOGI("=== 目标游戏进程, 初始化 Hook ===");

        extern void* dlsym(void* handle, const char* symbol);
        extern void* dlopen(const char* filename, int flag);

        void* dlopen_sym = dlsym(RTLD_DEFAULT, "dlopen");
        void* dlsym_sym = dlsym(RTLD_DEFAULT, "dlsym");

        if (dlopen_sym != nullptr && dlsym_sym != nullptr) {
            extern int DobbyHook(void* target, void* replace, void** orig);
            DobbyHook(dlopen_sym, (void*)hook_dlopen, (void**)&orig_dlopen);
            DobbyHook(dlsym_sym, (void*)hook_dlsym, (void**)&orig_dlsym);
            LOGI("dlopen/dlsym Hook 完成");
        } else {
            LOGE("找不到 dlopen/dlsym");
        }
    }

private:
    zygisk::Api* api = nullptr;
    JNIEnv* env = nullptr;
};

REGISTER_ZYGISK_MODULE(MyModule);
