#ifndef LIB_DFR_C4001_H_
#define LIB_DFR_C4001_H_

#include <zephyr/drivers/uart.h>
#include "lib_uart.h"
#include <span>
#include "lib_uart_primitives.h"
#include <lib_type_traits.hpp>

namespace dfr
{
    template<class T>
    concept Invokable = requires (T const& t) { t(); };


    template<class T>
    struct Sendable
    {
        static constexpr const bool value = false;
    };

    template<class T, size_t N> requires 
        (std::is_same_v<T, uint8_t> || std::is_same_v<T, const uint8_t> 
         || std::is_same_v<T, char> || std::is_same_v<T, const char>
         )
    struct Sendable<const T(&)[N]>
    {
        static constexpr const bool value = true;
        static auto send(uart::Channel &c, const T (&a)[N])
        {
            return c.Send((const uint8_t*)a, N - 1/*skip 0*/);
        }
    };

    template<class T, size_t N>
    struct Sendable<T[N]>: Sendable<T(&)[N]>{};

    template<class T, size_t N>
    struct Sendable<T(&)[N]>: Sendable<const T(&)[N]>{};

    template<>
    struct Sendable<const char*>
    {
        static constexpr const bool value = true;
        static auto send(uart::Channel &c, const char *a)
        {
            int l = strlen(a);
            return c.Send((const uint8_t*)a, l);
        }
    };

    template<>
    struct Sendable<std::span<const char>>
    {
        static constexpr const bool value = true;
        static auto send(uart::Channel &c, std::span<const char> const& s)
        {
            return c.Send((const uint8_t*)s.data(), s.size());
        }
    };

    template<>
    struct Sendable<std::span<char>>: Sendable<std::span<const char>>
    {
    };

    template<>
    struct Sendable<std::string_view>
    {
        static constexpr const bool value = true;
        static auto send(uart::Channel &c, std::string_view const& s)
        {
            return c.Send((const uint8_t*)s.data(), s.size());
        }
    };

    template<Invokable T>
    struct Sendable<T>
    {
        static constexpr const bool value = true;
        static auto send(uart::Channel &c, T const& a)
        {
            return a();
        }
    };

    class C4001: public uart::Channel
    {
        public:
            using duration_ms_t = uart::duration_ms_t;
            static const constexpr duration_ms_t kRestartTimeout{2000};
            static const constexpr duration_ms_t kDefaultWait{350};

            using Ref = std::reference_wrapper<C4001>;
            struct Err
            {
                ::Err uartErr;
                const char *pLocation;
            };

            using ExpectedResult = std::expected<Ref, Err>;
            struct CmdErr
            {
                Err e;
                uint16_t returnCode;
            };

            template<typename V>
                using RetVal = RetValT<Ref, V>;

            template<class V>
                using ExpectedValue = std::expected<RetVal<V>, Err>;

            struct Version
            {
                char m_Version[32];
            };

        public:

            /**********************************************************************/
            /* PresenceResult                                                     */
            /**********************************************************************/

            C4001(const struct device *pUART);

            ExpectedResult Init();
            ExpectedResult ReloadConfig();

            ExpectedResult Restart();
            ExpectedResult FactoryReset();

            const Version& GetHWVer() const { return m_HWVersion; }
            const Version& GetSWVer() const { return m_SWVersion; }
            auto GetInhibitDuration() const { return m_Inhibit; }
            auto GetRangeFrom() const { return m_MinRange; }
            auto GetRangeTo() const { return m_MaxRange; }
            auto GetTriggerDistance() const { return m_TrigRange; }
            auto GetDetectLatency() const { return m_DetectLatency; }
            auto GetClearLatency() const { return m_ClearLatency; }
            auto GetSensitivityHold() const { return m_SensitivityHold; }
            auto GetSensitivityTrig() const { return m_SensitivityTrigger; }
        private:

            constexpr static const uint8_t kCmdSensorStop[] = "sensorStop";
            constexpr static const uint8_t kCmdSensorStart[] = "sensorStart";
            constexpr static const uint8_t kCmdGetHWVersion[] = "getHWV";
            constexpr static const uint8_t kCmdGetSWVersion[] = "getSWV";
            constexpr static const uint8_t kCmdSaveConfig[] = "saveConfig";
            constexpr static const uint8_t kCmdResetConfig[] = "resetCfg";
            constexpr static const uint8_t kCmdRestart[] = "resetSystem";
            constexpr static const uint8_t kCmdSetRunApp[] = "setRunApp";

            constexpr static const uint8_t kCmdRestartParamNormal[] = "0";
            constexpr static const uint8_t kCmdRestartParamBootloader[] = "1";

            constexpr static const uint8_t kCmdAppModePresence[] = "0";
            constexpr static const uint8_t kCmdAppModeSpeedDistance[] = "1";

            constexpr static const uint8_t kCmdSetInhibit[] = "setInhibit";
            constexpr static const uint8_t kCmdGetInhibit[] = "getInhibit";

            constexpr static const uint8_t kCmdSetRange[] = "setRange";
            constexpr static const uint8_t kCmdGetRange[] = "getRange";

            constexpr static const uint8_t kCmdSetTrigRange[] = "setTrigRange";
            constexpr static const uint8_t kCmdGetTrigRange[] = "getTrigRange";

            constexpr static const uint8_t kCmdSetSensitivity[] = "setSensitivity";
            constexpr static const uint8_t kCmdGetSensitivity[] = "getSensitivity";

            constexpr static const uint8_t kCmdSetLatency[] = "setLatency";
            constexpr static const uint8_t kCmdGetLatency[] = "getLatency";

            template<class Ret>
            struct result
            {
                template<class E>
                static Ret to(E &&e, const char* pLocation)
                {
                    using PureE = std::remove_cvref_t<E>;
                    if constexpr(is_expected_type_v<PureE>)
                    {
                        if constexpr (std::is_same_v<PureE, Ret>)
                            return std::move(e);
                        else
                            return to(e.error(), pLocation);
                    }else if constexpr (std::is_same_v<PureE,::Err>)
                        return Ret(std::unexpected(Err{e, pLocation}));
                    else if constexpr (std::is_same_v<PureE,Err>)
                        return Ret(std::unexpected(e));
                    else if constexpr (std::is_same_v<PureE,CmdErr>)
                        return Ret(std::unexpected(e.e));
                    else
                    {
                        static_assert(std::is_same_v<PureE,Err>, "Don't know how to convert passed type");
                        return Ret(std::unexpected(Err{}));
                    }
                }
            };

#define TRY_UART_COMM(f, location) \
            if (auto r = f; !r) \
                return result<ExpectedResult>::to(std::move(r), location)

            template<class... ToSend> static auto to_send(ToSend&&...args) { return std::forward_as_tuple(std::forward<ToSend>(args)...); }
            template<class... ToRecv> static auto to_recv(ToRecv&&...args) { return std::forward_as_tuple(std::forward<ToRecv>(args)...); }

            template<class ToSend> 
            ExpectedResult SendTpl(ToSend &&arg) 
            {
                TRY_UART_COMM(Sendable<ToSend>::send(*this, arg), "SendTpl");
                return std::ref(*this);
            }

            template<class... ToSend> 
            ExpectedResult SendCmdNoResp(std::string_view cmd, ToSend&&...args) 
            { 
                static_assert((Sendable<ToSend>::value && ... && true), "All arguments must be sendable");
                Channel::ExpectedResult _r(std::ref(*this));
                bool first = true;
                auto send_one = [&]<class SendArg>(SendArg &&a)
                {
                    if (!_r.has_value())
                        return;
                    if (first)
                        first = false;
                    else
                        _r = Sendable<decltype(" ")>::send(*this, " ");
                    _r = std::move(Sendable<std::remove_cvref_t<SendArg>>::send(*this, std::forward<SendArg>(a)));
                };
                send_one(cmd);
                (send_one(std::forward<ToSend>(args)),...);
                TRY_UART_COMM(_r, "SendArgs");
                TRY_UART_COMM(Sendable<decltype("\r\n")>::send(*this, "\r\n"), "<endl>");
                //TRY_UART_COMM(uart::primitives::drain(*this, {.maxWait = 50}), "SendCmdNoResp.drain");
                return std::ref(*this);
            }


            template<class... ToSend> 
            ExpectedResult SendCmd(std::string_view cmd, ToSend&&...args) 
            { 
                static_assert((Sendable<std::remove_cvref_t<ToSend>>::value && ... && true), "All arguments must be sendable");
                Channel::ExpectedResult _r(std::ref(*this));
                bool first = true;
                auto send_one = [&]<class SendArg>(SendArg &&a)
                {
                    if (!_r.has_value())
                        return;
                    if (first)
                        first = false;
                    else
                        _r = Sendable<decltype(" ")>::send(*this, " ");
                    _r = std::move(Sendable<std::remove_cvref_t<SendArg>>::send(*this, std::forward<SendArg>(a)));
                };
                send_one(cmd);
                (send_one(std::forward<ToSend>(args)),...);
                TRY_UART_COMM(_r, "SendArgs");

                TRY_UART_COMM(Sendable<decltype("\r\n")>::send(*this, "\r\n"), "<endl>");

                //wait for an answer
                using namespace uart::primitives;
                if (auto r = find_any_str({}, *this, "Done\r\n", "Error\r\n"); !r)
                    return std::unexpected(Err{r.error()});
                else if (r->v != 0)//not 'Done', but 'Error'
                    return std::unexpected(Err{{"SendCmd Error resp"}});
                return std::ref(*this);
            }

            template<class... ToSend, class... ToRecv> 
            ExpectedResult SendCmdWithParams(std::string_view cmd, std::tuple<ToSend...> tosend, std::tuple<ToRecv...> torecv, bool dbg = false) 
            { 
                static_assert((Sendable<std::remove_cvref_t<ToSend>>::value && ... && true), "All arguments must be sendable");
                Channel::ExpectedResult _r(std::ref(*this));
                bool first = true;
                auto send_one = [&]<class SendArg>(SendArg &&a)
                {
                    if (!_r.has_value())
                        return;
                    if (first)
                        first = false;
                    else
                        _r = Sendable<decltype(" ")>::send(*this, " ");
                    _r = std::move(Sendable<std::remove_cvref_t<SendArg>>::send(*this, std::forward<SendArg>(a)));
                };
                auto send_tuple = [&]<size_t... idx>(std::index_sequence<idx...>)
                {
                    (send_one(std::get<idx>(tosend)),...);
                };
                send_one(cmd);
                send_tuple(std::make_index_sequence<sizeof...(ToSend)>());
                TRY_UART_COMM(_r, "SendArgs");
                TRY_UART_COMM(SendTpl("\r\n"), "<endl>");

                auto recv_tuple = [&]<size_t... idx>(std::index_sequence<idx...>)
                {
                    return uart::primitives::read_any(*this, std::get<idx>(torecv)...);
                };

                if (m_Dbg)
                    printk("Receiving params\r\n");
                TRY_UART_COMM(recv_tuple(std::make_index_sequence<sizeof...(ToRecv)>()), "SendCmdWithParams");

                if (m_Dbg)
                    printk("Received. Receiving Done or Error\r\n");
                //wait for a final 'Done'
                using namespace uart::primitives;
                if (auto r = find_any_str({}, *this, "Done\r\n", "Error\r\n"); !r)
                    return std::unexpected(Err{r.error()});
                else if (r->v != 0)//not 'Done', but 'Error'
                    return std::unexpected(Err{{"SendCmd Error"}});
                if (m_Dbg)
                    printk("Done\r\n");
                return std::ref(*this);
            }

            template<class... ToSend, class... ToRecv> 
            ExpectedResult SendCmdWithParamsStd(std::string_view cmd, std::tuple<ToSend...> tosend, std::tuple<ToRecv...> torecv, bool dbg = false) 
            { 
                using namespace uart::primitives;
                return SendCmdWithParams(
                        cmd,
                        std::move(tosend),
                        std::tuple_cat(to_recv(
                                    find_sv_t{cmd},
                                    find_sv_t{"Response "})
                                , torecv)
                    );
            }

            ExpectedResult ReadFrame();
            
            //data
            //Version m_Version;
            //Configuration m_Configuration;
            Version m_HWVersion;
            Version m_SWVersion;

            class RxBlock: public Channel::RxBlock
            {
            public:
                RxBlock(C4001 &c):Channel::RxBlock(c, c.m_recvBuf, sizeof(c.m_recvBuf)){}
            private:
            };

            uint8_t m_recvBuf[128];

            float m_Inhibit = 2;
            float m_MinRange = 1.6f;
            float m_MaxRange = 25.f;
            float m_TrigRange = 0.f;

            uint8_t m_SensitivityTrigger = 0;
            uint8_t m_SensitivityHold = 0;

            float m_DetectLatency = 0.f;
            float m_ClearLatency = 0.f;
        public:
            class Configurator
            {
            public:
                using Ref = std::reference_wrapper<Configurator>;
                using ExpectedResult = std::expected<Ref, Err>;

                ~Configurator();

                void StartDbg();
                void StopDbg();

                ExpectedResult End();

                ExpectedResult SaveConfig() noexcept;
                ExpectedResult ResetConfig() noexcept;
                ExpectedResult Restart() noexcept;
                ExpectedResult ReloadConfig() noexcept;
                ExpectedResult SwitchToPresenceMode() noexcept;
                ExpectedResult SwitchToSpeedDistanceMode() noexcept;
                ExpectedResult UpdateInhibit() noexcept;
                ExpectedResult SetInhibit(float v) noexcept;

                ExpectedResult UpdateRange() noexcept;
                ExpectedResult SetRange(float from, float to) noexcept;

                ExpectedResult UpdateTrigRange();
                ExpectedResult SetTrigRange(float v);

                ExpectedResult UpdateSensitivity();
                ExpectedResult SetSensitivity(uint8_t trig, uint8_t hold);
                ExpectedResult SetSensitivityTrig(uint8_t val);
                ExpectedResult SetSensitivityHold(uint8_t val);

                ExpectedResult UpdateLatency();
                ExpectedResult SetLatency(float detect, float clear);
            private:
                Configurator(C4001 &c);

                ExpectedResult StopSensor();
                ExpectedResult StartSensor();

                ExpectedResult UpdateHWVersion();
                ExpectedResult UpdateSWVersion();

                C4001 &m_C;
                RxBlock m_RxBlock;
                ExpectedResult m_CtrResult;
                bool m_Finished = false;

                friend class C4001;
            };

            Configurator GetConfigurator();
        private:
    };
}

template<>
struct ::tools::formatter_t<::Err>
{
    template<FormatDestination Dest>
    static std::expected<size_t, FormatError> format_to(Dest &&dst, std::string_view const& fmtStr, ::Err const& e)
    {
        return tools::format_to(std::forward<Dest>(dst), "E<{} at {}>", e.code, e.pLocation);
    }
};

template<>
struct ::tools::formatter_t<dfr::C4001::Err>
{
    template<FormatDestination Dest>
    static std::expected<size_t, FormatError> format_to(Dest &&dst, std::string_view const& fmtStr, dfr::C4001::Err const& e)
    {
        return tools::format_to(std::forward<Dest>(dst), "Err\\{uart=[{}] at {} }", e.uartErr, e.pLocation);
    }
};

#endif
