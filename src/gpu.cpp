#include "gpu.h"
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
#include <spdlog/spdlog.h>
#ifdef HAVE_XNVCTRL
#include "nvctrl.h"
#endif
#include "timing.hpp"
#ifdef HAVE_NVML
#include "nvidia_info.h"
#endif

#include "amdgpu.h"

using namespace std::chrono_literals;

struct gpuInfo gpu_info {};
amdgpu_files amdgpu {};

bool checkNvidia(const char *pci_dev){
    bool nvSuccess = false;
#ifdef HAVE_NVML
    nvSuccess = checkNVML(pci_dev) && getNVMLInfo({});
#endif
#ifdef HAVE_XNVCTRL
    if (!nvSuccess)
        nvSuccess = checkXNVCtrl();
#endif
#ifdef _WIN32
    if (!nvSuccess)
        nvSuccess = checkNVAPI();
#endif
    return nvSuccess;
}

void getNvidiaGpuInfo(const struct overlay_params& params){
#ifdef HAVE_NVML
    if (nvmlSuccess){
        getNVMLInfo(params);
        gpu_info.load = nvidiaUtilization.gpu;
        gpu_info.temp = nvidiaTemp;
        gpu_info.memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info.CoreClock = nvidiaCoreClock;
        gpu_info.MemClock = nvidiaMemClock;
        gpu_info.powerUsage = nvidiaPowerUsage / 1000;
        gpu_info.memoryTotal = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        gpu_info.fan_speed = nvidiaFanSpeed;
        if (params.enabled[OVERLAY_PARAM_ENABLED_throttling_status]){
            gpu_info.is_temp_throttled = (nvml_throttle_reasons & 0x0000000000000060LL) != 0;
            gpu_info.is_power_throttled = (nvml_throttle_reasons & 0x000000000000008CLL) != 0;
            gpu_info.is_other_throttled = (nvml_throttle_reasons & 0x0000000000000112LL) != 0;
        }
        #ifdef HAVE_XNVCTRL
            static bool nvctrl_available = checkXNVCtrl();
            if (nvctrl_available)
                gpu_info.fan_speed = getNvctrlFanSpeed();
        #endif

        return;
    }
#endif
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        gpu_info.load = nvctrl_info.load;
        gpu_info.temp = nvctrl_info.temp;
        gpu_info.memoryUsed = nvctrl_info.memoryUsed / (1024.f);
        gpu_info.CoreClock = nvctrl_info.CoreClock;
        gpu_info.MemClock = nvctrl_info.MemClock;
        gpu_info.powerUsage = 0;
        gpu_info.memoryTotal = nvctrl_info.memoryTotal;
        gpu_info.fan_speed = nvctrl_info.fan_speed;
        return;
    }
#endif
#ifdef _WIN32
nvapi_util();
#endif
}

void getAmdGpuInfo(int index){
#ifdef __linux__
    int64_t value = 0;
    if (metrics_paths[index].empty()){
        if (amdgpus[index].busy) {
            rewind(amdgpus[index].busy);
            fflush(amdgpus[index].busy);
            int value = 0;
            if (fscanf(amdgpus[index].busy, "%d", &value) != 1)
                value = 0;
            gpu_info.load = value;
        }


        if (amdgpus[index].memory_clock) {
            rewind(amdgpus[index].memory_clock);
            fflush(amdgpus[index].memory_clock);
            if (fscanf(amdgpus[index].memory_clock, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info.MemClock = value / 1000000;
        }

        if (amdgpus[index].power_usage) {
            rewind(amdgpus[index].power_usage);
            fflush(amdgpus[index].power_usage);
            if (fscanf(amdgpus[index].power_usage, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info.powerUsage = value / 1000000;
        }

        if (amdgpus[index].fan) {
            rewind(amdgpus[index].fan);
            fflush(amdgpus[index].fan);
            if (fscanf(amdgpus[index].fan, "%" PRId64, &value) != 1)
                value = 0;
            gpu_info.fan_speed = value;
        }
    }

    if (amdgpus[index].vram_total) {
        rewind(amdgpus[index].vram_total);
        fflush(amdgpus[index].vram_total);
        if (fscanf(amdgpus[index].vram_total, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memoryTotal = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpus[index].vram_used) {
        rewind(amdgpus[index].vram_used);
        fflush(amdgpus[index].vram_used);
        if (fscanf(amdgpus[index].vram_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memoryUsed = float(value) / (1024 * 1024 * 1024);
    }
    // On some GPUs SMU can sometimes return the wrong temperature.
    // As HWMON is way more visible than the SMU metrics, let's always trust it as it is the most likely to work
    if (amdgpus[index].core_clock) {
        rewind(amdgpus[index].core_clock);
        fflush(amdgpus[index].core_clock);
        if (fscanf(amdgpus[index].core_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info.CoreClock = value / 1000000;
    }

    if (amdgpus[index].temp){
        rewind(amdgpus[index].temp);
        fflush(amdgpus[index].temp);
        int value = 0;
        if (fscanf(amdgpus[index].temp, "%d", &value) != 1)
            value = 0;
        gpu_info.temp = value / 1000;
    }

    if (amdgpus[index].junction_temp){
        rewind(amdgpus[index].junction_temp);
        fflush(amdgpus[index].junction_temp);
        int value = 0;
        if (fscanf(amdgpus[index].junction_temp, "%d", &value) != 1)
            value = 0;
        gpu_info.junction_temp = value / 1000;
    }

    if (amdgpus[index].memory_temp){
        rewind(amdgpus[index].memory_temp);
        fflush(amdgpus[index].memory_temp);
        int value = 0;
        if (fscanf(amdgpus[index].memory_temp, "%d", &value) != 1)
            value = 0;
        gpu_info.memory_temp = value / 1000;
    }

    if (amdgpus[index].gtt_used) {
        rewind(amdgpus[index].gtt_used);
        fflush(amdgpus[index].gtt_used);
        if (fscanf(amdgpus[index].gtt_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.gtt_used = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpus[index].gpu_voltage_soc) {
        rewind(amdgpus[index].gpu_voltage_soc);
        fflush(amdgpus[index].gpu_voltage_soc);
        if (fscanf(amdgpus[index].gpu_voltage_soc, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.voltage = value;
    }
#endif
}
