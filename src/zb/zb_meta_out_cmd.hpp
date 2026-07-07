#ifndef ZB_META_OUT_CMD_HPP_
#define ZB_META_OUT_CMD_HPP_

#include "lib_object_pool.hpp"
#include "zb_meta_annotations.hpp"

namespace zbm
{

    template<class Func> requires std::is_function_v<Func>
    struct cmd_out_t
    {
        //std::meta::info -> parameter types
        static constexpr auto g_Params = []() consteval{
            auto params = std::meta::parameters_of(^^Func);
            return std::define_static_array(params);
        }();

        static_assert(g_Params.size() <= 10, "Too many arguments. Max 10 is supported");

        //order of parameters to store in the pool (alignment sorted for optimal size)
        //mapped as: position 'idx' in stored location at pool => position of the parameter in the function signature
        static constexpr auto g_ParamsIndxSortedForStorage = []() consteval{
            std::array<size_t, g_Params.size()> indices{};
            for(size_t i = 0; i < g_Params.size(); ++i)
                indices[i] = i;
            std::sort(indices.begin(), indices.end(), [](auto p1, auto p2){ return std::meta::alignment_of(g_Params[p1]) > std::meta::alignment_of(g_Params[p2]); });
            return std::define_static_array(indices);
        }();
    };

    namespace detail {
        //workaround for not working constexpr std::format
        // Fixed-capacity compile-time string generator
        template<unsigned ID>
        struct arg_name_provider 
        {
            static constexpr unsigned prefix_len = 3; // length of "arg"
            static constexpr unsigned total_len = prefix_len + 1;//decimal 0-9

            char chars[total_len + 1]{};

            constexpr arg_name_provider() {
                const char* prefix = "arg";
                for (unsigned i = 0; i < prefix_len; ++i) {
                    chars[i] = prefix[i];
                }
                chars[prefix_len] = '0' + ID;
                chars[total_len] = '\0';
            }
        };
    }

    template<std::meta::info cmd_out_mem_refl>
    struct cmd_out_pool_t
    {
        using cmd_type_t = typename [:std::meta::type_of(cmd_out_mem_refl):];
        static constexpr auto cmd_a = *get_sending_command_annotation(cmd_out_mem_refl);
        static constexpr auto cluster_a = *get_parent_cluster_annotation(cmd_out_mem_refl);
        static_assert(cluster_a.role == role_t::Client || cluster_a.role == role_t::Server);

        struct arg_storage_t;
        consteval{
            std::vector<std::meta::info> mems;
            template for(constexpr auto argIdx : cmd_type_t::g_ParamsIndxSortedForStorage)
            {
                auto arg_ref = cmd_type_t::g_Params[argIdx];
                constexpr auto name = detail::arg_name_provider<argIdx>();
                mems.push_back(std::meta::data_member_spec(arg_ref, std::meta::data_member_options{.name = name.chars}));
            }
            std::meta::define_aggregate(^^arg_storage_t, mems);
        };

        struct runtime_args_t
        {
            zb_callback_t cb;
            zb_addr_u dst_addr;
            uint8_t dst_ep;
            addr_mode_t addr_mode;
            bool canceled;
            arg_storage_t args;
        };

        using PoolType = ObjectPool<runtime_args_t, cmd_a.pool_size>;
        using pool_idx_type_t = typename PoolType::size_type;
        using args_ret_t = std::optional<pool_idx_type_t>;

        inline constinit static PoolType g_Pool{};
        using RequestPtr = PoolType::template Ptr<g_Pool>;

        template<class... TArgs>
        struct store_arguments_t
        {
            static_assert(cmd_type_t::g_Params.size() == sizeof...(TArgs), "Passed amount of arguments must match the signature of the command!");

            static arg_storage_t store(TArgs&&... args)
            {
                return [&]<size_t... I>(std::index_sequence<I...>){
                    //here happens the magic or re-odering arguments for storage purposes
                    return arg_storage_t{std::forward<TArgs...[cmd_type_t::g_ParamsIndxSortedForStorage[I]]>(args...[cmd_type_t::g_ParamsIndxSortedForStorage[I]])...};
                }(std::make_index_sequence<sizeof...(TArgs)>());
            }
        };


        /**********************************************************************/
        /* prepare_args for various destination call modes                    */
        /**********************************************************************/

        template<class... TArgs>
        static args_ret_t prepare_args(zb_callback_t cb, TArgs&&... args) { 
            auto r = g_Pool.PtrToIdx(g_Pool.Acquire(
                        cb, 
                        uint16_t(0), 
                        uint8_t(0), 
                        addr_mode_t::NoAddr_NoEP, 
                        false, 
                        store_arguments_t<TArgs...>::store(std::forward<TArgs>(args)...)
                    )); 
            return r == PoolType::kInvalid ? std::nullopt : args_ret_t(r);
        }

        template<class... TArgs>
        static args_ret_t prepare_args(zb_callback_t cb, uint16_t short_addr, uint8_t ep, TArgs&&... args) { 
            auto r = g_Pool.PtrToIdx(g_Pool.Acquire(
                        cb, 
                        short_addr, 
                        ep, 
                        addr_mode_t::Dst16EP, 
                        false, 
                        store_arguments_t<TArgs...>::store(std::forward<TArgs>(args)...)
                    )); 
            return r == PoolType::kInvalid ? std::nullopt : args_ret_t(r);
        }

        template<class... TArgs>
        static args_ret_t prepare_args(zb_callback_t cb, zb_ieee_addr_t ieee_addr, uint8_t ep, TArgs&&... args) { 
            auto r = g_Pool.PtrToIdx(g_Pool.Acquire(
                        cb, 
                        zb_addr_u{.addr_long{ieee_addr}}, 
                        ep, 
                        addr_mode_t::Dst64EP, 
                        false, 
                        store_arguments_t<TArgs...>::store(std::forward<TArgs>(args)...)
                    )); 
            return r == PoolType::kInvalid ? std::nullopt : args_ret_t(r);
        }

        template<class... TArgs>
        static args_ret_t prepare_args(zb_callback_t cb, uint8_t bind_table_id, TArgs&&... args) { 
            auto r = g_Pool.PtrToIdx(g_Pool.Acquire(
                        cb, 
                        uint16_t(0), 
                        bind_table_id, 
                        addr_mode_t::EPAsBindTableId, 
                        false, 
                        store_arguments_t<TArgs...>::store(std::forward<TArgs>(args)...)
                    )); 
            return r == PoolType::kInvalid ? std::nullopt : args_ret_t(r);
        }

        template<class... TArgs>
        static args_ret_t prepare_args(zb_callback_t cb, uint16_t group_id, TArgs&&... args) { 
            auto r = g_Pool.PtrToIdx(g_Pool.Acquire(
                        cb, 
                        group_id, 
                        uint8_t(0), 
                        addr_mode_t::Group_NoEP, 
                        false, 
                        store_arguments_t<TArgs...>::store(std::forward<TArgs>(args)...)
                    )); 
            return r == PoolType::kInvalid ? std::nullopt : args_ret_t(r);
        }

        /**********************************************************************/
        /* Serialization                                                      */
        /**********************************************************************/
        static uint8_t* serialize_to(arg_storage_t const& src, uint8_t *dest)
        {
            template for(constexpr size_t i : std::ranges::views::iota(size_t(0), cmd_type_t::g_Params.size()))
            {
                constexpr auto name = detail::arg_name_provider<i>();
                auto const& m = src.[:find_member_by_name(^^arg_storage_t, name.chars):];
                if constexpr (serializable_c<decltype(m)>)
                {
                    dest = *m.serialize_to(dest, size_t(-1)/*real limits are unknown atm*/);
                }
                else
                {
                    std::memcpy(dest, &m, sizeof(m));
                    dest += sizeof(m);
                }
            }
            return dest;
        }

        static uint8_t* serialize_to(pool_idx_type_t idx, uint8_t *dest)
        {
            return serialize_to(g_Pool.IdxToPtr(idx)->args, dest);
        }

        /**********************************************************************/
        /* Request for buffer to send cmd                                     */
        /**********************************************************************/
        template<ep_a epa>
        static bool request(uint16_t argsPoolIdx)
        {
            static_assert(epa.profile_id && cmd_a.pool_size >= 1, "This command cannot be sent");
            return zigbee_get_out_buf_delayed_ext( &on_out_buf_ready<epa>, argsPoolIdx, 0) == RET_OK;
        }

        static void cancel(uint16_t argsPoolIdx)
        {
            auto *pArgs = g_Pool.IdxToPtr(argsPoolIdx);
            if (g_Pool.IsValid(pArgs))
                pArgs->canceled = true;
        }

        template<ep_a epa>
        static void on_out_buf_ready(zb_bufid_t bufid, uint16_t poolIdx)
        {
            auto *pArgs = g_Pool.IdxToPtr(poolIdx);
            if (!g_Pool.IsValid(pArgs))
            {
                //weird
                return;
            }

            RequestPtr raii(pArgs);
            if (pArgs->canceled)
            {
                if (bufid != ZB_BUF_INVALID)
                    zb_buf_free(bufid);
                return;
            }

            if (bufid == ZB_BUF_INVALID)
            {
                //out of mem?
                if (pArgs->cb) pArgs->cb(0);
                return;
            }

            constexpr uint16_t manu_code = cmd_a.manuf_code != ZB_ZCL_MANUF_CODE_INVALID ? cmd_a.manuf_code : cluster_a.manuf_code;

            frame_ctl_t f{.f{
                .cluster_specific = true, 
                .manufacture_specific = manu_code != ZB_ZCL_MANUF_CODE_INVALID
                , .direction = cluster_a.role == role_t::Client ? frame_direction_t::ToServer : frame_direction_t::ToClient
                , .disable_default_response = false
            }};
            ZB_ZCL_GET_SEQ_NUM();
            uint8_t* ptr = (uint8_t*)zb_zcl_start_command_header(bufid, f.u8, manu_code, cmd_a.id, nullptr);
            ptr = serialize_to(pArgs->args, ptr);
            zb_ret_t ret = zb_zcl_finish_and_send_packet(bufid, ptr, &pArgs->dst_addr, (uint8_t)pArgs->addr_mode, pArgs->dst_ep, epa.ep, epa.profile_id, cluster_a.id, pArgs->cb);
            if (RET_OK != ret && pArgs->cb)
                pArgs->cb(0);
            if (RET_OK != ret)
                zb_buf_free(bufid);
        }
    };
}

#endif
