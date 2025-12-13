#ifndef LIB_UART_H_
#define LIB_UART_H_

#include <zephyr/kernel.h>
#include "lib_ret_err.h"
#include <lib_formatter.hpp>
#include <expected>
#include <zephyr/drivers/uart.h>

namespace uart
{
    using duration_ms_t = int;
    static constexpr const duration_ms_t kForever = -2;
    static constexpr const duration_ms_t kDefault = -1;
    static constexpr const int ERR_OK = 0;

    class Channel
    {
    public:
        using Ref = std::reference_wrapper<Channel>;
        using ExpectedResult = std::expected<Ref, Err>;
        static constexpr const int kUARTAsyncBufSize = 8;
        static constexpr const int32_t kUARTRxTimeoutBits = 9 * 2;

        template<typename V>
        using RetVal = RetValT<Ref, V>;

        template<typename V>
        using ExpectedValue = std::expected<RetVal<V>, Err>;

        static const constexpr duration_ms_t kDefaultWait = duration_ms_t{-1};

        class RxBlock
        {
        public:
            RxBlock(Channel &c, uint8_t *pData, size_t len);
            ~RxBlock();

            void Stop();
        private:
            Channel &m_C;
            bool m_Stopped = false;
        };

        class ChangeWait
        {
        public:
            ChangeWait(Channel &c, duration_ms_t w);
            ~ChangeWait();
        private:
            Channel &m_C;
            duration_ms_t m_PrevWait;
        };

        Channel(const struct device *pUART);
        ~Channel();

        ExpectedResult Configure();

        void SetDefaultWait(duration_ms_t w) { m_DefaultWait = w; }
        duration_ms_t GetDefaultWait() const { return m_DefaultWait; }

        ExpectedResult Open();
        ExpectedResult Close();

        void AllowReadUpTo(uint8_t *pData, size_t len);
        void StopReading(bool dbg = false);

        ExpectedResult Send(const uint8_t *pData, size_t len);
        ExpectedValue<size_t> Read(uint8_t *pBuf, size_t len, duration_ms_t wait=kDefaultWait);
        ExpectedResult Drain(bool stopAtEnd);
        ExpectedResult WaitAllSent();

        ExpectedValue<uint8_t> ReadByte(duration_ms_t wait=kDefaultWait);
        ExpectedValue<uint8_t> PeekByte(duration_ms_t wait=kDefaultWait);

        bool HasOverflow() const { return m_Overflow; }

        //using EventCallback = GenericCallback<void(uart_event_type_t)>;
        //void SetEventCallback(EventCallback cb) { m_EventCallback = std::move(cb); }
        //bool HasEventCallback() const { return (bool)m_EventCallback; }

        bool m_Dbg;
    private:
        static void uart_async_callback(const struct device *dev, uart_event *evt, void *user_data);
        int uart_send();
        int uart_recv();

        size_t ReadInternal(uint8_t *pBuf, size_t len);

        const struct device *m_pUART = nullptr;
        struct k_sem m_tx_sem;
        struct k_sem m_rx_sem;
        struct k_sem m_rx_ctrl;
        duration_ms_t m_DefaultWait{0};
        bool m_rx_disable_request = false;
        bool m_rx_enable_request = false;

        bool m_rx_state = false;

        uint8_t m_UARTAsyncBufs[2][kUARTAsyncBufSize];
        int m_UARTAsyncBufNext = -1;
        int32_t m_UARTRxTimeoutUS = 200;
        //receive buf
        uint8_t *m_pRecvBuf = nullptr;
        int m_RecvLen = 0;

        //target rcv
        uint8_t *m_pInternalRecvBuf = nullptr;
        int m_InternalRecvBufLen = 0;
        int m_InternalRecvBufNextWrite = 0;
        int m_InternalRecvBufNextRead = 0;
        bool m_Overflow = false;

        //transmitt buf
        const uint8_t *m_pSendBuf = nullptr;
        int m_SendLen = 0;

        bool m_HasPeekByte = false;
        uint8_t m_PeekByte = 0;

        //std::atomic<bool> m_DataReady={false};
        //EventCallback m_EventCallback;

        //bool m_DbgPrintSend = false;
    };
}
#endif
