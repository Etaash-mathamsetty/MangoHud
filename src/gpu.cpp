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

std::vector<gpuInfo> gpu_info {};
std::vector<amdgpu_files> amdgpu {};

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

//TODO: Implement multigpu support with Nvidia
void getNvidiaGpuInfo(const struct overlay_params& params){
#ifdef HAVE_NVML
    if (nvmlSuccess){
        getNVMLInfo(params);
        gpu_info[0].load = nvidiaUtilization.gpu;
        gpu_info[0].temp = nvidiaTemp;
        gpu_info[0].memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info[0].CoreClock = nvidiaCoreClock;
        gpu_info[0].MemClock = nvidiaMemClock;
        gpu_info[0].powerUsage = nvidiaPowerUsage / 1000;
        gpu_info[0].memoryTotal = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        gpu_info[0].fan_speed = nvidiaFanSpeed;
        if (params.enabled[OVERLAY_PARAM_ENABLED_throttling_status]){
            gpu_info[0].is_temp_throttled = (nvml_throttle_reasons & 0x0000000000000060LL) != 0;
            gpu_info[0].is_power_throttled = (nvml_throttle_reasons & 0x000000000000008CLL) != 0;
            gpu_info[0].is_other_throttled = (nvml_throttle_reasons & 0x0000000000000112LL) != 0;
        }
        #ifdef HAVE_XNVCTRL
            static bool nvctrl_available = checkXNVCtrl();
            if (nvctrl_available)
                gpu_info[0].fan_speed = getNvctrlFanSpeed();
        #endif

        return;
    }
#endif
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        gpu_info[0].load = nvctrl_info.load;
        gpu_info[0].temp = nvctrl_info.temp;
        gpu_info[0].memoryUsed = nvctrl_info.memoryUsed / (1024.f);
        gpu_info[0].CoreClock = nvctrl_info.CoreClock;
        gpu_info[0].MemClock = nvctrl_info.MemClock;
        gpu_info[0].powerUsage = 0;
        gpu_info[0].memoryTotal = nvctrl_info.memoryTotal;
        gpu_info[0].fan_speed = nvctrl_info.fan_speed;
        return;
    }
#endif
#ifdef _WIN32
nvapi_util();
#endif
}

void getAmdGpuInfo(){
#ifdef __linux__
    for(size_t index = 0; index < metrics_paths.size(); index++)
    {
        int64_t value = 0;
        if (metrics_paths[index].empty()){
            if (amdgpu[index].busy) {
                rewind(amdgpu[index].busy);
                fflush(amdgpu[index].busy);
                int value = 0;
                if (fscanf(amdgpu[index].busy, "%d", &value) != 1)
                    value = 0;
                gpu_info[index].load = value;
            }


            if (amdgpu[index].memory_clock) {
                rewind(amdgpu[index].memory_clock);
                fflush(amdgpu[index].memory_clock);
                if (fscanf(amdgpu[index].memory_clock, "%" PRId64, &value) != 1)
                    value = 0;

                gpu_info[index].MemClock = value / 1000000;
            }

            if (amdgpu[index].power_usage) {
                rewind(amdgpu[index].power_usage);
                fflush(amdgpu[index].power_usage);
                if (fscanf(amdgpu[index].power_usage, "%" PRId64, &value) != 1)
                    value = 0;

                gpu_info[index].powerUsage = value / 1000000;
            }

            if (amdgpu[index].fan) {
                rewind(amdgpu[index].fan);
                fflush(amdgpu[index].fan);
                if (fscanf(amdgpu[index].fan, "%" PRId64, &value) != 1)
                    value = 0;
                gpu_info[index].fan_speed = value;
            }
        }

        if (amdgpu[index].vram_total) {
            rewind(amdgpu[index].vram_total);
            fflush(amdgpu[index].vram_total);
            if (fscanf(amdgpu[index].vram_total, "%" PRId64, &value) != 1)
                value = 0;
            gpu_info[index].memoryTotal = float(value) / (1024 * 1024 * 1024);
        }

        if (amdgpu[index].vram_used) {
            rewind(amdgpu[index].vram_used);
            fflush(amdgpu[index].vram_used);
            if (fscanf(amdgpu[index].vram_used, "%" PRId64, &value) != 1)
                value = 0;
            gpu_info[index].memoryUsed = float(value) / (1024 * 1024 * 1024);
        }
        // On some GPUs SMU can sometimes return the wrong temperature.
        // As HWMON is way more visible than the SMU metrics, let's always trust it as it is the most likely to work
        if (amdgpu[index].core_clock) {
            rewind(amdgpu[index].core_clock);
            fflush(amdgpu[index].core_clock);
            if (fscanf(amdgpu[index].core_clock, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info[index].CoreClock = value / 1000000;
        }

        if (amdgpu[index].temp){
            rewind(amdgpu[index].temp);
            fflush(amdgpu[index].temp);
            int value = 0;
            if (fscanf(amdgpu[index].temp, "%d", &value) != 1)
                value = 0;
            gpu_info[index].temp = value / 1000;
        }

        if (amdgpu[index].junction_temp){
            rewind(amdgpu[index].junction_temp);
            fflush(amdgpu[index].junction_temp);
            int value = 0;
            if (fscanf(amdgpu[index].junction_temp, "%d", &value) != 1)
                value = 0;
            gpu_info[index].junction_temp = value / 1000;
        }

        if (amdgpu[index].memory_temp){
            rewind(amdgpu[index].memory_temp);
            fflush(amdgpu[index].memory_temp);
            int value = 0;
            if (fscanf(amdgpu[index].memory_temp, "%d", &value) != 1)
                value = 0;
            gpu_info[index].memory_temp = value / 1000;
        }

        if (amdgpu[index].gtt_used) {
            rewind(amdgpu[index].gtt_used);
            fflush(amdgpu[index].gtt_used);
            if (fscanf(amdgpu[index].gtt_used, "%" PRId64, &value) != 1)
                value = 0;
            gpu_info[index].gtt_used = float(value) / (1024 * 1024 * 1024);
        }

        if (amdgpu[index].gpu_voltage_soc) {
            rewind(amdgpu[index].gpu_voltage_soc);
            fflush(amdgpu[index].gpu_voltage_soc);
            if (fscanf(amdgpu[index].gpu_voltage_soc, "%" PRId64, &value) != 1)
                value = 0;
            gpu_info[index].voltage = value;
        }
    }
#endif
}
