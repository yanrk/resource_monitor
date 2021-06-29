/********************************************************
 * Description : resource monitor implement
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 2.0
 * History     :
 * Copyright(C): 2021-2022
 ********************************************************/

#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <atlbase.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <versionhelpers.h>
#include <map>
#include <vector>
#include <string>
#include <codecvt>
#include "resource_monitor_impl.h"
#include "filesystem/hardware.h"
#include "charset/charset.h"
#include "string/string.h"
#include "log/log.h"

ProcessLeaf::ProcessLeaf()
    : process_descendant_set()
{

}

ProcessTree::ProcessTree(uint32_t ancestor, bool tree)
    : process_tree(tree)
    , process_descendant_set()
{
    process_descendant_set.insert(ancestor);
}

ProcessHelper::ProcessHelper(uint32_t ancestor, HANDLE handle)
    : process_ancestor(ancestor)
    , process_handle(handle)
    , cpu_check_time(0)
    , cpu_system_time(0)
{

}

ProcessSnapshot::ProcessSnapshot()
    : process_resource()
{
    memset(&process_resource, 0x0, sizeof(process_resource));
}

SystemSnapshot::SystemSnapshot()
    : system_resource()
    , process_leaf_map()
    , process_tree_map()
    , process_helper_map()
    , process_snapshot_map()
{
    memset(&system_resource, 0x0, sizeof(system_resource));
}

static ProcessResource & operator += (ProcessResource & process_resource, const ProcessSnapshot & process_snapshot)
{
    process_resource.cpu_usage += process_snapshot.process_resource.cpu_usage;
    process_resource.ram_usage += process_snapshot.process_resource.ram_usage;
    process_resource.gpu_3d_usage += process_snapshot.process_resource.gpu_3d_usage;
    process_resource.gpu_vr_usage += process_snapshot.process_resource.gpu_vr_usage;
    process_resource.gpu_enc_usage += process_snapshot.process_resource.gpu_enc_usage;
    process_resource.gpu_dec_usage += process_snapshot.process_resource.gpu_dec_usage;
    process_resource.gpu_mem_usage += process_snapshot.process_resource.gpu_mem_usage;
    return (process_resource);
}

static bool process_is_alive(HANDLE process_handle)
{
    DWORD process_exit_code = 0;
    return (nullptr != process_handle && GetExitCodeProcess(process_handle, &process_exit_code) && STILL_ACTIVE == process_exit_code);
}

static bool append_process_to_monitor(SystemSnapshot & system_snapshot, uint32_t process_id, bool process_tree)
{
    if (0 == process_id)
    {
        return (false);
    }

    std::map<uint32_t, ProcessTree> & process_tree_map = system_snapshot.process_tree_map;
    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;

    std::map<uint32_t, ProcessTree>::iterator iter_tree = process_tree_map.find(process_id);
    if (process_tree_map.end() != iter_tree)
    {
        return (true);
    }

    std::map<uint32_t, ProcessHelper>::iterator iter_helper = process_helper_map.find(process_id);
    if (process_helper_map.end() != iter_helper)
    {
        iter_tree = process_tree_map.find(iter_helper->second.process_ancestor);
        if (process_tree_map.end() != iter_tree)
        {
            iter_tree->second.process_descendant_set.erase(process_id);
        }
        process_tree_map.insert(std::make_pair(process_id, ProcessTree(process_id, process_tree)));
        process_snapshot_map.insert(std::make_pair(process_id, ProcessSnapshot()));
        return (true);
    }
    else
    {
        HANDLE process_handle = nullptr;
        if (GetCurrentProcessId() == process_id)
        {
            process_handle = GetCurrentProcess();
        }
        else
        {
            process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
        }

        if (nullptr != process_handle)
        {
            process_tree_map.insert(std::make_pair(process_id, ProcessTree(process_id, process_tree)));
            process_helper_map.insert(std::make_pair(process_id, ProcessHelper(process_id, process_handle)));
            process_snapshot_map.insert(std::make_pair(process_id, ProcessSnapshot()));
            return (true);
        }
    }

    return (false);
}

static bool remove_process_from_monitor(SystemSnapshot & system_snapshot, uint32_t process_id)
{
    if (0 == process_id)
    {
        return (false);
    }

    std::map<uint32_t, ProcessTree> & process_tree_map = system_snapshot.process_tree_map;
    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;

    std::map<uint32_t, ProcessTree>::iterator iter_tree = process_tree_map.find(process_id);
    if (process_tree_map.end() == iter_tree)
    {
        return (false);
    }

    std::set<uint32_t> & process_descendant_set = iter_tree->second.process_descendant_set;
    for (std::set<uint32_t>::iterator iter_descendant = process_descendant_set.begin(); process_descendant_set.end() != iter_descendant; ++iter_descendant)
    {
        uint32_t process_descendant = *iter_descendant;
        std::map<uint32_t, ProcessHelper>::iterator iter_helper = process_helper_map.find(process_descendant);
        if (process_helper_map.end() != iter_helper)
        {
            if (GetCurrentProcessId() != process_descendant)
            {
                CloseHandle(iter_helper->second.process_handle);
            }
            process_helper_map.erase(iter_helper);
        }
    }
    process_snapshot_map.erase(process_id);
    process_tree_map.erase(iter_tree);

    return (true);
}

static bool update_process_tree(SystemSnapshot & system_snapshot)
{
    std::map<uint32_t, ProcessLeaf> & process_leaf_map = system_snapshot.process_leaf_map;
    process_leaf_map.clear();

    std::map<uint32_t, ProcessTree> & process_tree_map = system_snapshot.process_tree_map;
    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    if (process_tree_map.empty() || process_helper_map.empty())
    {
        return (true);
    }

    std::map<uint32_t, uint32_t> process_ancestor_map;
    for (std::map<uint32_t, ProcessTree>::iterator iter = process_tree_map.begin(); process_tree_map.end() != iter; ++iter)
    {
        if (iter->second.process_tree)
        {
            process_ancestor_map[iter->first] = iter->first;
        }
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == snapshot)
    {
        return (false);
    }

    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };

    for (BOOL ok = Process32First(snapshot, &pe); TRUE == ok; ok = Process32Next(snapshot, &pe))
    {
        std::map<uint32_t, uint32_t>::iterator iter = process_ancestor_map.find(pe.th32ParentProcessID);
        if (process_ancestor_map.end() != iter)
        {
            if (process_ancestor_map.end() != process_ancestor_map.find(pe.th32ProcessID))
            {
                ProcessLeaf & process_leaf = process_leaf_map[pe.th32ParentProcessID];
                process_leaf.process_descendant_set.insert(pe.th32ProcessID);
            }
            else
            {
                process_ancestor_map[pe.th32ProcessID] = iter->second;
            }
        }
    }

    CloseHandle(snapshot);

    for (std::map<uint32_t, uint32_t>::iterator iter_ancestor = process_ancestor_map.begin(); process_ancestor_map.end() != iter_ancestor; ++iter_ancestor)
    {
        uint32_t process_id = iter_ancestor->first;
        uint32_t process_ancestor = iter_ancestor->second;
        std::map<uint32_t, ProcessTree>::iterator iter_tree = process_tree_map.find(process_ancestor);
        if (process_tree_map.end() != iter_tree)
        {
            std::map<uint32_t, ProcessHelper>::iterator iter_helper = process_helper_map.find(process_id);
            if (process_helper_map.end() != iter_helper)
            {
                if (process_ancestor != iter_helper->second.process_ancestor)
                {
                    std::map<uint32_t, ProcessTree>::iterator iter = process_tree_map.find(iter_helper->second.process_ancestor);
                    if (process_tree_map.end() != iter)
                    {
                        iter->second.process_descendant_set.erase(process_id);
                    }
                    iter_tree->second.process_descendant_set.insert(process_id);
                    iter_helper->second.process_ancestor = process_ancestor;
                }
            }
            else
            {
                HANDLE process_handle = nullptr;
                if (GetCurrentProcessId() == process_id)
                {
                    process_handle = GetCurrentProcess();
                }
                else
                {
                    process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
                }
                if (nullptr != process_handle)
                {
                    iter_tree->second.process_descendant_set.insert(process_id);
                    process_helper_map.insert(std::make_pair(process_id, ProcessHelper(process_ancestor, process_handle)));
                }
            }
        }
    }

    for (std::map<uint32_t, ProcessLeaf>::iterator iter_leaf = process_leaf_map.begin(); process_leaf_map.end() != iter_leaf; ++iter_leaf)
    {
        std::set<uint32_t> & process_descendant_set = iter_leaf->second.process_descendant_set;
        std::list<uint32_t> process_descendant_list(process_descendant_set.begin(), process_descendant_set.end());
        while (!process_descendant_list.empty())
        {
            uint32_t process_descendant = process_descendant_list.front();
            process_descendant_list.pop_front();
            std::map<uint32_t, ProcessLeaf>::iterator iter_sub_leaf = process_leaf_map.find(process_descendant);
            if (process_leaf_map.end() != iter_sub_leaf)
            {
                std::set<uint32_t> & sub_process_descendant_set = iter_sub_leaf->second.process_descendant_set;
                process_descendant_set.insert(sub_process_descendant_set.begin(), sub_process_descendant_set.end());
                process_descendant_list.insert(process_descendant_list.end(), sub_process_descendant_set.begin(), sub_process_descendant_set.end());
            }
        }
    }

    return (true);
}

static uint64_t file_time_to_utc_time(FILETIME file_time)
{
    uint64_t utc_time = file_time.dwHighDateTime;
    utc_time <<= 32;
    utc_time |= file_time.dwLowDateTime;
    return (utc_time);
}

static bool get_process_cpu_usage(ProcessHelper & process_helper, ProcessSnapshot & process_snapshot, SystemSnapshot & system_snapshot)
{
    SystemResource & system_resource = system_snapshot.system_resource;
    if (0 == system_resource.cpu_count || nullptr == process_helper.process_handle)
    {
        return (false);
    }

    if (!process_is_alive(process_helper.process_handle))
    {
        return (false);
    }

    FILETIME current_time = { 0x0 };
    GetSystemTimeAsFileTime(&current_time);
    uint64_t cpu_check_time = file_time_to_utc_time(current_time);

    FILETIME creation_time = { 0x0 };
    FILETIME exit_time = { 0x0 };
    FILETIME kernel_time = { 0x0 };
    FILETIME user_time = { 0x0 };
    if (!GetProcessTimes(process_helper.process_handle, &creation_time, &exit_time, &kernel_time, &user_time))
    {
        return (false);
    }

    uint64_t cpu_system_time = file_time_to_utc_time(kernel_time) + file_time_to_utc_time(user_time);
    if (0 == process_helper.cpu_check_time || process_helper.cpu_check_time >= cpu_check_time || process_helper.cpu_system_time > cpu_system_time)
    {
        process_helper.cpu_check_time = cpu_check_time;
        process_helper.cpu_system_time = cpu_system_time;
        return (false);
    }

    uint64_t check_time_delta = cpu_check_time - process_helper.cpu_check_time;
    uint64_t system_time_delta = cpu_system_time - process_helper.cpu_system_time;

    ProcessResource & process_resource = process_snapshot.process_resource;
    process_resource.cpu_usage += static_cast<double>(100.0 * system_time_delta / system_resource.cpu_count / check_time_delta);

    process_helper.cpu_check_time = cpu_check_time;
    process_helper.cpu_system_time = cpu_system_time;

    return (true);
}

static bool get_process_memory_usage(ProcessHelper & process_helper, ProcessSnapshot & process_snapshot)
{
    if (nullptr == process_helper.process_handle)
    {
        return (false);
    }

    if (!process_is_alive(process_helper.process_handle))
    {
        return (false);
    }

    ProcessResource & process_resource = process_snapshot.process_resource;

    SYSTEM_INFO si = { 0x0 };
    GetSystemInfo(&si);
    PSAPI_WORKING_SET_INFORMATION pwsi = { 0x0 };
    if (QueryWorkingSet(process_helper.process_handle, &pwsi, sizeof(pwsi)) || ERROR_BAD_LENGTH == GetLastError())
    {
        process_resource.ram_usage += pwsi.NumberOfEntries * si.dwPageSize;
        return (true);
    }

    PROCESS_MEMORY_COUNTERS pmc = { 0x0 };
    if (GetProcessMemoryInfo(process_helper.process_handle, &pmc, sizeof(pmc)))
    {
        process_resource.ram_usage += pmc.WorkingSetSize;
        return (true);
    }

    return (false);
}

static bool get_system_cpu_count(SystemSnapshot & system_snapshot)
{
    SystemResource & system_resource = system_snapshot.system_resource;
    std::size_t core_count = 0;
    if (Goofer::get_system_cpu_core_count(core_count))
    {
        system_resource.cpu_count = static_cast<uint64_t>(core_count);
        return (true);
    }
    else
    {
        system_resource.cpu_count = 0;
        return (false);
    }
}

static bool get_system_memory_usage(SystemSnapshot & system_snapshot)
{
    SystemResource & system_resource = system_snapshot.system_resource;
    uint64_t total_size = 0;
    uint64_t avail_size = 0;
    if (Goofer::get_system_memory_usage(total_size, avail_size))
    {
        system_resource.ram_total = total_size;
        system_resource.ram_usage = total_size - avail_size;
        return (true);
    }
    else
    {
        system_resource.ram_total = 0;
        system_resource.ram_usage = 0;
        return (false);
    }
}

static bool get_process_cpu_usage(SystemSnapshot & system_snapshot)
{
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;
    for (std::map<uint32_t, ProcessSnapshot>::iterator iter = process_snapshot_map.begin(); process_snapshot_map.end() != iter; ++iter)
    {
        iter->second.process_resource.cpu_usage = 0;
    }

    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    for (std::map<uint32_t, ProcessHelper>::iterator iter = process_helper_map.begin(); process_helper_map.end() != iter; ++iter)
    {
        get_process_cpu_usage(iter->second, process_snapshot_map[iter->second.process_ancestor], system_snapshot);
    }

    return (true);
}

static bool get_process_memory_usage(SystemSnapshot & system_snapshot)
{
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;
    for (std::map<uint32_t, ProcessSnapshot>::iterator iter = process_snapshot_map.begin(); process_snapshot_map.end() != iter; ++iter)
    {
        iter->second.process_resource.ram_usage = 0;
    }

    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    for (std::map<uint32_t, ProcessHelper>::iterator iter = process_helper_map.begin(); process_helper_map.end() != iter; ++iter)
    {
        get_process_memory_usage(iter->second, process_snapshot_map[iter->second.process_ancestor]);
    }

    return (true);
}

static bool get_formatted_counter_array(PDH_HCOUNTER counter_handle, DWORD value_format, std::vector<char> & buffer, PDH_FMT_COUNTERVALUE_ITEM *& item_array, ULONG & item_count)
{
    item_array = nullptr;
    item_count = 0;

    ULONG buffer_size = 0;
    PDH_STATUS status = PdhGetFormattedCounterArray(counter_handle, value_format, &buffer_size, &item_count, item_array);
    if (PDH_MORE_DATA == status)
    {
        buffer.resize(buffer_size);
        item_array = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM *>(&buffer[0]);
        status = PdhGetFormattedCounterArray(counter_handle, value_format, &buffer_size, &item_count, item_array);
    }

    if (ERROR_SUCCESS != status)
    {
        item_array = nullptr;
        item_count = 0;
        return (false);
    }

    return (item_count > 0);
}

static bool get_processor_utilization_percentage(PDH_HCOUNTER counter_handle, std::vector<char> & buffer, SystemSnapshot & system_snapshot)
{
    PDH_FMT_COUNTERVALUE_ITEM * item_array = nullptr;
    ULONG item_count = 0;
    if (!get_formatted_counter_array(counter_handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, buffer, item_array, item_count))
    {
        return (false);
    }

    SystemResource & system_resource = system_snapshot.system_resource;
    system_resource.cpu_usage = 0;
    for (ULONG item_index = 0; item_index < item_count; ++item_index)
    {
        PDH_FMT_COUNTERVALUE_ITEM & item = item_array[item_index];
        system_resource.cpu_usage += item.FmtValue.doubleValue;
    }

    return (true);
}

static bool get_process_gpu_utilization_percentage(PDH_HCOUNTER counter_handle, std::vector<char> & buffer, SystemSnapshot & system_snapshot)
{
    PDH_FMT_COUNTERVALUE_ITEM * item_array = nullptr;
    ULONG item_count = 0;
    if (!get_formatted_counter_array(counter_handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, buffer, item_array, item_count))
    {
        return (false);
    }

    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;

    for (std::map<uint32_t, ProcessSnapshot>::iterator iter = process_snapshot_map.begin(); process_snapshot_map.end() != iter; ++iter)
    {
        ProcessResource & process_resource = iter->second.process_resource;
        process_resource.gpu_3d_usage = 0;
        process_resource.gpu_vr_usage = 0;
        process_resource.gpu_enc_usage = 0;
        process_resource.gpu_dec_usage = 0;
    }

    SystemResource & system_resource = system_snapshot.system_resource;
    system_resource.gpu_3d_usage = 0;
    system_resource.gpu_vr_usage = 0;
    system_resource.gpu_enc_usage = 0;
    system_resource.gpu_dec_usage = 0;

    for (ULONG item_index = 0; item_index < item_count; ++item_index)
    {
        /*
         * NVIDIA
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_0_engtype_3D
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_10_engtype_Compute_1
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_11_engtype_VR
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_12_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_13_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_14_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_15_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_1_engtype_Compute_0
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_2_engtype_LegacyOverlay
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_3_engtype_VideoDecode
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_4_engtype_Security
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_5_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_6_engtype_Copy
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_7_engtype_VideoEncode
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_8_engtype_Graphics_1
         *    pid_25832_luid_0x00000000_0x0000DABC_phys_0_eng_9_engtype_Cuda
         *  AMD
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_0_engtype_3D - 25.7208
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_10_engtype_Timer 0 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_11_engtype_Security 1 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_12_engtype_Video Decode 1 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_13_engtype_Video Encode 0 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_14_engtype_Video Codec 0 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_1_engtype_High Priority 3D - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_2_engtype_High Priority Compute - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_3_engtype_True Audio 0 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_4_engtype_True Audio 1 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_5_engtype_Compute 3 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_6_engtype_Compute 0 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_7_engtype_Compute 1 - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_8_engtype_Copy - 0
         *    pid_18360_luid_0x00000000_0x0000B750_phys_0_eng_9_engtype_Copy - 0
         */
        double gpu_3d_usage = 0;
        double gpu_vr_usage = 0;
        double gpu_enc_usage = 0;
        double gpu_dec_usage = 0;

        PDH_FMT_COUNTERVALUE_ITEM & item = item_array[item_index];
        if (nullptr != strstr(item.szName, "_3D"))
        {
            gpu_3d_usage = item.FmtValue.doubleValue;
        }
        else if (nullptr != strstr(item.szName, "_VR"))
        {
            gpu_vr_usage = item.FmtValue.doubleValue;
        }
        else if (nullptr != strstr(item.szName, "Encode") || nullptr != strstr(item.szName, "Codec"))
        {
            gpu_enc_usage = item.FmtValue.doubleValue;
        }
        else if (nullptr != strstr(item.szName, "Decode"))
        {
            gpu_dec_usage = item.FmtValue.doubleValue;
        }
        else
        {
            continue;
        }

        const char * pid_beg = item.szName + 4;
        const char * pid_end = strchr(pid_beg, '_');
        uint32_t process_id = atoi(std::string(pid_beg, pid_end).c_str());
        std::map<uint32_t, ProcessHelper>::iterator iter_helper = process_helper_map.find(process_id);
        if (process_helper_map.end() != iter_helper)
        {
            std::map<uint32_t, ProcessSnapshot>::iterator iter_snapshot = process_snapshot_map.find(iter_helper->second.process_ancestor);
            if (process_snapshot_map.end() != iter_snapshot)
            {
                ProcessResource & process_resource = iter_snapshot->second.process_resource;
                process_resource.gpu_3d_usage += gpu_3d_usage;
                process_resource.gpu_vr_usage += gpu_vr_usage;
                process_resource.gpu_enc_usage += gpu_enc_usage;
                process_resource.gpu_dec_usage += gpu_dec_usage;
            }
        }
        system_resource.gpu_3d_usage += gpu_3d_usage;
        system_resource.gpu_vr_usage += gpu_vr_usage;
        system_resource.gpu_enc_usage += gpu_enc_usage;
        system_resource.gpu_dec_usage += gpu_dec_usage;
    }

    return (true);
}

static bool get_process_gpu_dedicated_memory_usage(PDH_HCOUNTER counter_handle, std::vector<char> & buffer, SystemSnapshot & system_snapshot)
{
    PDH_FMT_COUNTERVALUE_ITEM * item_array = nullptr;
    ULONG item_count = 0;
    if (!get_formatted_counter_array(counter_handle, PDH_FMT_LARGE, buffer, item_array, item_count))
    {
        return (false);
    }

    std::map<uint32_t, ProcessHelper> & process_helper_map = system_snapshot.process_helper_map;
    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = system_snapshot.process_snapshot_map;

    for (std::map<uint32_t, ProcessSnapshot>::iterator iter = process_snapshot_map.begin(); process_snapshot_map.end() != iter; ++iter)
    {
        ProcessResource & process_resource = iter->second.process_resource;
        process_resource.gpu_mem_usage = 0;
    }

    SystemResource & system_resource = system_snapshot.system_resource;
    system_resource.gpu_mem_usage = 0;

    for (ULONG item_index = 0; item_index < item_count; ++item_index)
    {
        /*
         * pid_25832_luid_0x000000_0x00DABC_phys_0
         */
        PDH_FMT_COUNTERVALUE_ITEM & item = item_array[item_index];
        uint64_t gpu_mem_usage = static_cast<uint64_t>(item.FmtValue.largeValue);
        const char * pid_beg = item.szName + 4;
        const char * pid_end = strchr(pid_beg, '_');
        uint32_t process_id = atoi(std::string(pid_beg, pid_end).c_str());
        std::map<uint32_t, ProcessHelper>::iterator iter_helper = process_helper_map.find(process_id);
        if (process_helper_map.end() != iter_helper)
        {
            std::map<uint32_t, ProcessSnapshot>::iterator iter_snapshot = process_snapshot_map.find(iter_helper->second.process_ancestor);
            if (process_snapshot_map.end() != iter_snapshot)
            {
                ProcessResource & process_resource = iter_snapshot->second.process_resource;
                process_resource.gpu_mem_usage += gpu_mem_usage;
            }
        }
        system_resource.gpu_mem_usage += gpu_mem_usage;
    }

    return (true);
}

static bool get_system_gpu_dedicated_memory_total(SystemSnapshot & system_snapshot)
{
    std::list<std::string> & graphics_card_list = system_snapshot.graphics_card_list;
    graphics_card_list.clear();

    SystemResource & system_resource = system_snapshot.system_resource;
    system_resource.gpu_mem_total = 0;

    ATL::CComPtr<IDXGIFactory1> dxgi_factory;
    if (CreateDXGIFactory1(IsWindowsVistaSP2OrGreater() ? __uuidof(IDXGIFactory2) : __uuidof(IDXGIFactory1), reinterpret_cast<void **>(&dxgi_factory)) < 0)
    {
        return (false);
    }

    UINT adapter_index = 0;
    while (true)
    {
        ATL::CComPtr<IDXGIAdapter1> dxgi_adapter;
        if (DXGI_ERROR_NOT_FOUND == dxgi_factory->EnumAdapters1(adapter_index++, &dxgi_adapter))
        {
            break;
        }

        DXGI_ADAPTER_DESC adapter_desc = { 0x0 };
        HRESULT result = dxgi_adapter->GetDesc(&adapter_desc);
        if (result < 0)
        {
            continue;
        }

        if (0x1414 == adapter_desc.VendorId)
        {
            continue;
        }

        graphics_card_list.emplace_back(Goofer::unicode_to_utf8(adapter_desc.Description));
        system_resource.gpu_mem_total += static_cast<uint64_t>(adapter_desc.DedicatedVideoMemory);
    }

    return (true);
}

ResourceMonitorImpl::ResourceMonitorImpl()
    : m_running(false)
    , m_query_thread()
    , m_query_event(nullptr)
    , m_query_handle(nullptr)
    , m_processor_counter(nullptr)
    , m_gpu_engine_counter(nullptr)
    , m_gpu_memory_counter(nullptr)
    , m_system_snapshot()
    , m_system_snapshot_mutex()
{

}

ResourceMonitorImpl::~ResourceMonitorImpl()
{
    exit();
}

bool ResourceMonitorImpl::init()
{
    exit();

    do
    {
        RUN_LOG_DBG("resource monitor init begin");

        m_running = true;

        if (!get_system_cpu_count(m_system_snapshot))
        {
            RUN_LOG_ERR("resource monitor init failure while get system cpu count failed");
            break;
        }

        if (!get_system_memory_usage(m_system_snapshot))
        {
            RUN_LOG_ERR("resource monitor init failure while get system memory usage failed");
            break;
        }

        if (!get_system_gpu_dedicated_memory_total(m_system_snapshot))
        {
            RUN_LOG_ERR("resource monitor init failure while get system gpu dedicated memory total failed");
            break;
        }

        m_query_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (nullptr == m_query_event)
        {
            RUN_LOG_ERR("resource monitor init failure while create query event failed");
            break;
        }

        if (ERROR_SUCCESS != PdhOpenQuery(nullptr, 0, &m_query_handle) || nullptr == m_query_handle)
        {
            RUN_LOG_ERR("resource monitor init failure while create query handle failed");
            break;
        }

        if (ERROR_SUCCESS != PdhAddCounter(m_query_handle, "\\Processor(_Total)\\% Processor Time", 0, &m_processor_counter))
        {
            RUN_LOG_ERR("resource monitor init failure while add processor time counter failed");
            break;
        }

        if (ERROR_SUCCESS != PdhAddCounter(m_query_handle, "\\GPU Engine(*)\\Utilization Percentage", 0, &m_gpu_engine_counter))
        {
            RUN_LOG_ERR("resource monitor init failure while add gpu engine utilization percentage counter failed");
            break;
        }

        if (ERROR_SUCCESS != PdhAddCounter(m_query_handle, "\\GPU Process Memory(*)\\Dedicated Usage", 0, &m_gpu_memory_counter))
        {
            RUN_LOG_ERR("resource monitor init failure while add gpu process memory dedicated usage counter failed");
            break;
        }

        if (ERROR_SUCCESS != PdhCollectQueryDataEx(m_query_handle, 5, m_query_event))
        {
            RUN_LOG_ERR("resource monitor init failure while collect query data start failed");
            break;
        }

        m_query_thread = std::thread(&ResourceMonitorImpl::query_resource_thread, this);
        if (!m_query_thread.joinable())
        {
            RUN_LOG_ERR("resource monitor init failure while query resource thread create failed");
            break;
        }

        RUN_LOG_DBG("resource monitor init success");

        return (true);
    } while (false);

    exit();

    return (false);
}

void ResourceMonitorImpl::exit()
{
    if (m_running)
    {
        RUN_LOG_DBG("resource monitor exit begin");

        m_running = false;

        if (m_query_thread.joinable())
        {
            SetEvent(m_query_event);
            RUN_LOG_DBG("resource monitor exit while query resource thread exit begin");
            m_query_thread.join();
            RUN_LOG_DBG("resource monitor exit while query resource thread exit end");
        }

        if (nullptr != m_processor_counter)
        {
            PdhRemoveCounter(m_processor_counter);
            m_processor_counter = nullptr;
        }

        if (nullptr != m_gpu_engine_counter)
        {
            PdhRemoveCounter(m_gpu_engine_counter);
            m_gpu_engine_counter = nullptr;
        }

        if (nullptr != m_gpu_memory_counter)
        {
            PdhRemoveCounter(m_gpu_memory_counter);
            m_gpu_memory_counter = nullptr;
        }

        if (nullptr != m_query_handle)
        {
            PdhCloseQuery(m_query_handle);
            m_query_handle = nullptr;
        }

        if (nullptr != m_query_event)
        {
            CloseHandle(m_query_event);
            m_query_event = nullptr;
        }

        RUN_LOG_DBG("resource monitor exit end");
    }
}

void ResourceMonitorImpl::query_resource_thread()
{
    std::vector<char> buffer;

    while (m_running)
    {
        if (WAIT_OBJECT_0 != WaitForSingleObject(m_query_event, INFINITE))
        {
            break;
        }

        if (!m_running)
        {
            break;
        }

        std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

        update_process_tree(m_system_snapshot);
        get_process_cpu_usage(m_system_snapshot);
        get_process_memory_usage(m_system_snapshot);
        get_system_memory_usage(m_system_snapshot);
        get_processor_utilization_percentage(m_processor_counter, buffer, m_system_snapshot);
        get_process_gpu_utilization_percentage(m_gpu_engine_counter, buffer, m_system_snapshot);
        get_process_gpu_dedicated_memory_usage(m_gpu_memory_counter, buffer, m_system_snapshot);
    }
}

bool ResourceMonitorImpl::append_process(uint32_t process_id, bool process_tree)
{
    if (!m_running || 0 == process_id)
    {
        return (false);
    }

    std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

    if (append_process_to_monitor(m_system_snapshot, process_id, process_tree))
    {
        RUN_LOG_DBG("append process (%u) tree (%s) to monitor success", process_id, process_tree ? "true" : "false");
        return (true);
    }
    else
    {
        RUN_LOG_ERR("append process (%u) tree (%s) to monitor failure", process_id, process_tree ? "true" : "false");
        return (false);
    }
}

bool ResourceMonitorImpl::remove_process(uint32_t process_id)
{
    if (!m_running || 0 == process_id)
    {
        return (false);
    }

    std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

    if (remove_process_from_monitor(m_system_snapshot, process_id))
    {
        RUN_LOG_DBG("remove process (%u) from monitor success", process_id);
        return (true);
    }
    else
    {
        RUN_LOG_ERR("remove process (%u) from monitor failure", process_id);
        return (false);
    }
}

bool ResourceMonitorImpl::get_process_resource(uint32_t process_id, ProcessResource & process_resource)
{
    if (!m_running || 0 == process_id)
    {
        return (false);
    }

    std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

    std::map<uint32_t, ProcessSnapshot> & process_snapshot_map = m_system_snapshot.process_snapshot_map;
    std::map<uint32_t, ProcessSnapshot>::iterator iter_snapshot = process_snapshot_map.find(process_id);
    if (process_snapshot_map.end() != iter_snapshot)
    {
        memcpy(&process_resource, &iter_snapshot->second.process_resource, sizeof(process_resource));
        std::map<uint32_t, ProcessLeaf> & process_leaf_map = m_system_snapshot.process_leaf_map;
        std::map<uint32_t, ProcessLeaf>::iterator iter_leaf = process_leaf_map.find(process_id);
        if (process_leaf_map.end() != iter_leaf)
        {
            std::set<uint32_t> & process_descendant_set = iter_leaf->second.process_descendant_set;
            for (std::set<uint32_t>::iterator iter_descendant = process_descendant_set.begin(); process_descendant_set.end() != iter_descendant; ++iter_descendant)
            {
                process_resource += process_snapshot_map[*iter_descendant];
            }
        }
        return (true);
    }

    return (false);
}

bool ResourceMonitorImpl::get_system_resource(SystemResource & system_resource)
{
    if (!m_running)
    {
        return (false);
    }

    std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

    memcpy(&system_resource, &m_system_snapshot.system_resource, sizeof(system_resource));

    return (true);
}

bool ResourceMonitorImpl::get_graphics_cards(std::list<std::string> & graphics_card_list)
{
    if (!m_running)
    {
        return (false);
    }

    std::lock_guard<std::mutex> locker(m_system_snapshot_mutex);

    graphics_card_list = m_system_snapshot.graphics_card_list;

    return (true);
}
