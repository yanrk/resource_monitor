/********************************************************
 * Description : resource monitor implement
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 2.0
 * History     :
 * Copyright(C): 2021-2022
 ********************************************************/

#ifndef RESOURCE_MONITOR_IMPL_H
#define RESOURCE_MONITOR_IMPL_H


#include <set>
#include <map>
#include <mutex>
#include <thread>
#include <pdh.h>
#include <wtypes.h>
#include "resource_monitor.h"

struct ProcessLeaf
{
    std::set<uint32_t>                      process_descendant_set;

    ProcessLeaf();
};

struct ProcessTree
{
    bool                                    process_tree;
    std::set<uint32_t>                      process_descendant_set;

    ProcessTree(uint32_t ancestor, bool tree);
};

struct ProcessHelper
{
    uint32_t                                process_ancestor;
    HANDLE                                  process_handle;
    uint64_t                                cpu_check_time;
    uint64_t                                cpu_system_time;

    ProcessHelper(uint32_t ancestor, HANDLE handle);
};

struct ProcessSnapshot
{
    ProcessResource process_resource;

    ProcessSnapshot();
};

struct SystemSnapshot
{
    SystemResource                          system_resource;
    std::list<std::string>                  graphics_card_names;
    std::map<uint32_t, ProcessLeaf>         process_leaf_map;     /* key: every monitoring process, value: sub processes which is a monitoring process too  */
    std::map<uint32_t, ProcessTree>         process_tree_map;     /* key: every monitoring process, value: process and sub processes which is not a monitoring process */
    std::map<uint32_t, ProcessHelper>       process_helper_map;   /* key: every monitoring process and their sub processes if need monitor a process tree */
    std::map<uint32_t, ProcessSnapshot>     process_snapshot_map; /* key: every monitoring process */

    SystemSnapshot();
};

class ResourceMonitorImpl
{
public:
    ResourceMonitorImpl();
    virtual ~ResourceMonitorImpl();

public:
    bool init();
    void exit();

public:
    bool append_process(uint32_t process_id, bool process_tree);
    bool remove_process(uint32_t process_id);

public:
    bool get_process_resource(uint32_t process_id, ProcessResource & process_resource);
    bool get_system_resource(SystemResource & system_resource);
    bool get_graphics_cards(std::list<std::string> & graphics_card_names);

private:
    void query_resource_thread();

private:
    volatile bool                                       m_running;
    std::thread                                         m_query_thread;
    HANDLE                                              m_query_event;
    PDH_HQUERY                                          m_query_handle;
    PDH_HCOUNTER                                        m_processor_counter;
    PDH_HCOUNTER                                        m_gpu_engine_counter;
    PDH_HCOUNTER                                        m_gpu_memory_counter;
    SystemSnapshot                                      m_system_snapshot;
    std::mutex                                          m_system_snapshot_mutex;
};


#endif // RESOURCE_MONITOR_IMPL_H
