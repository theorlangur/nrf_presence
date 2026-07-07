#ifndef ZB_META_EP_HPP_
#define ZB_META_EP_HPP_

#include "lib_ring_buffer.hpp"

#include "zb_meta_types.hpp"
#include "zb_meta_annotations.hpp"
#include "zb_meta_cluster.hpp"
#include "zb_meta_out_cmd.hpp"


namespace zbm
{
    template<size_t ServerCount, size_t ClientCount>
    struct ZB_PACKED_PRE simple_desc_t: zb_af_simple_desc_1_1_t
    {
        zb_uint16_t app_cluster_list_ext[(ServerCount + ClientCount) - 2];
    } ZB_PACKED_STRUCT;

    template<size_t ServerCount, size_t ClientCount> requires ((ServerCount + ClientCount) < 2)
    struct ZB_PACKED_PRE simple_desc_t<ServerCount, ClientCount>: zb_af_simple_desc_1_1_t
    {
    } ZB_PACKED_STRUCT;


    struct short_addr_t
    {
        using addr_tag = void;
        uint16_t short_addr;
        uint8_t ep;
    };
    struct long_addr_t
    {
        using addr_tag = void;
        long_addr_t(zb_ieee_addr_t a, uint8_t e):
            ep(e)
        {
            memcpy(long_addr, a, sizeof(zb_ieee_addr_t));
        }
        zb_ieee_addr_t long_addr;
        uint8_t ep;
    };
    struct group_addr_t
    {
        using addr_tag = void;
        uint16_t group;
    };
    struct bind_id_addr_t
    {
        using addr_tag = void;
        uint8_t bind_table_id;
    };

    inline short_addr_t to_short(uint16_t _short, uint8_t ep) { return {_short, ep}; }
    inline long_addr_t to_long(zb_ieee_addr_t addr, uint8_t ep) { return {addr, ep}; }
    inline group_addr_t to_group(uint16_t group) { return {group}; }
    inline bind_id_addr_t to_bind_id(uint8_t id) { return {id}; }

    template<class C>
    concept is_zb_addr_type_c = requires{ typename C::addr_tag; };

    template<std::meta::info local_clusters_r>
    struct meta_ctr_param_t{};

    template<ep_base_cfg_t cfg, uint8_t ep_id>
    struct ep_base_t
    {
        using SimpleDesc = simple_desc_t<cfg.server_clusters, cfg.client_clusters>;

        consteval ep_base_t(ep_base_t const&) = delete;
        consteval ep_base_t(ep_base_t &&) = delete;
        template<std::meta::info local_clusters_r>
        consteval ep_base_t(ep_a epa, auto clusters, meta_ctr_param_t<local_clusters_r> dummy):
            simple_desc{ 
                /*base struct*/{
                    .endpoint = epa.ep,
                        .app_profile_id = ZB_AF_HA_PROFILE_ID, 
                        .app_device_id = epa.dev_id,
                        .app_device_version = epa.dev_ver,
                        .reserved = 0,
                        .app_input_cluster_count = (uint8_t)cfg.server_clusters,
                        .app_output_cluster_count = (uint8_t)cfg.client_clusters,
                        .app_cluster_list = {}
                } 
            },
            rep_ctx{},
            cvc_alarm_ctx{},
            clusters_descriptions{},
            ep{
                .ep_id = epa.ep,
                .profile_id = ZB_AF_HA_PROFILE_ID,
                .device_handler = nullptr,
                .identify_handler = nullptr,
                .reserved_size = 0,
                .reserved_ptr = nullptr,
                .cluster_count = uint8_t(cfg.server_clusters + cfg.client_clusters),
                .cluster_desc_list = clusters_descriptions,
                .simple_desc = &simple_desc,
                .rep_info_count = (uint8_t)cfg.reporting_attributes,
                .reporting_info = rep_ctx,
                .cvc_alarm_count = (uint8_t)cfg.cvc_attributes,
                .cvc_alarm_info = cvc_alarm_ctx
            }
        {

            for(size_t i = 0, n = clusters.size(); i < n; ++i)
            {
                auto const& ca = clusters[i];
                if (i < 2)
                    simple_desc.app_cluster_list[i] = ca.annotation.id;
                else
                    simple_desc.app_cluster_list_ext[i - 2] = ca.annotation.id;
            }

            //static constexpr auto local_cluster_r = std::meta::remove_cvref(^^decltype(local_clusters));
            static constexpr auto local_cluster_r = std::meta::remove_cvref(std::meta::type_of(local_clusters_r));
            static constexpr auto local_cluster_mems = std::define_static_array(std::meta::nonstatic_data_members_of(local_cluster_r, std::meta::access_context::current()));
            int i = 0;
            template for(constexpr auto m : local_cluster_mems)
            {
                auto const& ca = clusters[i];
                static constexpr auto cluster_refl = std::meta::reflect_object([:local_clusters_r:].[:m:]);//cluster_t &
                clusters_descriptions[i] = zb_zcl_cluster_desc_t{
                    .cluster_id = ca.annotation.id,
                    .attr_count = std::size([:local_clusters_r:].[:m:].attributes),
                    .attr_desc_list = [:local_clusters_r:].[:m:].attributes,
                    .role_mask = (zb_uint8_t)ca.annotation.role,
                    .manuf_code = ca.annotation.manuf_code,
                    .cluster_init = &generic_cluster_init<cluster_refl, ep_id>
                };
                ++i;
            }
        }

        alignas(4) SimpleDesc simple_desc;
        alignas(4) zb_zcl_reporting_info_t rep_ctx[cfg.reporting_attributes];
        alignas(4) zb_zcl_cvc_alarm_variables_t cvc_alarm_ctx[cfg.cvc_attributes];
        alignas(4) zb_zcl_cluster_desc_t clusters_descriptions[cfg.server_clusters + cfg.client_clusters];
        alignas(4) zb_af_endpoint_desc_t ep;
    };

    consteval ep_a get_ep_annotations(std::meta::info ep_ref)
    {
        auto ep_annotations = std::meta::annotations_of_with_type(ep_ref, ^^zbm::ep_a);
        return std::meta::extract<zbm::ep_a>(ep_annotations[0]);
    } 

    template<std::meta::info ep_mem_decl, std::meta::info epm>
    struct ep_factory_t
    {
        /*
         * struct ep_t
         * {
         *   ep_base_t<cfg, ep_a> ep_data;
         * };
         * */
        struct ep_t;//it's important this is a first declaration
        static constexpr ep_a g_Annotation = get_ep_annotations(ep_mem_decl);//source of constexpr template-capable ep_id
        consteval
        {
            std::meta::info ep_type = std::meta::remove_cvref(std::meta::type_of(epm));
            auto mems = std::meta::nonstatic_data_members_of(ep_type, std::meta::access_context::current());
            std::vector<std::meta::info> cluster_types;
            for(auto m : mems)
                cluster_types.push_back(std::meta::type_of(m));
            if (!cluster_types.empty())
            {
                ep_base_cfg_t cfg = analyze_clusters(cluster_types);
                auto ep_data_type = std::meta::substitute(^^ep_base_t, {std::meta::reflect_constant(cfg), std::meta::reflect_constant(g_Annotation.ep)});
                std::meta::define_aggregate(^^ep_t, {
                        std::meta::data_member_spec(ep_data_type, std::meta::data_member_options{"ep_data"})
                        });
            }
        };
    };

    consteval std::meta::info get_ep_type_from_factory(std::meta::info ep_mem_decl, std::meta::info ep_ref)
    {
        auto ep_fact_inst = std::meta::substitute(^^ep_factory_t, {std::meta::reflect_constant(ep_mem_decl), std::meta::reflect_constant(ep_ref)});
        return std::define_static_array(std::meta::members_of(ep_fact_inst, std::meta::access_context::current()))[0];//struct ep_t;
    } 


    template<std::meta::info ep_mem_decl, std::meta::info ep_ref> requires (!std::meta::annotations_of_with_type(ep_mem_decl, ^^zbm::ep_a).empty() && !extract_clusters_from_ep(ep_ref).empty())
    struct ep_create_t
    {
        using ep_type_t = [:get_ep_type_from_factory(ep_mem_decl, ep_ref):];
        static constexpr auto epa = get_ep_annotations(ep_mem_decl);
        static constexpr auto cluster_list = define_static_array(extract_clusters_from_ep(ep_ref));
        static constexpr auto cluster_refs = []() consteval{
            std::vector<std::meta::info> refs;
            template for(constexpr auto ci : cluster_list)
                refs.push_back(std::meta::reflect_object([:ep_ref:].[:ci.cluster:]));
            return define_static_array(refs);
        }();

        struct ep_front_t: ep_type_t
        {

            /**********************************************************************/
            /* Various types and declarations and statics                         */
            /**********************************************************************/
            static constexpr size_t kCmdQueueSize = epa.cmd_queue_depth;
            using cmd_id_t = int;
            using cmd_send_status_cb_t = void(*)(cmd_id_t, zb_zcl_command_send_status_t *);
            using send_request_func_t = bool (*)(uint16_t argsPoolIdx);
            using cancel_func_t = void (*)(uint16_t argsPoolIdx);
            struct cmd_request_t
            {
                cmd_id_t id;
                uint8_t args_idx;
                send_request_func_t send_req;
                cancel_func_t cancel_req;
                cmd_send_status_cb_t cb;
                uint32_t timeout_ms;
            };
            inline static RingBuffer<cmd_request_t, kCmdQueueSize> g_cmd_queue;
            inline static zb::zb_alarm_ext_16_t g_cmd_timeout_tracker;
            inline static cmd_id_t g_cmd_num = 0;

            /**********************************************************************/
            /* Attribute Set                                                      */
            /**********************************************************************/
            template<std::meta::info attribute_refl, bool checked, class A>
            zb_zcl_status_t set_raw(A &&arg)
            {
                constexpr auto user_cluster_ref = std::meta::parent_of(attribute_refl);
                constexpr auto attribute_a = get_attribute_annotation(attribute_refl);
                using attribute_type_t = typename [:std::meta::type_of(attribute_refl):];

                static_assert(attribute_a.type != type_t::Invalid, "Undefined or incorrectly defined attribute!");
                static_assert(attribute_a.a & access_t::Write, "Attribute is not writable!");
                static_assert(std::is_convertible_v<A, attribute_type_t>, "Cannot convert to attribute type");

                static constexpr auto local_cluster_r = ^^typename cluster_list_factory_t<ep_ref>::cluster_list_t;
                static constexpr auto local_cluster_mems = std::define_static_array(std::meta::nonstatic_data_members_of(local_cluster_r, std::meta::access_context::current()));
                static constexpr std::optional<std::meta::info> zbm_cluster_refl_opt = []() consteval ->std::optional<std::meta::info>{
                    template for(constexpr auto m : local_cluster_mems)
                    {
                        constexpr auto zbm_cluster_refl = std::meta::type_of(m);
                        using zbm_cluster_t = typename [:zbm_cluster_refl:];
                        if constexpr (std::meta::type_of(zbm_cluster_t::cluster_data_ref_refl) == user_cluster_ref)
                            return zbm_cluster_refl;
                    }
                    return std::nullopt;
                }();
                static_assert(zbm_cluster_refl_opt, "Attribute not found!");

                static constexpr auto zbm_cluster_refl = *zbm_cluster_refl_opt;
                using zbm_cluster_t = typename [:zbm_cluster_refl:];
                return zb_zcl_set_attr_val(epa.ep, zbm_cluster_t::g_ClusterA.id, (zb_uint8_t)zbm_cluster_t::g_ClusterA.role, attribute_a.id, (zb_uint8_t*)&arg, checked);
            }

            template<std::meta::info attribute_refl, class A>
            zb_zcl_status_t set(A &&arg) { return set_raw<attribute_refl, false, A>(std::forward<A>(arg)); }
            template<std::meta::info attribute_refl, class A>
            zb_zcl_status_t set_checked(A &&arg) { return set_raw<attribute_refl, true, A>(std::forward<A>(arg)); }

            /**********************************************************************/
            /* Sending commands                                                   */
            /**********************************************************************/

            struct send_cmd_config_t
            {
                cmd_send_status_cb_t cb = nullptr;
                uint32_t timeout_ms = kCmdTimeoutDefault;
            };

            template<std::meta::info cmd_out_refl, send_cmd_config_t cfg, class... Args> requires (!is_zb_addr_type_c<Args> && ...)
            std::optional<cmd_id_t> send_cmd(Args&&...args)
            {
                using pool_t = cmd_out_pool_t<cmd_out_refl>;
                return send_cmd_impl<cmd_out_refl, cfg>(pool_t::prepare_args(&on_send_cmd_cb, std::forward<Args>(args)...));
            }

            template<std::meta::info cmd_out_refl, send_cmd_config_t cfg, class... Args>
            std::optional<cmd_id_t> send_cmd(long_addr_t a, Args&&...args)
            {
                using pool_t = cmd_out_pool_t<cmd_out_refl>;
                return send_cmd_impl<cmd_out_refl, cfg>(pool_t::prepare_args(&on_send_cmd_cb, a.long_addr, a.ep, std::forward<Args>(args)...));
            }

            template<std::meta::info cmd_out_refl, send_cmd_config_t cfg, class... Args>
            std::optional<cmd_id_t> send_cmd(group_addr_t a, Args&&...args)
            {
                using pool_t = cmd_out_pool_t<cmd_out_refl>;
                return send_cmd_impl<cmd_out_refl, cfg>(pool_t::prepare_args(&on_send_cmd_cb, a.group, std::forward<Args>(args)...));
            }

            template<std::meta::info cmd_out_refl, send_cmd_config_t cfg, class... Args>
            std::optional<cmd_id_t> send_cmd(bind_id_addr_t a, Args&&...args)
            {
                using pool_t = cmd_out_pool_t<cmd_out_refl>;
                return send_cmd_impl<cmd_out_refl, cfg>(pool_t::prepare_args(&on_send_cmd_cb, a.bind_table_id, std::forward<Args>(args)...));
            }


            private:

            static void on_send_cmd_timeout(cmd_request_t *pCmdReq)
            {
                pCmdReq->cancel_req(pCmdReq->args_idx);
                auto *pCurrent = g_cmd_queue.peek();
                if (pCurrent == pCmdReq)
                    on_send_cmd_cb(0);
                else
                {
                    //how is this possible?
                }
            }

            static bool send_next_cmd(bool with_cb = true)
            {
                if (auto *pNextCmd = g_cmd_queue.peek())
                {
                    //try and send next request
                    if (!pNextCmd->send_req(pNextCmd->args_idx))
                    {
                        //couldn't send
                        auto cb = pNextCmd->cb;
                        auto cmd_id = pNextCmd->id;
                        g_cmd_queue.drop();
                        if (cb && with_cb)
                            cb(cmd_id, nullptr);
#if defined(DBG_CMD)
                        else
                            printk("failed to initiate send command request for %d (cb=%p)\r\n", cmd_id, cb);
#endif
                        return false;
                    }else if (pNextCmd->timeout_ms)
                        g_cmd_timeout_tracker.Setup([pNextCmd]{on_send_cmd_timeout(pNextCmd);}, pNextCmd->timeout_ms);
                    return true;
                }
                return false;
            }

            static void on_send_cmd_cb(zb_uint8_t param)
            {
                g_cmd_timeout_tracker.Cancel();
                auto *pCurrent = g_cmd_queue.peek();
                if (!pCurrent)
                {
#if defined(DBG_CMD)
                    printk("on_send_cmd_cb: param=%d; pCurrent=null; queue empty. unexpected\r\n", param);
#endif
                    return;
                }
                ZB_ASSERT(pCurrent);
                auto cmd_id = pCurrent->id;
                auto cb = pCurrent->cb;
#if defined(DBG_CMD)
                printk("on_send_cmd_cb: param=%d; id=%d; cb=%p; pCurrent=%p\r\n", param, cmd_id, (void*)cb, pCurrent);
#endif
                g_cmd_queue.drop();
                if (cb)
                {
                    zb_zcl_command_send_status_t *cmd_send_status = param ? ZB_BUF_GET_PARAM(param, zb_zcl_command_send_status_t) : nullptr;
                    cb(cmd_id, cmd_send_status);
                }

                if (param)
                    zb_buf_free(param);
                //if we cannot send commands we'll just drain the queue
                //sad but there's nothing much else we can do
                while(g_cmd_queue.peek() && !send_next_cmd());
            }

            template<std::meta::info cmd_out_refl, send_cmd_config_t cfg>
            std::optional<cmd_id_t> send_cmd_impl(auto args_pool_idx)
            {
                using pool_t = cmd_out_pool_t<cmd_out_refl>;
                typename pool_t::RequestPtr raii(pool_t::g_Pool.IdxToPtr(*args_pool_idx));
                constexpr auto kTimeout = cfg.timeout_ms == kCmdTimeoutDefault ? pool_t::cmd_a.timeout_ms : cfg.timeout_ms;
                auto r = g_cmd_queue.push(
                        /*struct cmd_request*/
                        /*id*/        g_cmd_num,
                        /*args_idx*/  *args_pool_idx,
                        /*send_req*/  &pool_t::template request<epa>,
                       /*cancel_req*/ &pool_t::cancel,
                        /*cb*/        cfg.cb,
                        /*timeout_ms*/kTimeout
                );
                if (!r) return std::nullopt;
                //if the size of the Queue is 1 it means this command is the only there
                //we need to send it right away
#if defined(DBG_CMD)
                printk("send_cmd1: id=%d; cb=%p\r\n", g_cmd_num, (void*)cfg.cb);
#endif
                if (*r == 1) 
                    if (!send_next_cmd(false))
                        return std::nullopt;
                raii.release();
                return g_cmd_num++;
            }
        };

        //actual place where ZBOSS structures for attributes, commands for each cluster are stored
        constinit static inline cluster_list_factory_t<ep_ref>::cluster_list_t clusters{};
        //actual place where ZBOSS structures for endpoints with cluster descriptions are stored
        constinit static inline ep_front_t value{
            {.ep_data{epa, cluster_list, meta_ctr_param_t<^^clusters>{}}}
        };
    };
}

#endif
