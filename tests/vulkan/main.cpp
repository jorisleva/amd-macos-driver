// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "offscreen_reference.h"

namespace {
constexpr uint32_t kVendor = 0x1002, kLocal = 64, kGuard = 16, kSentinel = 0xa5c37e19;
constexpr uint64_t kTimeoutNs = 5000000000ull;
void check(VkResult r, const char* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": VkResult=" + std::to_string(r));
}
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"';
    return out.str();
}
std::string version(uint32_t v) {
    return std::to_string(VK_VERSION_MAJOR(v)) + "." + std::to_string(VK_VERSION_MINOR(v)) + "." + std::to_string(VK_VERSION_PATCH(v));
}
bool target(const VkPhysicalDeviceProperties& p, uint32_t id) {
    return p.vendorID == kVendor && p.deviceID == id && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
        && std::string(p.deviceName).find("RX 9070 XT") != std::string::npos;
}
uint32_t parseId(const std::string& text) {
    size_t consumed = 0;
    if (text.empty() || text.front() == '-') throw std::runtime_error("Invalid PCI device ID");
    auto value = std::stoul(text, &consumed, 0);
    if (consumed != text.size() || value == 0 || value > 0xffff) throw std::runtime_error("PCI device ID must be in 1..0xffff");
    return uint32_t(value);
}
struct ShaderRequirements { bool int64 = false; };
ShaderRequirements shaderContract(const std::vector<uint32_t>& words, const std::string& entry) {
    if (words.size() < 5 || words[0] != 0x07230203 || words[1] > 0x00010500 || !words[3] || words[4])
        throw std::runtime_error("Unsupported SPIR-V header for Vulkan 1.2");
    ShaderRequirements needs;
    uint32_t entryId = 0, localEntry = 0;
    size_t entries = 0;
    bool local = false;
    std::map<uint32_t, uint32_t> sets, bindings, storage;
    for (size_t i = 5; i < words.size();) {
        const uint32_t length = words[i] >> 16, op = words[i] & 0xffff;
        if (!length || length > words.size() - i) throw std::runtime_error("Malformed SPIR-V instruction");
        // Minimal admission check for this probe, not a general SPIR-V validator.
        // Opcode / enumerant values are defined in the SPIR-V grammar shipped by Khronos.
        if (op == 17) { // OpCapability
            if (length != 2 || (words[i + 1] != 1 && words[i + 1] != 11)) throw std::runtime_error("Shader capability outside the probe contract");
            needs.int64 |= words[i + 1] == 11;
        } else if (op == 15) { // OpEntryPoint: GLCompute
            if (length < 4 || words[i + 1] != 5) throw std::runtime_error("Probe requires a compute shader");
            const char* name = reinterpret_cast<const char*>(&words[i + 3]);
            const size_t maxBytes = (length - 3) * sizeof(uint32_t);
            const char* end = std::find(name, name + maxBytes, '\0');
            if (end == name + maxBytes || std::string(name, end) != entry) throw std::runtime_error("SPIR-V entry point does not match --entry");
            entryId = words[i + 2]; ++entries;
        } else if (op == 16 && length >= 3 && words[i + 2] == 17) { // OpExecutionMode LocalSize
            if (length != 6 || words[i + 3] != kLocal || words[i + 4] != 1 || words[i + 5] != 1)
                throw std::runtime_error("Probe requires fixed local size 64,1,1");
            local = true; localEntry = words[i + 1];
        } else if (op == 331) { // OpExecutionModeId: deliberately not supported here
            throw std::runtime_error("Specialized/dynamic local size is outside the probe contract");
        } else if (op == 71 && length == 4) { // OpDecorate
            if (words[i + 2] == 34) sets[words[i + 1]] = words[i + 3];
            if (words[i + 2] == 33) bindings[words[i + 1]] = words[i + 3];
        } else if (op == 59 && length >= 4) { // OpVariable
            storage[words[i + 2]] = words[i + 3];
            if (words[i + 3] == 9) throw std::runtime_error("Push constants are outside the probe contract");
        }
        i += length;
    }
    if (entries != 1 || !local || localEntry != entryId || bindings.size() != 4 || sets.size() != 4)
        throw std::runtime_error("Probe requires one compute entry and four storage descriptors");
    std::set<uint32_t> indices;
    for (const auto& binding : bindings) {
        auto set = sets.find(binding.first), var = storage.find(binding.first);
        if (set == sets.end() || set->second != 0 || var == storage.end() || var->second != 12 || binding.second > 3)
            throw std::runtime_error("Descriptor ABI must be storage buffers, set 0 / bindings 0..3");
        indices.insert(binding.second);
    }
    if (indices.size() != 4) throw std::runtime_error("Duplicate buffer binding");
    return needs;
}
struct Verification { uint32_t data = 0, guards = 0, inputs = 0; };
Verification verify(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b,
                    const std::vector<uint32_t>& result, const std::vector<uint32_t>& originalA,
                    const std::vector<uint32_t>& originalB, uint32_t count) {
    Verification v;
    for (size_t i = 0; i < result.size(); ++i) {
        if (i >= kGuard && i < kGuard + count) v.data += result[i] != uint32_t(originalA[i] + originalB[i]);
        else v.guards += result[i] != kSentinel;
        v.inputs += a[i] != originalA[i];
        v.inputs += b[i] != originalB[i];
    }
    return v;
}
void selfTest() {
    const uint32_t n = 65;
    std::vector<uint32_t> a(n + 2 * kGuard, kSentinel), b = a, result = a;
    for (uint32_t i = 0; i < n; ++i) {
        a[i + kGuard] = 0xffffffffu - i;
        b[i + kGuard] = 2 * i + 3;
        result[i + kGuard] = a[i + kGuard] + b[i + kGuard];
    }
    auto v = verify(a, b, result, a, b, n);
    if (v.data || v.guards || v.inputs) throw std::runtime_error("Verifier rejected a correct result");
    result[kGuard + 4] ^= 1;
    result[0] ^= 1;
    result.back() ^= 1;
    auto mutatedA = a;
    mutatedA[kGuard] ^= 1;
    v = verify(mutatedA, b, result, a, b, n);
    if (v.data != 1 || v.guards != 2 || v.inputs != 1) throw std::runtime_error("Verifier missed planted corruption");
    VkPhysicalDeviceProperties p{};
    p.vendorID = kVendor; p.deviceID = 0x7550; p.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    std::string name = "AMD Radeon RX 9070 XT";
    std::copy(name.begin(), name.end(), p.deviceName);
    if (!target(p, 0x7550) || target(p, 0x1234)) throw std::runtime_error("PCI ID selection failure");
    for (auto type : {VK_PHYSICAL_DEVICE_TYPE_CPU, VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU}) {
        p.deviceType = type;
        if (target(p, 0x7550)) throw std::runtime_error("Accepted a non-discrete GPU");
    }
    p.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU; p.vendorID = 0x10de;
    if (target(p, 0x7550)) throw std::runtime_error("Accepted NVIDIA");
    p.vendorID = kVendor; p.deviceName[0] = '\0';
    if (target(p, 0x7550)) throw std::runtime_error("Accepted an unconfirmed Radeon model");
}
struct DeviceRecord {
    VkPhysicalDeviceProperties properties{};
    std::string driverName, driverInfo;
    uint32_t driverId = 0;
};
std::string deviceJson(const DeviceRecord& d) {
    const auto& p = d.properties;
    std::ostringstream out;
    out << "{\"name\":" << quote(p.deviceName) << ",\"vendor_id\":" << p.vendorID
        << ",\"device_id\":" << p.deviceID << ",\"device_type\":" << p.deviceType
        << ",\"api_version\":" << quote(version(p.apiVersion)) << ",\"driver_version_raw\":" << p.driverVersion
        << ",\"driver_id\":" << d.driverId << ",\"driver_name\":" << quote(d.driverName)
        << ",\"driver_info\":" << quote(d.driverInfo) << '}';
    return out.str();
}
std::string checksum(const uint32_t* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= (data[i] >> shift) & 0xff; hash *= 1099511628211ull;
    }
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
struct CaseResult { uint32_t count, iteration; Verification differences; std::string outputChecksum, referenceChecksum, memoryPath; };
struct MemoryRecord {
    std::string path, role;
    uint32_t binding, type, heap, flags;
    VkDeviceSize heapSize;
};
struct Report {
    std::string status = "failed", mode, error, shader, entry = "main";
    std::vector<DeviceRecord> devices;
    std::vector<CaseResult> cases;
    std::vector<offscreen::Case> graphicsCases;
    std::vector<MemoryRecord> allocations;
    int selected = -1;
    uint32_t queueFamily = 0, deviceId = 0;
    uint32_t subPixelPrecisionBits = 0, formatFeatures = 0;
    bool shaderInt64 = false;
    bool validation = false;
    bool synchronizationValidation = false;
    std::atomic<uint32_t> validationErrors{0};
    std::string json() const {
        std::ostringstream out;
        out << "{\n  \"schema_version\":2,\"status\":" << quote(status) << ",\"mode\":" << quote(mode)
            << ",\"code_revision\":" << quote(CODE_REVISION) << ",\"error\":" << quote(error)
            << ",\"expected_vendor_id\":" << kVendor << ",\"expected_device_id\":" << deviceId
            << ",\"shader\":" << quote(shader) << ",\"entry_point\":" << quote(entry);
        if (mode != "graphics") out << ",\"local_size\":[64,1,1],\"guard_words_each_side\":" << kGuard << ",\"integer_tolerance\":0";
        out << ",\"fence_timeout_ns\":" << kTimeoutNs
            << ",\"shader_int64_required\":" << (shaderInt64 ? "true" : "false")
            << ",\"validation_enabled\":" << (validation ? "true" : "false")
            << ",\"synchronization_validation_enabled\":" << (synchronizationValidation ? "true" : "false")
            << ",\"validation_errors\":" << validationErrors.load() << ",\n  \"devices\":[";
        for (size_t i = 0; i < devices.size(); ++i) out << (i ? "," : "") << deviceJson(devices[i]);
        out << "],\n  \"selected_device\":";
        if (selected < 0) out << "null"; else out << deviceJson(devices[size_t(selected)]);
        out << ",\"queue_family\":" << queueFamily << ",\n  \"allocations\":[";
        for (size_t i = 0; i < allocations.size(); ++i) {
            const auto& a = allocations[i];
            out << (i ? "," : "") << "{\"memory_path\":" << quote(a.path) << ",\"role\":" << quote(a.role)
                << ",\"binding\":" << a.binding << ",\"memory_type\":" << a.type << ",\"heap_index\":" << a.heap
                << ",\"property_flags\":" << a.flags << ",\"heap_size_bytes\":" << a.heapSize << '}';
        }
        out << "],\n  \"cases\":[";
        for (size_t i = 0; i < cases.size(); ++i) {
            const auto& c = cases[i];
            out << (i ? "," : "") << "{\"count\":" << c.count << ",\"iteration\":" << c.iteration
                << ",\"data_mismatches\":" << c.differences.data << ",\"guard_mismatches\":" << c.differences.guards
                << ",\"input_mismatches\":" << c.differences.inputs
                << ",\"memory_path\":" << quote(c.memoryPath)
                << ",\"output_fnv1a64\":" << quote(c.outputChecksum) << ",\"reference_fnv1a64\":" << quote(c.referenceChecksum) << '}';
        }
        out << "],\n  \"graphics\":{\"format\":\"R8G8B8A8_UNORM\",\"samples\":1,\"guard_bytes_each_side\":"
            << offscreen::kGuardBytes << ",\"subpixel_precision_bits\":" << subPixelPrecisionBits
            << ",\"optimal_tiling_format_features\":" << formatFeatures << ",\"cases\":[";
        for (size_t i = 0; i < graphicsCases.size(); ++i) {
            const auto& c = graphicsCases[i]; const auto& d = c.differences;
            out << (i ? "," : "") << "{\"scene\":" << quote(c.scene) << ",\"name\":" << quote(c.name)
                << ",\"width\":" << c.width << ",\"height\":" << c.height << ",\"iteration\":" << c.iteration
                << ",\"channel_tolerance\":" << c.tolerance << ",\"covered_pixels\":" << c.covered
                << ",\"overlap_pixels\":" << c.overlap << ",\"pixel_mismatches\":" << d.pixels
                << ",\"channel_mismatches\":" << d.channels << ",\"maximum_channel_error\":" << d.maxError
                << ",\"guard_mismatches\":" << d.guards << ",\"upload_mismatches\":" << d.upload
                << ",\"output_fnv1a64\":" << quote(c.outputChecksum)
                << ",\"reference_fnv1a64\":" << quote(c.referenceChecksum) << '}';
        }
        out << "]}\n}\n";
        return out.str();
    }
};
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++static_cast<Report*>(context)->validationErrors;
        std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}
struct Buffer { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; void* mapped = nullptr; VkDeviceSize size = 0; };
struct Harness {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::array<Buffer, 4> buffers{}, deviceBuffers{}, stagingBuffers{};
    bool pending = false;
    ~Harness() {
        // On timeout do not free resources still used by the GPU or perform an unbounded idle wait.
        // Process teardown owns recovery; this run has already failed.
        if (pending) return;
        if (device) {
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (module) vkDestroyShaderModule(device, module, nullptr);
            if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            for (auto group : {&buffers, &deviceBuffers, &stagingBuffers}) for (auto& b : *group) {
                if (b.mapped) vkUnmapMemory(device, b.memory);
                if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
                if (b.memory) vkFreeMemory(device, b.memory, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (messenger) {
            auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroy) destroy(instance, messenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    void enumerate(Report& report) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "AMD validation probe"; app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; create.pApplicationInfo = &app;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const std::array<const char*, 2> extensions{VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        const VkValidationFeatureEnableEXT syncFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validationFeatures.enabledValidationFeatureCount = 1; validationFeatures.pEnabledValidationFeatures = &syncFeature;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = debugCallback; debug.pUserData = &report;
        if (report.validation) {
            create.enabledLayerCount = 1; create.ppEnabledLayerNames = &layer;
            create.enabledExtensionCount = report.synchronizationValidation ? 2u : 1u;
            create.ppEnabledExtensionNames = extensions.data(); create.pNext = &debug;
            if (report.synchronizationValidation) debug.pNext = &validationFeatures;
        }
        check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
        // VkDebugUtilsMessengerCreateInfoEXT::pNext must be null for this separate call.
        debug.pNext = nullptr;
        if (report.validation) {
            auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!make) throw std::runtime_error("Validation debug messenger unavailable");
            check(make(instance, &debug, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
        }
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> handles(count);
        check(vkEnumeratePhysicalDevices(instance, &count, handles.data()), "vkEnumeratePhysicalDevices");
        handles.resize(count);
        size_t matches = 0;
        for (auto handle : handles) {
            DeviceRecord record;
            vkGetPhysicalDeviceProperties(handle, &record.properties);
            if (record.properties.apiVersion >= VK_API_VERSION_1_2) {
                VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
                VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; props.pNext = &driver;
                vkGetPhysicalDeviceProperties2(handle, &props);
                record.driverId = driver.driverID; record.driverName = driver.driverName; record.driverInfo = driver.driverInfo;
            }
            if (target(record.properties, report.deviceId)) { physical = handle; report.selected = int(report.devices.size()); ++matches; }
            report.devices.push_back(record);
        }
        if (report.mode == "list") return;
        if (matches != 1) {
            report.selected = -1;
            throw std::runtime_error(matches == 0 ? "RX 9070 XT with the requested PCI ID absent; no fallback permitted" : "Multiple target GPUs; selection is ambiguous");
        }
        const auto& limits = report.devices[size_t(report.selected)].properties;
        if (limits.apiVersion < VK_API_VERSION_1_2 || limits.limits.maxComputeWorkGroupInvocations < kLocal || limits.limits.maxComputeWorkGroupSize[0] < kLocal)
            throw std::runtime_error("Vulkan 1.2 / local size 64 unavailable");
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues.data());
        const VkQueueFlags requiredQueue = report.mode == "graphics" ? VK_QUEUE_GRAPHICS_BIT : VK_QUEUE_COMPUTE_BIT;
        auto it = std::find_if(queues.begin(), queues.end(), [requiredQueue](const auto& q) { return q.queueCount && (q.queueFlags & requiredQueue); });
        if (it == queues.end()) throw std::runtime_error("Required GPU queue unavailable");
        report.queueFamily = uint32_t(it - queues.begin());
    }
    void allocate(Buffer& b, VkDeviceSize size, bool deviceLocal, Report& report,
                  const std::string& path, const std::string& role, uint32_t binding) {
        b.size = size;
        VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; create.size = size;
        create.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(device, &create, nullptr, &b.buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, b.buffer, &requirements);
        VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        uint32_t type = UINT32_MAX;
        const VkMemoryPropertyFlags required = deviceLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        // Prefer unmappable VRAM for the transfer path; report the actual flags even on BAR-visible heaps.
        for (unsigned pass = 0; pass < 2 && type == UINT32_MAX; ++pass) {
            for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
                const auto flags = memory.memoryTypes[i].propertyFlags;
                if (!(requirements.memoryTypeBits & (1u << i)) || (flags & required) != required) continue;
                if (deviceLocal && !pass && (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
                type = i; break;
            }
        }
        if (type == UINT32_MAX) throw std::runtime_error(deviceLocal ? "No compatible device-local storage memory" : "No host-visible coherent staging/storage memory");
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &alloc, nullptr, &b.memory), "vkAllocateMemory");
        check(vkBindBufferMemory(device, b.buffer, b.memory, 0), "vkBindBufferMemory");
        if (!deviceLocal) check(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "vkMapMemory");
        const auto& chosen = memory.memoryTypes[type];
        report.allocations.push_back({path, role, binding, type, chosen.heapIndex, chosen.propertyFlags, memory.memoryHeaps[chosen.heapIndex].size});
    }
    void compute(Report& report, const std::vector<uint32_t>& words) {
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex = report.queueFamily; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; create.queueCreateInfoCount = 1; create.pQueueCreateInfos = &q;
        VkPhysicalDeviceFeatures supported{}, enabled{};
        vkGetPhysicalDeviceFeatures(physical, &supported);
        if (report.shaderInt64 && !supported.shaderInt64) throw std::runtime_error("Translated shader requires unavailable shaderInt64");
        enabled.shaderInt64 = report.shaderInt64 ? VK_TRUE : VK_FALSE; create.pEnabledFeatures = &enabled;
        check(vkCreateDevice(physical, &create, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, report.queueFamily, 0, &queue);
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i) { bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT; }
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount = uint32_t(bindings.size()); layout.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &setLayout), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 1; pl.pSetLayouts = &setLayout;
        check(vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize = words.size() * sizeof(uint32_t); sm.pCode = words.data();
        check(vkCreateShaderModule(device, &sm, nullptr, &module), "vkCreateShaderModule");
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module; pipelineInfo.stage.pName = report.entry.c_str(); pipelineInfo.layout = pipelineLayout;
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");
        constexpr uint32_t maxCount = 4097;
        for (uint32_t i = 0; i < buffers.size(); ++i) {
            const VkDeviceSize size = (i == 3 ? 2 : maxCount + 2 * kGuard) * sizeof(uint32_t);
            allocate(buffers[i], size, false, report, "host-coherent", "storage", i);
            allocate(deviceBuffers[i], size, true, report, "device-local-staging", "storage", i);
            allocate(stagingBuffers[i], size, false, report, "device-local-staging", "staging", i);
        }
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets = 1; dp.poolSizeCount = 1; dp.pPoolSizes = &poolSize;
        check(vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; da.descriptorPool = descriptorPool; da.descriptorSetCount = 1; da.pSetLayouts = &setLayout;
        VkDescriptorSet set{}; check(vkAllocateDescriptorSets(device, &da, &set), "vkAllocateDescriptorSets");
        std::array<VkDescriptorBufferInfo, 4> infos{};
        std::array<VkWriteDescriptorSet, 4> writes{};
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex = report.queueFamily;
        check(vkCreateCommandPool(device, &cp, nullptr, &commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = commandPool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        VkCommandBuffer command{}; check(vkAllocateCommandBuffers(device, &ca, &command), "vkAllocateCommandBuffers");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; check(vkCreateFence(device, &fc, nullptr, &fence), "vkCreateFence");
        for (bool staged : {false, true}) {
            const std::string memoryPath = staged ? "device-local-staging" : "host-coherent";
            auto& gpuBuffers = staged ? deviceBuffers : buffers;
            auto& hostBuffers = staged ? stagingBuffers : buffers;
            for (uint32_t i = 0; i < gpuBuffers.size(); ++i) {
                infos[i] = {gpuBuffers[i].buffer, 0, gpuBuffers[i].size};
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = set; writes[i].dstBinding = i;
                writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &infos[i];
            }
            vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
            for (uint32_t count : {1u, 63u, 64u, 65u, 257u, 4097u}) {
                for (uint32_t iteration = 0; iteration < 3; ++iteration) {
                    // Check the entire allocation, including unused tail words, after every submission.
                    std::vector<uint32_t> a(maxCount + 2 * kGuard, kSentinel), b = a;
                    for (uint32_t i = 0; i < count; ++i) { a[i + kGuard] = 0xfffffff0u - i * 17u + iteration; b[i + kGuard] = i * 31u + 27u + iteration; }
                    std::copy(a.begin(), a.end(), static_cast<uint32_t*>(hostBuffers[0].mapped));
                    std::copy(b.begin(), b.end(), static_cast<uint32_t*>(hostBuffers[1].mapped));
                    std::fill_n(static_cast<uint32_t*>(hostBuffers[2].mapped), a.size(), kSentinel);
                    auto params = static_cast<uint32_t*>(hostBuffers[3].mapped); params[0] = count; params[1] = kGuard;
                    check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
                    auto barrier = [&](VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
                        VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                        memoryBarrier.srcAccessMask = srcAccess; memoryBarrier.dstAccessMask = dstAccess;
                        vkCmdPipelineBarrier(command, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
                    };
                    if (staged) {
                        barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                        for (uint32_t i = 0; i < gpuBuffers.size(); ++i) {
                            VkBufferCopy copy{0, 0, gpuBuffers[i].size};
                            vkCmdCopyBuffer(command, hostBuffers[i].buffer, gpuBuffers[i].buffer, 1, &copy);
                        }
                        barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    } else {
                        barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    }
                    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
                    vkCmdDispatch(command, (count + kLocal - 1) / kLocal, 1, 1);
                    if (staged) {
                        // Also order upload reads before overwriting the same staging buffers during readback.
                        // Transfer writes cover the unchanged input/guard regions copied back from VRAM.
                        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
                        for (uint32_t i = 0; i < gpuBuffers.size(); ++i) {
                            VkBufferCopy copy{0, 0, gpuBuffers[i].size};
                            vkCmdCopyBuffer(command, gpuBuffers[i].buffer, hostBuffers[i].buffer, 1, &copy);
                        }
                        barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
                    } else {
                        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
                    }
                    check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
                    check(vkResetFences(device, 1, &fence), "vkResetFences");
                    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
                    check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit"); pending = true;
                    check(vkWaitForFences(device, 1, &fence, VK_TRUE, kTimeoutNs), "vkWaitForFences"); pending = false;
                    auto copyBuffer = [&](size_t i) { auto p = static_cast<uint32_t*>(hostBuffers[i].mapped); return std::vector<uint32_t>(p, p + a.size()); };
                    auto differences = verify(copyBuffer(0), copyBuffer(1), copyBuffer(2), a, b, count);
                    differences.inputs += params[0] != count; differences.inputs += params[1] != kGuard;
                    std::vector<uint32_t> expected(a.size(), kSentinel);
                    for (uint32_t i = 0; i < count; ++i) expected[i + kGuard] = a[i + kGuard] + b[i + kGuard];
                    report.cases.push_back({count, iteration, differences,
                        checksum(static_cast<uint32_t*>(hostBuffers[2].mapped), a.size()), checksum(expected.data(), expected.size()), memoryPath});
                    if (differences.data || differences.guards || differences.inputs) throw std::runtime_error("GPU result or buffer guards differ from reference");
                    if (report.validationErrors.load()) throw std::runtime_error("Vulkan validation reported an error");
                }
            }
        }
    }
};
std::vector<uint32_t> readShader(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open shader: " + path);
    auto length = input.tellg();
    if (length < 20 || length > 16 * 1024 * 1024 || length % 4 != 0) throw std::runtime_error("Invalid SPIR-V file size");
    std::vector<uint32_t> words(size_t(length) / 4); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(words.data()), length) || words[0] != 0x07230203) throw std::runtime_error("Invalid SPIR-V header");
    return words;
}
#include "offscreen.h"
} // namespace

int main(int argc, char** argv) {
    Report report;
    std::string reportPath, shaderDirectory, imageDirectory;
    bool graphics = false;
    int exitCode = 1;
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") { selfTest(); std::cout << "PASS: verifier detects planted data/guard/input corruption; target selection rejects other devices\n"; return 0; }
        if (argc == 2 && std::string(argv[1]) == "--self-test-graphics") { offscreen::selfTest(); std::cout << "PASS: image reference, coverage, blending, tolerance and corruption checks\n"; return 0; }
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "amd_gpu_probe --list [--report FILE]\n"
                      << "amd_gpu_probe --device-id PCI_ID [--shader FILE --entry NAME] [--validation | --sync-validation] [--report FILE]\n"
                      << "amd_gpu_probe --check-shader FILE [--entry NAME] (ABI admission only, no GPU execution)\n"
                      << "amd_gpu_probe --graphics --device-id PCI_ID --shader-directory DIR --image-directory DIR [--sync-validation] [--report FILE]\n"
                      << "Without --shader, only identifies the target. Compute requires set 0 / four storage buffers, local 64x1x1.\n";
            return 0;
        }
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i == argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--list") report.mode = "list";
            else if (arg == "--graphics") graphics = true;
            else if (arg == "--shader-directory") shaderDirectory = value();
            else if (arg == "--image-directory") imageDirectory = value();
            else if (arg == "--check-shader") { report.mode = "shader-contract"; report.shader = value(); }
            else if (arg == "--device-id") report.deviceId = parseId(value());
            else if (arg == "--shader") report.shader = value();
            else if (arg == "--entry") report.entry = value();
            else if (arg == "--report") reportPath = value();
            else if (arg == "--validation") report.validation = true;
            else if (arg == "--sync-validation") { report.validation = true; report.synchronizationValidation = true; }
            else throw std::runtime_error("Unknown option: " + arg);
        }
        if (graphics) {
            if (!report.mode.empty() || !report.shader.empty() || report.entry != "main") throw std::runtime_error("Graphics cannot be combined with list/compute/shader-contract options");
            report.mode = "graphics";
            if (shaderDirectory.empty() || imageDirectory.empty()) throw std::runtime_error("Graphics requires shader and image directories");
        } else if (!shaderDirectory.empty() || !imageDirectory.empty()) throw std::runtime_error("Image/shader directories require --graphics");
        if (report.mode == "list" && (report.deviceId || !report.shader.empty())) throw std::runtime_error("--list cannot be combined with target/compute options");
        if (report.mode == "shader-contract" && report.deviceId) throw std::runtime_error("--check-shader cannot select a GPU");
        if (report.mode != "list" && report.mode != "shader-contract") {
            if (!report.deviceId) throw std::runtime_error("Explicit --device-id from the target inventory is required");
            if (!graphics) report.mode = report.shader.empty() ? "identify" : "compute";
        }
        auto words = report.shader.empty() ? std::vector<uint32_t>{} : readShader(report.shader);
        if (!words.empty()) report.shaderInt64 = shaderContract(words, report.entry).int64;
        if (report.mode != "shader-contract") {
            Harness harness;
            harness.enumerate(report);
            if (report.mode == "compute") harness.compute(report, words);
            if (report.mode == "graphics") { OffscreenHarness images(harness, report); images.run(shaderDirectory, imageDirectory); }
        }
        if (report.validationErrors.load()) throw std::runtime_error("Vulkan validation reported an error");
        report.status = report.mode == "compute" || report.mode == "graphics" ? "passed" : report.mode == "list" ? "enumerated" : report.mode == "shader-contract" ? "contract-checked" : "identified";
        exitCode = 0;
    } catch (const std::exception& e) { report.status = "failed"; report.error = e.what(); }
    const auto json = report.json();
    if (!reportPath.empty()) {
        std::ofstream file(reportPath, std::ios::binary);
        if (!file || !(file << json)) { std::cerr << "Cannot write report: " << reportPath << '\n'; exitCode = 1; }
    }
    std::cout << json;
    return exitCode;
}
