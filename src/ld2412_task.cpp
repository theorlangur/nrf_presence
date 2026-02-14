#include "ld2412_task.hpp"
#include <lib_misc_helpers.hpp>

namespace ld2412
{
    Instance::Instance(Queue &q, const struct device *uart, const char* thread_name):
	m_ThreadName(thread_name)
	, m_Q(q)
	, m_pUART(uart)
	, m_Sensor(uart)
    {
    }

    hlk::LD2412* Instance::setup(err_callback_t err, notify_callback_t notification)
    {
	m_ThreadID = {};
	if (!device_is_ready(m_pUART))
	{
	    printk("uart for ld2412 is not ready\r\n");
	    return {};
	}

	auto r = m_Sensor.Init();
	if (!r)
	    return {};

	m_ErrCB = err;
	m_NotifyCB = notification;

	m_ThreadID = k_thread_create(&m_Thread 
	    ,m_ThreadStack
	    ,sizeof(m_ThreadStack)
	    ,thread_func
	    ,this, nullptr, nullptr
	    ,THREAD_PRIORITY
	    , 0
	    , {0});
	if (!m_ThreadID)
	    return {};
	k_thread_name_set(m_ThreadID, m_ThreadName);
	return &m_Sensor;
    }

    hlk::LD2412* Instance::sensor()
    {
	if (m_ThreadID)
	    return &m_Sensor;
	else
	    return {};
    }

    void Instance::thread_func(void *_pThis, void *, void *)
    {
	Instance *pThis = (Instance*)(_pThis);
	return pThis->mainloop();
    }

    void Instance::mainloop()
    {
	QueueItem q;
	while(1)
	{
	    m_Q >> q;
	    std::visit(
		overloaded{
		    [&](restart_cfg_t const&)
		    {
			//TODO: implement
		    },
		    [&](bt_cfg_t const& cfg)
		    {
			//TODO: implement
		    },
		    [&](basic_cfg_t const& cfg)
		    {
			//TODO: implement
		    }
		},
		 q
	    );
	}
    }
}
