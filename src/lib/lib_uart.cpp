#define FORCE_FMT
#define PRINTF_FUNC(...) printk(__VA_ARGS__)

#include "lib_uart.h"
#include <functional>
#include "lib_misc_helpers.hpp"

namespace uart
{
    Channel::RxBlock::RxBlock(Channel &c, uint8_t *pData, size_t len):
	m_C(c)
    {
	m_C.AllowReadUpTo(pData, len);
    }

    Channel::RxBlock::~RxBlock()
    {
	Stop();
    }

    void Channel::RxBlock::Stop()
    {
	if (!m_Stopped)
	{
	    m_Stopped = true;
	    m_C.StopReading();
	}
    }

    Channel::ChangeWait::ChangeWait(Channel &c, duration_ms_t w):
	m_C(c)
    {
	m_PrevWait = m_C.GetDefaultWait();
	m_C.SetDefaultWait(w);
    }

    Channel::ChangeWait::~ChangeWait()
    {
	m_C.SetDefaultWait(m_PrevWait);
    }

    Channel::Channel(const struct device *pUART):
	m_pUART(pUART)
    {
    }

    Channel::~Channel()
    {
    }

    Channel::ExpectedResult Channel::Configure()
    {
	uart_config cfg;
	uart_config_get(m_pUART, &cfg);
	//m_UARTRxTimeoutUS = (1'000'000 * kUARTRxTimeoutBits) / cfg.baudrate;
	m_UARTRxTimeoutUS = 1'000 * m_DefaultWait;
	if (m_UARTRxTimeoutUS == 0) m_UARTRxTimeoutUS = 4;

	k_sem_init(&m_rx_sem, 0, 1);
	k_sem_init(&m_tx_sem, 1, 1);
	k_sem_init(&m_rx_ctrl, 0, 1);

	uart_callback_set(m_pUART, uart_async_callback, this);

	return std::ref(*this);
    }

    Channel::ExpectedResult Channel::Open()
    {
	return std::ref(*this);
    }

    Channel::ExpectedResult Channel::Close()
    {
	return std::ref(*this);
    }

    void Channel::uart_async_callback(const struct device *dev, uart_event *evt, void *user_data)
    {
	Channel *pC = (Channel *)user_data;
	switch(evt->type)
	{
	    case UART_TX_ABORTED:
		break;
	    case UART_TX_DONE:
	    {
		k_sem_give(&pC->m_tx_sem);
		if (pC->m_rx_enable_request)
		{
		    pC->m_rx_enable_request = false;
		    uart_rx_enable(pC->m_pUART, pC->m_UARTAsyncBufs[0], kUARTAsyncBufSize, pC->m_UARTRxTimeoutUS);
		}
	    }
	    break;
	    case UART_RX_BUF_REQUEST:
	    {
		if (pC->m_rx_disable_request)
		{
		    pC->m_rx_disable_request = false;
		    uart_rx_disable(dev);
		    break;
		}

		if (pC->m_UARTAsyncBufNext != -1)
		{
		    pC->m_rx_state = true;
		    pC->m_UARTAsyncBufNext ^= 1;
		    uart_rx_buf_rsp(dev
			    , pC->m_UARTAsyncBufs[pC->m_UARTAsyncBufNext]
			    , kUARTAsyncBufSize);
		}else
		{
		    //pC->m_rx_state = false;
		}
	    }
	    break;
	    case UART_RX_BUF_RELEASED:
		if (pC->m_rx_disable_request)
		{
		    pC->m_rx_disable_request = false;
		    uart_rx_disable(dev);
		}
		break;
	    case UART_RX_DISABLED:
		pC->m_rx_state = false;
		pC->m_UARTAsyncBufNext = -1;
		k_sem_give(&pC->m_rx_ctrl);
		break;
	    case UART_RX_RDY:
		{
		    if (pC->m_pInternalRecvBuf)
		    {
			int to_write = evt->data.rx.len;
			int read_pos = pC->m_InternalRecvBufNextRead;
			int write_pos = pC->m_InternalRecvBufNextWrite;
			if (read_pos < write_pos) read_pos += pC->m_InternalRecvBufLen;
			if ((write_pos != read_pos) && ((write_pos + to_write) >= read_pos))
			{
			    pC->m_Overflow = true;
			    pC->m_InternalRecvBufNextRead = ((write_pos + to_write + 1) % pC->m_InternalRecvBufLen);
			}

			int left = pC->m_InternalRecvBufLen - pC->m_InternalRecvBufNextWrite;
			if (left <= to_write) 
			    to_write = left;

			if (to_write)
			{
			    memcpy(pC->m_pInternalRecvBuf + pC->m_InternalRecvBufNextWrite, 
				    evt->data.rx.buf + evt->data.rx.offset,
				    to_write);
			    pC->m_InternalRecvBufNextWrite += to_write;
			    pC->m_InternalRecvBufNextWrite %= pC->m_InternalRecvBufLen;
			    int orig = to_write;
			    to_write = evt->data.rx.len - to_write;
			    if (to_write)
			    {
				memcpy(pC->m_pInternalRecvBuf + pC->m_InternalRecvBufNextWrite, 
					evt->data.rx.buf + evt->data.rx.offset + orig,
					to_write);
				pC->m_InternalRecvBufNextWrite += to_write;
			    }
			    k_sem_give(&pC->m_rx_sem);
			}else
			{
			}
		    }
		    else
		    {
		    }
		}
		break;
	    case UART_RX_STOPPED:
		if (!pC->m_rx_disable_request && pC->m_pInternalRecvBuf && pC->m_UARTAsyncBufNext != -1)
		{
		    pC->m_rx_state = false;
		    pC->m_UARTAsyncBufNext = 0;
		    uart_rx_enable(dev
			    , pC->m_UARTAsyncBufs[0]
			    , kUARTAsyncBufSize
			    , pC->m_UARTRxTimeoutUS);
		}
		break;
	}
    }

    int Channel::uart_send()
    {
	if (!m_SendLen) return 0;
	int b = uart_fifo_fill(m_pUART, m_pSendBuf, m_SendLen);
	if (b < 0) return b;
	m_pSendBuf += b;
	m_SendLen -= b;
	return b;
    }

    int Channel::uart_recv()
    {
	if (!m_RecvLen) return 0;
	int b = uart_fifo_read(m_pUART, m_pRecvBuf, m_RecvLen);
	if (b < 0) return b;
	m_pRecvBuf += b;
	m_RecvLen -= b;
	return b;
    }

    Channel::ExpectedResult Channel::Send(const uint8_t *pData, size_t len)
    {
	CALL_WITH_EXPECTED("Channel::Send", k_sem_take(&m_tx_sem, Z_TIMEOUT_MS(m_DefaultWait)));
	if (m_Dbg)
	{
	    FMT_PRINTLN("Channel::Send: {}", std::span<const char>{(const char*)pData, len});
	}
	m_pSendBuf = pData;
	m_SendLen = len;
	CALL_WITH_EXPECTED("Channel::Send (uart_tx)", uart_tx(m_pUART, pData, len, SYS_FOREVER_US));
	return std::ref(*this);
    }

    void Channel::AllowReadUpTo(uint8_t *pData, size_t len)
    {
	if (m_Dbg)
	    FMT_PRINTLN("Channel::AllowReadUpTo: {}", len);
	m_pInternalRecvBuf = pData;
        m_InternalRecvBufLen = len;
        m_InternalRecvBufNextWrite = 0;
        m_InternalRecvBufNextRead = 0;
	m_UARTAsyncBufNext = 0;
	m_rx_enable_request = true;
    }

    void Channel::StopReading(bool dbg)
    {
	m_rx_disable_request = true;
	int r = k_sem_take(&m_rx_ctrl, Z_TIMEOUT_MS(m_DefaultWait));

	if (r < 0)
	{
	    FMT_PRINTLN("Channel::StopReading: could not disable RX: {}", r);
	}else
	{
	    if (m_pInternalRecvBuf && m_InternalRecvBufNextWrite)
	    {
		if (dbg)
		{
		    FMT_PRINTLN("Channel::StopReading: data in buf: {}", std::span<const uint8_t>{(const uint8_t*)m_pInternalRecvBuf, (size_t)m_InternalRecvBufNextWrite});
		}
		else if (m_InternalRecvBufNextWrite != m_InternalRecvBufNextRead)
		{
		    if (m_Dbg)
			FMT_PRINTLN("Channel::StopReading: unread data in buf: {}", std::string_view{(const char*)m_pInternalRecvBuf + m_InternalRecvBufNextRead, (size_t)(m_InternalRecvBufNextWrite - m_InternalRecvBufNextRead)});
		}
	    }
	}
	m_pInternalRecvBuf = nullptr;
        m_InternalRecvBufLen = 0;
        m_InternalRecvBufNextWrite = 0;
        m_InternalRecvBufNextRead = 0;
	m_UARTAsyncBufNext = -1;
    }

    size_t Channel::ReadInternal(uint8_t *pBuf, size_t len)
    {
	int read_bytes = 0;
	if (m_InternalRecvBufNextRead > m_InternalRecvBufNextWrite)
	{
	    int avail = m_InternalRecvBufLen - m_InternalRecvBufNextRead;
	    int n = std::min(avail, (int)len);
	    memcpy(pBuf, m_pInternalRecvBuf + m_InternalRecvBufNextRead, n);
	    if (m_Dbg) for(int i = 0; i < n; ++i) printk("%c", *(m_pInternalRecvBuf + m_InternalRecvBufNextRead + i));
	    m_InternalRecvBufNextRead = (m_InternalRecvBufNextRead + n) % m_InternalRecvBufLen;
	    read_bytes += n;
	    if (read_bytes == len)
		return n;
	}

	int avail = m_InternalRecvBufNextWrite - m_InternalRecvBufNextRead;
	int left = len - read_bytes;
	int n = std::min(avail, left);
	memcpy(pBuf + read_bytes, m_pInternalRecvBuf + m_InternalRecvBufNextRead, n);
	if (m_Dbg) for(int i = 0; i < n; ++i) printk("%c", *(m_pInternalRecvBuf + m_InternalRecvBufNextRead + i));
	m_InternalRecvBufNextRead += n;
	read_bytes += n;
	return read_bytes;
    }

    Channel::ExpectedValue<size_t> Channel::Read(uint8_t *pBuf, size_t len, duration_ms_t wait)
    {
	m_Overflow = false;
	if (!len) 
	{
	    printk("Channel::Read: len=0\r\n");
	    return RetVal<size_t>{*this, size_t(0)};
	}
	if (wait == kDefaultWait) wait = m_DefaultWait;
	bool addedPeek = m_HasPeekByte;
	if (m_HasPeekByte)
	{
	    //printk("Channel::Read: peek byte of=%d\r\n", m_PeekByte);
	    pBuf[0] = m_PeekByte;
	    m_HasPeekByte = false;
	    --len;
	    ++pBuf;
	    if (!len) 
	    {
		//printk("Channel::Read: peek only\r\n");
		return RetVal<size_t>{*this, 1};
	    }
	}

	if (m_pInternalRecvBuf)
	{
	    int read = ReadInternal(pBuf, len);
	    if (read == len)
		return RetVal<size_t>{*this, len};
	    if (wait == 0)
		return RetVal<size_t>{*this, size_t(read)};

	    if (m_UARTAsyncBufNext == -1)
	    {
		printk("Read: restarting recv\r\n");
		m_UARTAsyncBufNext = 0;
		uart_rx_enable(m_pUART, m_UARTAsyncBufs[0], kUARTAsyncBufSize, m_UARTRxTimeoutUS);
	    }

	    //FMT_PRINTLN("Read len: {}; read: {}", len, read);
	    pBuf += read;
	    int left = (int)len - read;
	    while(left)
	    {
		//CALL_WITH_EXPECTED("Channel::Read(internal)", k_sem_take(&m_rx_sem, Z_TIMEOUT_MS(wait)));
		if (auto err = k_sem_take(&m_rx_sem, Z_TIMEOUT_MS(wait)); err != 0)
		{
		    printk("Read failed. write pos: %d; read pos: %d; (buf idx=%d; state=%d)\r\n", m_InternalRecvBufNextWrite, m_InternalRecvBufNextRead, m_UARTAsyncBufNext, m_rx_state);
		    //FMT_PRINTLN("Read failed. write pos: {}; read pos: {}; (buf idx={})", m_InternalRecvBufNextWrite, m_InternalRecvBufNextRead, m_UARTAsyncBufNext);
		    return std::unexpected(Err{"Channel::Read(internal)", err});
		}
		int read = ReadInternal(pBuf, left);
		pBuf += read;
		left -= read;
	    }
	    return RetVal<size_t>{*this, len};
	}
	return std::unexpected(Err{"Channel::Read(wrong state)", 0});
    }

    Channel::ExpectedResult Channel::Drain(bool stopAtEnd)
    {
	uint8_t buf[8];
	while(Read(buf, sizeof(buf), 0));

	if (stopAtEnd)
	{
	    if (m_pInternalRecvBuf)
		StopReading();

	    k_sem_reset(&m_rx_sem);
	}
	return std::ref(*this);
    }

    Channel::ExpectedResult Channel::WaitAllSent()
    {
	CALL_WITH_EXPECTED("Channel::WaitAllSent", k_sem_take(&m_tx_sem, Z_TIMEOUT_MS(m_DefaultWait)));
	k_sem_give(&m_tx_sem);
	return std::ref(*this);
    }

    Channel::ExpectedValue<uint8_t> Channel::ReadByte(duration_ms_t wait)
    {
	if (wait == kDefaultWait) wait = m_DefaultWait;
	uint8_t b;
	if (auto e = Read(&b, 1, wait); !e)
	    return std::unexpected(e.error());
	else if (auto l = e.value().v; !l)
	    return std::unexpected(::Err{"Channel::ReadByte no data", 0});
	else
	    return RetVal<uint8_t>{std::ref(*this), b};
    }

    Channel::ExpectedValue<uint8_t> Channel::PeekByte(duration_ms_t wait)
    {
	if (m_HasPeekByte)
	    return RetVal<uint8_t>{std::ref(*this), m_PeekByte};
	if (wait == kDefaultWait) wait = m_DefaultWait;
	if (auto e = ReadByte(wait); !e) return e;
	else{
	    m_HasPeekByte = true;
	    m_PeekByte = e.value().v;
	    return RetVal<uint8_t>{std::ref(*this), m_PeekByte};
	}
	return RetVal<uint8_t>{*this, 0};
    }
}
