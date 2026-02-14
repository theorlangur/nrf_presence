#ifndef LD2412_TASK_HPP_
#define LD2412_TASK_HPP_

#include <optional>
#include <variant>
#include <nrf_general/lib_msgq_typed.hpp>
#include <nrf_uart/periphery/lib_ld2412.hpp>

namespace ld2412
{
    enum class err_t
    {
        Ok,
        Restart,
        Bluetooth
    };

    enum class notification_id_t: uint8_t
    {
        BackgroundAnalysisDone
        , BackgroundAnalysisError
    };

    struct basic_cfg_t{
    };
    struct restart_cfg_t{};
    struct bt_cfg_t{bool on;};

    using QueueItem = std::variant<
        restart_cfg_t
        , bt_cfg_t
        , basic_cfg_t
    >;
    using Queue = msgq::Queue<QueueItem,4>;

    class Instance
    {
    public:
        using err_callback_t = void(*)(err_t);
        using notify_callback_t = void(*)(notification_id_t);

        Instance(Queue &q, const struct device *uart, const char* thread_name);

        hlk::LD2412* setup(err_callback_t err, notify_callback_t notification);
        hlk::LD2412* sensor();

    private:
        void mainloop();

        /**********************************************************************/
        /* Thread-related stuff                                               */
        /**********************************************************************/
        static void thread_func(void *, void *, void *);

        static constexpr size_t THREAD_STACK_SIZE = 1024 * 2;
        static constexpr size_t THREAD_PRIORITY=7;

        alignas(16) k_thread_stack_t m_ThreadStack[THREAD_STACK_SIZE];
        k_thread m_Thread;
        const char *m_ThreadName = nullptr;
        k_tid_t m_ThreadID = {};

        /**********************************************************************/
        /* End of thread-related stuff                                        */
        /**********************************************************************/
        Queue &m_Q;
        const struct device *m_pUART;
        hlk::LD2412 m_Sensor;

        err_callback_t m_ErrCB = nullptr;
        notify_callback_t m_NotifyCB = nullptr;
    };
}
#endif
