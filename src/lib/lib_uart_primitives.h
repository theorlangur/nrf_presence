#ifndef UART_PRIMITIVES_HPP_
#define UART_PRIMITIVES_HPP_
#include "lib_uart.h"
#include <functional>
#include <span>

namespace uart
{
    namespace primitives
    {
        struct cfg_t
        {
            duration_ms_t maxWait = kDefault;
            const char *pCtx = "";
        };
        inline auto flush_and_wait(Channel &c, cfg_t cfg = {})
        {
            using ExpectedResult = Channel::ExpectedResult;
            if (auto r = c.Drain(false); !r)
                return ExpectedResult(std::unexpected(r.error()));

            if (auto r = c.PeekByte(cfg.maxWait); !r)
                return ExpectedResult(std::unexpected(r.error()));
            return ExpectedResult(std::ref(c));
        }

        inline auto drain(Channel &c, cfg_t cfg = {})
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            uint8_t buf[16];
            while(true)
            {
                if (auto r = c.Read(buf, sizeof(buf)); !r)
                    break;
            }

            return ExpectedResult(std::ref(c));
        }

        inline auto skip_bytes(Channel &c, size_t bytes, cfg_t cfg = {})
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            uint8_t buf[16];
            while(bytes)
            {
                if (auto r = c.Read(buf, std::min(sizeof(buf), bytes)); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else
                    bytes -= r.value().v;
            }

            return ExpectedResult(std::ref(c));
        }

        inline auto match_bytes(Channel &c, std::span<const uint8_t> bytes, cfg_t cfg = {})
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            for(size_t idx = 0, n = bytes.size(); idx < n; ++idx)
            {
                if (auto r = c.ReadByte(); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else if (bytes[idx] != r.value().v)
                    return ExpectedResult(std::unexpected(::Err{"match_bytes", ERR_OK}));
            }
            return ExpectedResult(std::ref(c));
        }

        template<class... Seqs>
        inline auto match_any_bytes(Channel &c, Seqs&&... bytes)
        {
            using MatchAnyResult = Channel::RetVal<int>;
            using ExpectedResult = std::expected<MatchAnyResult, Err>;

            std::span<uint8_t> sequences[sizeof...(Seqs)] = {bytes...};
            bool anyValidLeft = true;
            size_t idx = 0;
            while(anyValidLeft)
            {
                if (auto r = c.ReadByte(); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else
                {
                    uint8_t b = r.value().v;
                    anyValidLeft = false;
                    int match = -1;
                    for(auto &s : sequences)
                    {
                        ++match;
                        if (s.empty())
                            continue;

                        if ((s.size() <= idx) && s[idx] != b)
                            s = {};
                        else if (s.size() == (idx + 1)) //match
                            return ExpectedResult(MatchAnyResult{std::ref(c), match});
                        else
                            anyValidLeft = true;
                    }
                    ++idx;
                }
            }
            return ExpectedResult(std::unexpected(::Err{"match_any_bytes", ERR_OK}));
        }

        inline auto match_bytes(Channel &c, const uint8_t *pBytes, uint8_t terminator, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            while(*pBytes != terminator)
            {
                if (auto r = c.ReadByte(); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else if (r.value().v != *pBytes)
                    return ExpectedResult(std::unexpected(::Err{"match_bytes", ERR_OK}));
                ++pBytes;
            }
            return ExpectedResult(std::ref(c));
        }

        [[gnu::always_inline]]inline auto match_bytes(Channel &c, const char *pStr, const char *pCtx = "")
        {
            return uart::primitives::match_bytes(c, (const uint8_t*)pStr, 0, pCtx);
        }

        template<size_t N>
        [[gnu::always_inline]]inline auto match_bytes(Channel &c, const char (&arr)[N], const char *pCtx = "")
        {
            return uart::primitives::match_bytes(c, (const uint8_t*)arr, 0, pCtx);
        }

        template<size_t N>
        [[gnu::always_inline]]inline auto match_bytes(Channel &c, const uint8_t (&arr)[N], const char *pCtx = "")
        {
            return uart::primitives::match_bytes(c, std::span<const uint8_t>(arr, N), {.pCtx = pCtx});
        }

        template<class C1, class C2>
        concept same_as_no_ref = std::same_as<C1, C2> || std::same_as<C1, C2&> || std::same_as<C1, C2&>;

        inline auto match_any_bytes_term(Channel &c, uint8_t term, same_as_no_ref<std::span<const uint8_t>> auto&&... bytes)
        {
            using MatchAnyResult = Channel::RetVal<int>;
            using ExpectedResult = std::expected<MatchAnyResult, Err>;
            const uint8_t* sequences[sizeof...(bytes)]={bytes.data()...};
            bool anyValidLeft = true;
            size_t idx = 0;

            while(anyValidLeft)
            {
                if (auto r = c.ReadByte(); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else
                {
                    uint8_t b = r.value().v;
                    anyValidLeft = false;
                    int match = -1;
                    for(auto &s : sequences)
                    {
                        ++match;
                        if (!s)
                            continue;

                        if (s[idx] == b)
                        {
                            if (s[idx + 1] == term)
                                return ExpectedResult(MatchAnyResult{std::ref(c), match});
                            anyValidLeft = true;
                        }else
                            s = nullptr;
                    }

                    ++idx;
                }
            }
            return ExpectedResult(std::unexpected(::Err{"match_any_bytes_term", ERR_OK}));
        }

        template<class C>
        concept convertible_to_const_char_ptr = requires(C &c) { {c}->std::convertible_to<const char*>; };

        [[gnu::always_inline]]inline auto match_any_str(Channel &c, convertible_to_const_char_ptr auto&&... bytes)
        {
            return uart::primitives::match_any_bytes_term(c, 0, std::span<const uint8_t>{bytes.data(), bytes.size()}...);
        }

        inline auto read_until(Channel &c, uint8_t until, duration_ms_t maxWait = kDefault, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto start = k_uptime_get();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            while(check_timeout())
            {
                if (auto r = c.PeekByte(maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else if (r.value().v == until)
                    return ExpectedResult(std::ref(c));

                if (auto r = c.ReadByte(maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
            }
            return ExpectedResult(std::unexpected(::Err{"read_until timeout", ERR_OK}));
        }

        template<class... Byte>
        inline auto read_any_until(cfg_t cfg, Channel &c, Byte... until)
        {
            using ReadAnyResult = Channel::RetVal<int>;
            using ExpectedResult = std::expected<ReadAnyResult, Err>;
            auto start = k_uptime_get();
            auto maxWait = cfg.maxWait;
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            while(check_timeout())
            {
                if (auto r = c.PeekByte(cfg.maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else 
                {
                    int d = -1;
                    auto check_single = [&](int idx, uint8_t u)
                    {
                        if (d != -1) return;
                        if (r.value().v == u) d = idx;
                    };
                    [&]<int... idx>(std::integer_sequence<int, idx...>)
                    {
                        (check_single(idx, until),...);
                    }(std::make_integer_sequence<int, sizeof...(Byte)>());
                    if (d != -1)
                        return ExpectedResult(ReadAnyResult{std::ref(c), d});
                }

                if (auto r = c.ReadByte(cfg.maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
            }
            return ExpectedResult(std::unexpected(::Err{"read_until timeout", ERR_OK}));
        }

        inline auto find_bytes(Channel &c, std::span<const uint8_t> arr, duration_ms_t maxWait = kDefault, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            auto start = k_uptime_get();
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            while(check_timeout())
            {
                if (auto r = read_until(c, arr[0], maxWait, pCtx); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                if (!check_timeout())
                    return ExpectedResult(std::unexpected(::Err{"find_bytes timeout", ERR_OK}));
                if (auto r = match_bytes(c, arr, {.maxWait = maxWait, .pCtx = pCtx}); r)
                    return ExpectedResult(std::ref(c));
            }
            return ExpectedResult(std::unexpected(::Err{"find_bytes timeout", ERR_OK}));
        }

        inline auto find_bytes(Channel &c, const uint8_t *arr, uint8_t term, duration_ms_t maxWait = kDefault, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto start = k_uptime_get();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            int it = 0;
            while(check_timeout())
            {
                if (auto r = read_until(c, arr[0], maxWait, pCtx); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                if (!check_timeout())
                {
                    printk("find_bytes timeout; it=%d\r\n", it);
                    return ExpectedResult(std::unexpected(::Err{"find_bytes timeout", ERR_OK}));
                }
                if (auto r = match_bytes(c, arr, term, pCtx); r)
                    return ExpectedResult(std::ref(c));
                ++it;
            }
            printk("find_bytes final timeout; it=%d\r\n", it);
            return ExpectedResult(std::unexpected(::Err{"find_bytes timeout", ERR_OK}));
        }

        [[gnu::always_inline]]inline auto find_bytes(Channel &c, const char *str, duration_ms_t maxWait = kDefault, const char *pCtx = "")
        {
            return find_bytes(c, (const uint8_t*)str, 0, maxWait, pCtx);
        }

        inline auto find_any_bytes(cfg_t cfg, Channel &c, uint8_t term, std::same_as<std::span<const uint8_t>> auto&&... arr)
        {
            using FindAnyResult = Channel::RetVal<int>;
            using ExpectedResult = std::expected<FindAnyResult, Err>;
            auto start = k_uptime_get();
            auto maxWait = cfg.maxWait;
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            while(check_timeout())
            {
                if (auto r = read_any_until(cfg, c, arr[0]...); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                if (!check_timeout())
                    return ExpectedResult(std::unexpected(::Err{"find_any_bytes timeout", ERR_OK}));
                if (auto r = match_any_bytes_term(c, term, arr...); r)
                    return ExpectedResult(FindAnyResult{std::ref(c), (*r).v});
            }
            return ExpectedResult(std::unexpected(::Err{"find_any_bytes timeout", ERR_OK}));
        }

        [[gnu::always_inline]]inline auto find_any_str_impl(cfg_t cfg, Channel &c, std::same_as<std::string_view> auto&&... str)
        {
            return find_any_bytes(cfg, c, 0, std::span<const uint8_t>{(const uint8_t*)str.data(), str.size()}...);
        }

        template<class... BytePtr> requires (convertible_to_const_char_ptr<BytePtr> &&...)
        [[gnu::always_inline]]inline auto find_any_str(cfg_t cfg, Channel &c, BytePtr&&... str)
        {
            return find_any_str_impl(cfg, c, std::string_view(str)...);
        }

        inline auto read_until_into(Channel &c, uint8_t until, uint8_t *pDst, size_t dstSize, bool consume_last, cfg_t cfg)
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            auto start = k_uptime_get();
            auto maxWait = cfg.maxWait;
            if (maxWait == kDefault) maxWait = c.GetDefaultWait();
            auto check_timeout = [&]->bool{
                return (maxWait == kForever) || (k_uptime_get() - start) < maxWait;
            };

            while(check_timeout())
            {
                if (auto r = c.PeekByte(cfg.maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else if (r.value().v == until)
                {
                    if (consume_last)
                    {
                        if (auto r = c.ReadByte(cfg.maxWait); !r)
                            return ExpectedResult(std::unexpected(r.error()));
                    }
                    return ExpectedResult(std::ref(c));
                }

                if (!check_timeout())
                    return ExpectedResult(std::unexpected(::Err{"read_until_into timeout", ERR_OK}));

                if (!dstSize)
                    return ExpectedResult(std::unexpected(::Err{"read_until_into dst too small", ERR_OK}));

                if (auto r = c.ReadByte(cfg.maxWait); !r)
                    return ExpectedResult(std::unexpected(r.error()));
                else
                {
                    *pDst++ = (*r).v;
                    --dstSize;
                }
            }
            return ExpectedResult(std::unexpected(::Err{"read_until_into timeout", ERR_OK}));
        }

        template<class T>
        inline auto read_into(Channel &c, T &dst, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            if (auto r = c.Read((uint8_t*)&dst, sizeof(T)); !r)
                return ExpectedResult(std::unexpected(r.error()));
            else if (r.value().v != sizeof(T))
                return ExpectedResult(std::unexpected(::Err{"read_into wrong len", ERR_OK}));

            return ExpectedResult(std::ref(c));
        }

        inline auto read_into_bytes(Channel &c, uint8_t *pDst, int l, const char *pCtx = "")
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            if (auto r = c.Read(pDst, l); !r)
                return ExpectedResult(std::unexpected(r.error()));
            else if (r.value().v != l)
                return ExpectedResult(std::unexpected(::Err{"read_into_bytes wrong len", ERR_OK}));

            return ExpectedResult(std::ref(c));
        }

        template<class... Args>
        inline auto write_any(Channel &c, Args&&... args)
        {
            using ExpectedResult = std::expected<Channel::Ref, ::Err>;
            ExpectedResult r{std::ref(c)};
            (void)((bool)(r = c.Send((uint8_t const*)&args, sizeof(args))) &&...);
            return r;
        }

        template<class T>
        struct match_t
        {
            using functional_read_helper = void;
            const T v;
            const char *pCtx = "";

            static constexpr size_t size() { return sizeof(std::remove_cvref_t<T>); }
            auto run(Channel &c) { return uart::primitives::match_bytes(c, std::span<const uint8_t>((uint8_t const*)&v, sizeof(T)), {.pCtx = pCtx}); }
        };

        struct find_str_t
        {
            using functional_read_helper = void;
            const char *pStr;
            const char *pCtx = "";

            static constexpr size_t size() { return 0; }
            auto run(Channel &c) { return uart::primitives::find_bytes(c, pStr); }
        };

        struct find_sv_t
        {
            using functional_read_helper = void;
            std::string_view sv;
            const char *pCtx = "";

            static constexpr size_t size() { return 0; }
            auto run(Channel &c) { return uart::primitives::find_bytes(c, std::span<const uint8_t>{(const uint8_t*)sv.begin(), sv.size()}, kDefault, pCtx); }
        };

        template<size_t N>
        struct skip_t
        {
            using functional_read_helper = void;
            static constexpr size_t size() { return N; }
            auto run(Channel &c) { return uart::primitives::skip_bytes(c, N); }
        };

        template<class CB>
        struct callback_t
        {
            using functional_read_helper = void;
            static constexpr size_t size() { return 0; }

            CB v;
            auto run(Channel &c) { return v(); }
        };

        template<class T, class D>
        struct read_var_t
        {
            using functional_read_helper = void;
            const T &len;
            D *v;

            static constexpr size_t size() { return 0; }
            size_t rt_size() const { return len; }
            auto run(Channel &c) { 
                if (auto r = c.Read((uint8_t*)v, len); !r)
                    return Channel::ExpectedResult(std::unexpected(r.error()));
                return Channel::ExpectedResult(std::ref(c));
            } 
        };

        template<class T, size_t N>
        struct read_until_t
        {
            using functional_read_helper = void;
            T (&dst)[N];
            T until;
            bool consume_last = true;

            static constexpr size_t size() { return sizeof(T) * N; }
            size_t rt_size() const { return sizeof(T) * N; }
            auto run(Channel &c) { return read_until_into(c, until, (uint8_t*)dst, N, consume_last, {}); } 
        };

        template<class C>
        concept is_functional_read_helper = requires{ typename C::functional_read_helper; };

        template<class T>
        constexpr size_t uart_sizeof()
        {
            if constexpr (is_functional_read_helper<T>)
                return T::size();
            else
                return sizeof(T);
        }

        template<class T>
        constexpr size_t uart_rtsize(T &a)
        {
            if constexpr (requires{ a.rt_size(); })
                return a.rt_size();
            else
            {
                return uart::primitives::uart_sizeof<T>();
            }
        }

        template<class T>
        inline auto recv_for(Channel &c, T &&a)
        {
            using PureT = std::remove_cvref_t<T>;
            if constexpr (is_functional_read_helper<PureT>)
                return a.run(c);
            else
                return uart::primitives::read_into(c, a);
        }

        template<class Sz, class T>
        inline auto recv_for_checked(Channel &c, Sz &limit, T &&a)
        {
            auto sz = uart::primitives::uart_rtsize(a);
            if (limit < sz)
                return Channel::ExpectedResult(std::unexpected(::Err{"recv_for_checked: Insufficient length", ERR_OK})); 
            limit -= sz;

            using PureT = std::remove_cvref_t<T>;
            if constexpr (is_functional_read_helper<PureT>)
                return  a.run(c);
            else
                return uart::primitives::read_into(c, a);
        }

        template<class... Args>
        inline auto read_any(Channel &c, Args&&... args)
        {
            Channel::ExpectedResult r(std::ref(c));
            (void)((bool)(r = uart::primitives::recv_for(c, args)) && ...);
            return r;
        }

        template<class Sz, class... Args>
        inline auto read_any_limited(Channel &c, Sz &limit, Args&&... args)
        {
            Channel::ExpectedResult r(std::ref(c));
            (void)((bool)(r = uart::primitives::recv_for_checked(c, limit, args)) && ...);
            return r;
        }
    }
}
#endif
