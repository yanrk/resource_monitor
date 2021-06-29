/********************************************************
 * Description : resource monitor
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 2.0
 * History     :
 * Copyright(C): 2021-2022
 ********************************************************/

#include "resource_monitor.h"
#include "resource_monitor_impl.h"

ResourceMonitor::ResourceMonitor()
    : m_resource_monitor_impl(nullptr)
{

}

ResourceMonitor::~ResourceMonitor()
{
    exit();
}

bool ResourceMonitor::init()
{
    exit();

    do
    {
        m_resource_monitor_impl = new ResourceMonitorImpl;
        if (nullptr == m_resource_monitor_impl)
        {
            break;
        }

        if (!m_resource_monitor_impl->init())
        {
            break;
        }

        return (true);
    } while (false);

    exit();

    return (false);
}

void ResourceMonitor::exit()
{
    if (nullptr != m_resource_monitor_impl)
    {
        m_resource_monitor_impl->exit();
        delete m_resource_monitor_impl;
        m_resource_monitor_impl = nullptr;
    }
}

bool ResourceMonitor::append_process(uint32_t process_id, bool process_tree)
{
    return (nullptr != m_resource_monitor_impl && m_resource_monitor_impl->append_process(process_id, process_tree));
}

bool ResourceMonitor::remove_process(uint32_t process_id)
{
    return (nullptr != m_resource_monitor_impl && m_resource_monitor_impl->remove_process(process_id));
}

bool ResourceMonitor::get_process_resource(uint32_t process_id, ProcessResource & process_resource)
{
    return (nullptr != m_resource_monitor_impl && m_resource_monitor_impl->get_process_resource(process_id, process_resource));
}

bool ResourceMonitor::get_system_resource(SystemResource & system_resource)
{
    return (nullptr != m_resource_monitor_impl && m_resource_monitor_impl->get_system_resource(system_resource));
}

bool ResourceMonitor::get_graphics_cards(std::list<std::string> & graphics_card_list)
{
    return (nullptr != m_resource_monitor_impl && m_resource_monitor_impl->get_graphics_cards(graphics_card_list));
}
