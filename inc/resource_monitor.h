/********************************************************
 * Description : resource monitor
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 2.0
 * History     :
 * Copyright(C): 2021-2022
 ********************************************************/

#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H


#include <cstdint>
#include <cstddef>
#include <list>
#include <string>

#ifdef _MSC_VER
    #define RESOURCE_MONITOR_CDECL            __cdecl
    #define RESOURCE_MONITOR_STDCALL          __stdcall
    #ifdef EXPORT_RESOURCE_MONITOR_DLL
        #define RESOURCE_MONITOR_API          __declspec(dllexport)
    #else
        #ifdef USE_RESOURCE_MONITOR_DLL
            #define RESOURCE_MONITOR_API      __declspec(dllimport)
        #else
            #define RESOURCE_MONITOR_API
        #endif // USE_RESOURCE_MONITOR_DLL
    #endif // EXPORT_RESOURCE_MONITOR_DLL
#else
    #define RESOURCE_MONITOR_CDECL
    #define RESOURCE_MONITOR_STDCALL
    #define RESOURCE_MONITOR_API
#endif // _MSC_VER

struct RESOURCE_MONITOR_API ProcessResource
{
    double          cpu_usage;
    uint64_t        ram_usage;
    double          gpu_3d_usage;
    double          gpu_vr_usage;
    double          gpu_enc_usage;
    double          gpu_dec_usage;
    uint64_t        gpu_mem_usage;
};

struct RESOURCE_MONITOR_API SystemResource
{
    uint64_t        cpu_count;
    double          cpu_usage;
    uint64_t        ram_usage;
    uint64_t        ram_total;
    uint64_t        gpu_count;
    double          gpu_3d_usage;
    double          gpu_vr_usage;
    double          gpu_enc_usage;
    double          gpu_dec_usage;
    uint64_t        gpu_mem_usage;
    uint64_t        gpu_mem_total;
};

class ResourceMonitorImpl;

class RESOURCE_MONITOR_API ResourceMonitor
{
public:
    ResourceMonitor();
    virtual ~ResourceMonitor();

public:
    bool init();
    void exit();

public:
    bool append_process(uint32_t process_id, bool process_tree);
    bool remove_process(uint32_t process_id);

public:
    bool get_process_resource(uint32_t process_id, ProcessResource & process_resource);
    bool get_system_resource(SystemResource & system_resource);
    bool get_graphics_cards(std::list<std::string> & graphics_card_list);

private:
    ResourceMonitor(const ResourceMonitor &) = delete;
    ResourceMonitor(ResourceMonitor &&) = delete;
    ResourceMonitor & operator = (const ResourceMonitor &) = delete;
    ResourceMonitor & operator = (ResourceMonitor &&) = delete;

private:
    ResourceMonitorImpl           * m_resource_monitor_impl;
};


#endif // RESOURCE_MONITOR_H
