//
// Created by Perfare on 2020/7/4.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;
static bool g_api_ready = false;   // 新增：必需 API 是否就绪

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

// 新增：集中检查 dump 必需的 API，任何一个为空都直接退出，避免 pc=0 崩溃
static bool check_required_apis() {
    bool ok = true;
#define REQ(name) do { if (!(name)) { LOGE("required api missing: %s", #name); ok = false; } } while (0)
    // 初始化阶段必需
    REQ(il2cpp_domain_get);
    REQ(il2cpp_domain_get_assemblies);
    REQ(il2cpp_thread_attach);
    REQ(il2cpp_is_vm_thread);
    REQ(il2cpp_assembly_get_image);
    REQ(il2cpp_image_get_name);
    // dump 阶段必需
    REQ(il2cpp_class_from_type);
    REQ(il2cpp_class_get_type);
    REQ(il2cpp_class_get_name);
    REQ(il2cpp_class_get_namespace);
    REQ(il2cpp_class_get_flags);
    REQ(il2cpp_class_is_valuetype);
    REQ(il2cpp_class_is_enum);
    REQ(il2cpp_class_get_parent);
    REQ(il2cpp_class_get_interfaces);
    REQ(il2cpp_class_get_fields);
    REQ(il2cpp_class_get_methods);
    REQ(il2cpp_class_get_properties);
    REQ(il2cpp_class_get_method_from_name);
#undef REQ
    return ok;
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    if (!type) return false;                 // 新增：防御空指针
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    if (!klass) return outPut.str();         // 新增
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (!method) break;
        //TODO attribute
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer - il2cpp_base;
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        outPut << "\n\t";
        uint32_t iflags = 0;
        uint32_t flags = il2cpp_method_get_flags ? il2cpp_method_get_flags(method, &iflags) : 0;   // 新增判空
        outPut << get_method_modifier(flags);
        //TODO genericContainerIndex
        auto return_type = il2cpp_method_get_return_type ? il2cpp_method_get_return_type(method) : nullptr;
        if (return_type && _il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        auto return_class = return_type ? il2cpp_class_from_type(return_type) : nullptr;
        outPut << (return_class && il2cpp_class_get_name ? il2cpp_class_get_name(return_class) : "?") << " "
               << (il2cpp_method_get_name ? il2cpp_method_get_name(method) : "?") << "(";
        auto param_count = il2cpp_method_get_param_count ? il2cpp_method_get_param_count(method) : 0;
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param ? il2cpp_method_get_param(method, i) : nullptr;
            if (!param) continue;
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            auto parameter_class = il2cpp_class_from_type(param);
            outPut << (parameter_class && il2cpp_class_get_name ? il2cpp_class_get_name(parameter_class) : "?") << " "
                   << (il2cpp_method_get_param_name ? il2cpp_method_get_param_name(method, i) : "?");
            outPut << ", ";
        }
        if (param_count > 0) {
            outPut.seekp(-2, outPut.cur);
        }
        outPut << ") { }\n";
        //TODO GenericInstMethod
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    if (!klass) return outPut.str();         // 新增
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        //TODO attribute
        auto prop = const_cast<PropertyInfo *>(prop_const);
        if (!prop) break;
        auto get = il2cpp_property_get_get_method ? il2cpp_property_get_get_method(prop) : nullptr;
        auto set = il2cpp_property_get_set_method ? il2cpp_property_get_set_method(prop) : nullptr;
        auto prop_name = il2cpp_property_get_name ? il2cpp_property_get_name(prop) : nullptr;
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags ? il2cpp_method_get_flags(get, &iflags) : 0);
            auto rt = il2cpp_method_get_return_type ? il2cpp_method_get_return_type(get) : nullptr;
            prop_class = rt ? il2cpp_class_from_type(rt) : nullptr;
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags ? il2cpp_method_get_flags(set, &iflags) : 0);
            auto param = il2cpp_method_get_param ? il2cpp_method_get_param(set, 0) : nullptr;
            prop_class = param ? il2cpp_class_from_type(param) : nullptr;
        }
        if (prop_class && il2cpp_class_get_name) {
            outPut << il2cpp_class_get_name(prop_class) << " " << (prop_name ? prop_name : "?") << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name;
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    if (!klass) return outPut.str();         // 新增
    auto is_enum = il2cpp_class_is_enum ? il2cpp_class_is_enum(klass) : false;
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        if (!field) break;
        //TODO attribute
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags ? il2cpp_field_get_flags(field) : 0;
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type ? il2cpp_field_get_type(field) : nullptr;
        auto field_class = field_type ? il2cpp_class_from_type(field_type) : nullptr;
        outPut << (field_class && il2cpp_class_get_name ? il2cpp_class_get_name(field_class) : "?") << " "
               << (il2cpp_field_get_name ? il2cpp_field_get_name(field) : "?");
        //TODO 获取构造函数初始化后的字段值
        if ((attrs & FIELD_ATTRIBUTE_LITERAL) && is_enum && il2cpp_field_static_get_value) {
            uint64_t val = 0;
            il2cpp_field_static_get_value(field, &val);
            outPut << " = " << std::dec << val;
        }
        outPut << "; // 0x" << std::hex
               << (il2cpp_field_get_offset ? il2cpp_field_get_offset(field) : 0) << "\n";
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    if (!type) {
        outPut << "\n// <null type>\n";
        return outPut.str();
    }
    auto *klass = il2cpp_class_from_type ? il2cpp_class_from_type(type) : nullptr;
    if (!klass) {
        outPut << "\n// <null klass>\n";
        return outPut.str();
    }
    outPut << "\n// Namespace: "
           << (il2cpp_class_get_namespace ? il2cpp_class_get_namespace(klass) : "") << "\n";
    auto flags = il2cpp_class_get_flags ? il2cpp_class_get_flags(klass) : 0;
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    //TODO attribute
    auto is_valuetype = il2cpp_class_is_valuetype ? il2cpp_class_is_valuetype(klass) : false;
    auto is_enum = il2cpp_class_is_enum ? il2cpp_class_is_enum(klass) : false;
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && (flags & TYPE_ATTRIBUTE_SEALED)) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << (il2cpp_class_get_name ? il2cpp_class_get_name(klass) : "?"); //TODO genericContainerIndex
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent ? il2cpp_class_get_parent(klass) : nullptr;
    if (!is_valuetype && !is_enum && parent && il2cpp_class_get_type) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type && parent_type->type != IL2CPP_TYPE_OBJECT) {
            extends.emplace_back(il2cpp_class_get_name ? il2cpp_class_get_name(parent) : "?");
        }
    }
    void *iter = nullptr;
    if (il2cpp_class_get_interfaces) {
        while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
            extends.emplace_back(il2cpp_class_get_name ? il2cpp_class_get_name(itf) : "?");
        }
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (size_t i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    //TODO EventInfo
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);

    // 新增：集中检查必需 API，缺失则直接返回，杜绝后续空指针调用
    if (!check_required_apis()) {
        LOGE("il2cpp_api_init failed: missing required APIs");
        g_api_ready = false;
        return;
    }
    g_api_ready = true;

    Dl_info dlInfo;
    if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
        il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
    }
    LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);

    while (!il2cpp_is_vm_thread(nullptr)) {
        LOGI("Waiting for il2cpp_init...");
        sleep(1);
    }
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get returned null");
        g_api_ready = false;
        return;
    }
    il2cpp_thread_attach(domain);
}

void il2cpp_dump(const char *outDir) {
    // 新增：先确认 API 就绪
    if (!g_api_ready) {
        LOGE("il2cpp_dump: API not ready, skip");
        return;
    }
    LOGI("dumping...");
    size_t size = 0;
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_dump: domain null");
        return;
    }
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies) {
        LOGE("il2cpp_dump: assemblies null");
        return;
    }
    std::stringstream imageOutput;
    for (size_t i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        if (!image) continue;
        imageOutput << "// Image " << i << ": " << il2cpp_image_get_name(image) << "\n";
    }
    std::vector<std::string> outPuts;
    if (il2cpp_image_get_class && il2cpp_image_get_class_count) {
        LOGI("Version greater than 2018.3");
        //使用il2cpp_image_get_class
        for (size_t i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) continue;
            std::stringstream imageStr;
            imageStr << "\n// Dll : " << il2cpp_image_get_name(image);
            auto classCount = il2cpp_image_get_class_count(image);
            for (size_t j = 0; j < classCount; ++j) {
                auto klass = il2cpp_image_get_class(image, j);
                if (!klass) continue;
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                //LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    } else {
        LOGI("Version less than 2018.3");
        //使用反射
        auto corlib = il2cpp_get_corlib ? il2cpp_get_corlib() : nullptr;
        if (!corlib) {
            LOGE("il2cpp_dump: corlib null");
            return;
        }
        auto assemblyClass = il2cpp_class_from_name(corlib, "System.Reflection", "Assembly");
        auto assemblyLoad = assemblyClass ? il2cpp_class_get_method_from_name(assemblyClass, "Load", 1) : nullptr;
        auto assemblyGetTypes = assemblyClass ? il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0) : nullptr;
        if (assemblyLoad && assemblyLoad->methodPointer) {
            LOGI("Assembly::Load: %p", assemblyLoad->methodPointer);
        } else {
            LOGI("miss Assembly::Load");
            return;
        }
        if (assemblyGetTypes && assemblyGetTypes->methodPointer) {
            LOGI("Assembly::GetTypes: %p", assemblyGetTypes->methodPointer);
        } else {
            LOGI("miss Assembly::GetTypes");
            return;
        }
        typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
        typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
        for (size_t i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) continue;
            std::stringstream imageStr;
            auto image_name = il2cpp_image_get_name(image);
            if (!image_name) continue;
            imageStr << "\n// Dll : " << image_name;
            auto imageName = std::string(image_name);
            auto pos = imageName.rfind('.');
            auto imageNameNoExt = imageName.substr(0, pos);
            auto assemblyFileName = il2cpp_string_new(imageNameNoExt.data());
            auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr,
                                                                                        assemblyFileName,
                                                                                        nullptr);
            auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer)(
                    reflectionAssembly, nullptr);
            if (!reflectionTypes) continue;
            auto items = reflectionTypes->vector;
            for (il2cpp_array_size_t j = 0; j < reflectionTypes->max_length; ++j) {
                auto klass = il2cpp_class_from_system_type((Il2CppReflectionType *) items[j]);
                if (!klass) continue;
                auto type = il2cpp_class_get_type(klass);
                //LOGD("type name : %s", il2cpp_type_get_name(type));
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    }
    LOGI("write dump file");
    auto outPath = std::string(outDir).append("/files/dump.cs");
    std::ofstream outStream(outPath);
    if (!outStream) {
        LOGE("il2cpp_dump: cannot open %s", outPath.c_str());
        return;
    }
    outStream << imageOutput.str();
    auto count = outPuts.size();
    for (size_t i = 0; i < count; ++i) {
        outStream << outPuts[i];
    }
    outStream.close();
    LOGI("dump done!");
}
