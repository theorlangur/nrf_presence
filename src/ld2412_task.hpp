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
        Bluetooth,
        SetBasicCfg,
        SetLightSenseCfg,
        SetEnergyThresholds,
        RunBackAnalysis,
        ConfigureCollectStatistics,
        SnapshotStatistics
    };

    enum class notification_id_t: uint8_t
    {
        BackgroundAnalysisDone
        , BackgroundAnalysisError
    };

    struct basic_cfg_t{
        hlk::LD2412::DistanceRes resolution = hlk::LD2412::DistanceRes::_0_50;
        uint8_t gate_from = 0;
        uint8_t gate_to = 13;
        uint16_t clear_delay = 0;
    };
    struct light_sense_cfg_t{
        hlk::LD2412::LightSensitivity mode = hlk::LD2412::LightSensitivity::Off;;
        uint8_t threshold = 0;
    };
    struct energy_thresholds_cfg_t{
        hlk::LD2412::gate_array_t const& still_thresholds;
        hlk::LD2412::gate_array_t const& move_thresholds;
    };
    struct run_background_analysis_t{
        enum class Result
        {
            Ok,
            Failed
        };
        using callback_t = void(*)(Result result);
        callback_t cb;
    };
    struct collect_statistics_cfg_t{
        uint8_t window_size = 0;//samples 0 -> off
    };
    struct snapshot_statistics_cfg_t{
        using callback_t = void(*)(hlk::LD2412::energy_stat_array_t const& still, hlk::LD2412::energy_stat_array_t const& move);
        callback_t cb;
    };
    struct restart_cfg_t{};
    struct bt_cfg_t{bool on;};

    using QueueItem = std::variant<
        restart_cfg_t
        , bt_cfg_t
        , basic_cfg_t
        , light_sense_cfg_t
        , energy_thresholds_cfg_t
        , run_background_analysis_t
        , collect_statistics_cfg_t
        , snapshot_statistics_cfg_t
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
        void collect_sample();
        void check_back_analysis();

        /**********************************************************************/
        /* Thread-related stuff                                               */
        /**********************************************************************/
        static void thread_func(void *, void *, void *);

        static constexpr uint8_t MAX_STAT_SAMPLE_SIZE = 128;
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

        run_background_analysis_t::callback_t m_BackgroundAnalysisDoneCB = {};

        struct stat_sample_t
        {
            hlk::LD2412::gate_array_t still_energies;
            hlk::LD2412::gate_array_t move_energies;
        };

        stat_sample_t m_StatSampleBuffer[MAX_STAT_SAMPLE_SIZE];
        size_t m_StatSampleIndex = 0;
        size_t m_StatSampleCount = 0;
        size_t m_TotalSamplesWritten = 0;

        size_t m_LightSensorUpdateInterval = 5;//seconds
        size_t m_BackgroundCheckInterval = 2;//seconds
    };
}
#endif
