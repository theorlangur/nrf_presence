#ifndef ZB_META_CLUSTER_HPP_
#define ZB_META_CLUSTER_HPP_

#include "zb_meta_types.hpp"
#include "zb_meta_annotations.hpp"

namespace zbm
{
    template<std::meta::info cluster_ref>
    struct cluster_t
    {
        using cluster_data_type_t = [: []() consteval{ return std::meta::remove_cvref(std::meta::type_of(cluster_ref)); }() :];
        static constexpr cluster_a g_ClusterA = []() consteval{
            return std::meta::extract<zbm::cluster_a>(std::meta::annotations_of_with_type(std::meta::dealias(^^cluster_data_type_t), ^^zbm::cluster_a)[0]);
        }();
        static constexpr auto cluster_data_ref_refl = cluster_ref;
        static constexpr auto attributes_info = std::define_static_array(extract_attributes_from_cluster(cluster_ref));
        static constexpr size_t N = attributes_info.size();
        static constexpr auto cmd_in_info = std::define_static_array(extract_incoming_commands_from_cluster(cluster_ref));
        static constexpr size_t N_cmd_in = cmd_in_info.size();
        static constexpr auto cmd_out_info = std::define_static_array(extract_sending_commands_from_cluster(cluster_ref));
        static constexpr size_t N_cmd_out = cmd_out_info.size();

        consteval cluster_t():
            cluster_struct([:cluster_ref:]),
            attributes{
                {
                    .id = ZB_ZCL_ATTR_GLOBAL_CLUSTER_REVISION_ID, 
                    .type = (zb_uint8_t)type_t::Invalid, 
                    .access = (zb_uint8_t)access_t::Read, 
                    .manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC, 
                    .data_p = &rev
                }
            },
            rev(g_ClusterA.rev)
            {

                /**********************************************************************/
                /* attributes                                                         */
                /**********************************************************************/
                size_t i = 1;
                template for(constexpr auto a : attributes_info)
                {
                    attributes[i++] = zb_zcl_attr_t{
                        .id = a.annotation.id, 
                            .type = (zb_uint8_t)a.annotation.type, 
                            .access = (zb_uint8_t)a.annotation.a, 
                            .manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC, 
                            .data_p = &[:cluster_ref:].[:a.attribute:]
                    };
                }
                attributes[N + 1] = g_LastAttribute;

                /**********************************************************************/
                /* incoming commands                                                  */
                /**********************************************************************/
                i = 0;
                template for(constexpr auto a : cmd_in_info)
                    received_commands[i++] = a.annotation.id; 

                /**********************************************************************/
                /* outgoing commands                                                  */
                /**********************************************************************/
                i = 0;
                template for(constexpr auto a : cmd_out_info)
                    generated_commands[i++] = a.annotation.id; 
            }

        cluster_data_type_t &cluster_struct;//TODO: is it needed?
        alignas(4) zb_zcl_attr_t attributes[N + 2];
        zb_uint16_t rev;

        zb_uint8_t received_commands[N_cmd_in];
        zb_uint8_t generated_commands[N_cmd_out];

        zb_discover_cmd_list_t cmd_list =
        {
          zb_uint8_t(N_cmd_in), received_commands,
          zb_uint8_t(N_cmd_out), generated_commands
        };
    };

    namespace detail {
        //workaround for not working constexpr std::format
        // Fixed-capacity compile-time string generator
        template<unsigned ID>
        struct cluster_name_provider 
        {
            static constexpr unsigned prefix_len = 8; // length of "cluster"
            static constexpr unsigned total_len = prefix_len + 4;//hex

            char chars[total_len + 1]{};

            constexpr cluster_name_provider() {
                const char* prefix = "cluster_";
                for (unsigned i = 0; i < prefix_len; ++i) {
                    chars[i] = prefix[i];
                }
                unsigned temp = ID;
                for (unsigned i = 0; i < 4; ++i) {
                    if ((temp & 0xf) < 10)
                        chars[total_len - 1 - i] = '0' + (temp & 0xf);
                    else
                        chars[total_len - 1 - i] = 'a' + (temp & 0xf) - 10;
                    temp >>= 4;
                }
                chars[total_len] = '\0';
            }
        };
    }

    template<std::meta::info ep_ref>
    struct cluster_list_factory_t
    {
        /*
         * struct cluster_list_t
         * {
         *   cluster_t cluster_abcd;//0xabcd - cluster ID
         *   cluster_t cluster_0012;//0x0012 - cluster ID
         *   ...
         *   cluster_t cluster_8321;
         * };
         * */
        struct cluster_list_t;
        consteval{
            std::vector<std::meta::info> mems;
            template for(constexpr auto ci : define_static_array(extract_clusters_from_ep(ep_ref)))
            {
                auto c_ref = std::meta::substitute(^^cluster_t, {std::meta::reflect_constant(std::meta::reflect_object([:ep_ref:].[:ci.cluster:]))});
                constexpr auto name = detail::cluster_name_provider<ci.annotation.id>();
                mems.push_back(std::meta::data_member_spec(c_ref, std::meta::data_member_options{.name = name.chars}));
            }
            std::meta::define_aggregate(^^cluster_list_t, mems);
        };
    };

    struct cmd_to_arg_t
    {
        const uint8_t *pData;
        template<class A> requires (alignof(A) == 1)
        const A& extract()
        {
            const A *p = (const A*)pData;
            pData += sizeof(A);
            return *p;
        }

        template<class A> requires (alignof(A) > 1)
        A extract()
        {
            static_assert(sizeof(A) <= 4, "type_t is too big");
            const A *p = (const A*)pData;
            pData += sizeof(A);
            A ret;
            memcpy(&ret, p, sizeof(A));
            return ret;
        }
    };

    struct cmd_handling_result_t
    {
        zb_ret_t status = RET_OK;
        bool processed = true;
    };

    template<std::meta::info cmd_t_refl>
    consteval std::optional<std::meta::info> get_fitting_call_operator_overload()
    {
        for(auto m : std::meta::members_of(cmd_t_refl, std::meta::access_context::current()))
        {
            auto t = std::meta::type_of(m);
            if (std::meta::is_function_type(t))
            {
                if (std::meta::return_type_of(t) == ^^cmd_handling_result_t)
                    return t;
            }
        }
        return std::nullopt;
    }

    struct cmd_handler_info_t
    {
        size_t totalParameterSize;
        std::meta::info functionType;
    };
    template<std::meta::info cmd_t_refl>
    consteval cmd_handler_info_t analyze_cmd_handler()
    {
        if constexpr (std::meta::is_pointer_type(cmd_t_refl))
        {
            size_t res = 0;
            constexpr auto no_ptr = std::meta::remove_pointer(cmd_t_refl);
            static_assert(std::meta::is_function_type(no_ptr), "A handler of pointer type shall only be a pointer to a function");
            static_assert(std::meta::return_type_of(no_ptr) == ^^cmd_handling_result_t, "Command handling must return cmd_handling_result_t");
            for(auto m : std::meta::parameters_of(no_ptr))
                res += std::meta::size_of(m);
            return {res, no_ptr};
        }else
        {
            size_t res = 0;
            static_assert(std::meta::is_object_type(cmd_t_refl), "If not a function pointer, than it shall be a functor");
            constexpr auto func_call_t_refl = get_fitting_call_operator_overload<cmd_t_refl>();
            static_assert(func_call_t_refl, "Could not find fitting operator() overload");
            for(auto m : std::meta::parameters_of(*func_call_t_refl))
                res += std::meta::size_of(m);
            return {res, *func_call_t_refl};
        }
    }

    template<std::meta::info cluster_data_ref, cmd_in_with_annotation ci>
    cmd_handling_result_t call_cmd(zb_zcl_parsed_hdr_t *cmd_info, std::span<uint8_t> raw_data)
    {
        constexpr auto cmd_t_refl = std::meta::type_of(ci.cmd);
        if constexpr (std::meta::is_pointer_type(cmd_t_refl))
            if (!([:cluster_data_ref:].[:ci.cmd:])) return {RET_OK, false};

        constexpr cmd_handler_info_t handler_info = analyze_cmd_handler<cmd_t_refl>();
        if (raw_data.size() < handler_info.totalParameterSize) return {RET_ILLEGAL_REQUEST, true};
        static constexpr auto params = std::define_static_array(std::meta::parameters_of(handler_info.functionType));
        return [&]<size_t... I>(std::index_sequence<I...>)
        {
            cmd_to_arg_t args(raw_data.data());
            return ([:cluster_data_ref:].[:ci.cmd:])(args.extract<typename [:params[I]:]>()...);
        }(std::make_index_sequence<params.size()>());
    }

    template<std::meta::info cluster_ref, uint8_t ep/*is this really needed?*/>
    inline zb_bool_t on_cluster_cmd_handling(zb_uint8_t param)
    {
        if ( ZB_ZCL_GENERAL_GET_CMD_LISTS_PARAM == param )
        {
            ZCL_CTX().zb_zcl_cluster_cmd_list = &[:cluster_ref:].cmd_list;
            return ZB_TRUE;
        }
        //TODO: check ep
        zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(param, zb_zcl_parsed_hdr_t);
        using cluster_t = [:std::meta::remove_cvref(std::meta::type_of(cluster_ref)):];
        template for (constexpr auto cmdInfo : cluster_t::cmd_in_info)
        {
            if (cmdInfo.annotation.id == cmd_info->cmd_id)
            {
                //call that command
                cmd_handling_result_t r = call_cmd<cluster_t::cluster_data_ref_refl, cmdInfo>(cmd_info, std::span<uint8_t>{(uint8_t*)zb_buf_begin(param), zb_buf_len(param)});
                auto const& [status, processed] = r;

                if(processed)
                {
                    if( cmd_info->disable_default_response && status == RET_OK)
                    {
                        zb_buf_free(param);
                    }
                    else if (status == RET_NOT_IMPLEMENTED)
                    {
                        ZB_ZCL_PROCESS_COMMAND_FINISH(param, cmd_info, ZB_ZCL_STATUS_UNSUP_CMD);
                    }
                    else if (status != RET_BUSY)
                    {
                        ZB_ZCL_PROCESS_COMMAND_FINISH(param, cmd_info, status==RET_OK ? ZB_ZCL_STATUS_SUCCESS : ZB_ZCL_STATUS_INVALID_FIELD);
                    }
                }

                return processed;
            }
        }
        return false;
    }

    template<std::meta::info cluster_r, uint8_t ep>
    void generic_cluster_init()
    {
        using cluster_desc_t = [:std::meta::remove_cvref(std::meta::type_of(cluster_r)):];
        //using zcl_desc_t = zcl_description_t<StructTag>;
        //constexpr auto d = zcl_description_t<StructTag>::get();
        //if constexpr (requires { zcl_desc_t::zboss_init_func(d.info().role); })
        //{
        //    //there's a default ZBOSS init func -> try and call it
        //    auto zboss_init_f = zcl_desc_t::zboss_init_func(d.info().role);
        //    //Note: this will work poorly when same cluster is used for different end points
        //    if (zboss_init_f) zboss_init_f();
        //}
        zb_zcl_cluster_check_value_t check_val = nullptr;
        zb_zcl_cluster_write_attr_hook_t write_hook = nullptr;
        zb_zcl_cluster_handler_t cmd_handler = nullptr;
        if constexpr (cluster_desc_t::N_cmd_in >= 0)
        {
            cmd_handler = &on_cluster_cmd_handling<cluster_r, ep>;
        }
        //
        //if constexpr (d.count_members_with_validators() > 0)
        //    check_val = &on_cluster_check_value<StructTag, ep>;

        if (check_val || write_hook || cmd_handler)
        {
            //constexpr auto i = d.info();
            //zb_ret_t ret = zb_zcl_add_cluster_handlers(i.id, (uint8_t)i.role
            //        , check_val /*cluster_check_value*/
            //        , write_hook /*cluster_write_attr_hook*/
            //        , cmd_handler /*cluster_handler*/
            //        );
            //if (ret == RET_ALREADY_EXISTS)
            {
                //auto *pSlot = g_AdditionalClusterHandlers.add();
                //if (pSlot)
                //{
                //    pSlot->ep = ep;
                //    pSlot->cluster = i.id;
                //    pSlot->checker = check_val;
                //    pSlot->cmd_handler = cmd_handler;
                //}else if (g_GlobalErrorHandler)
                //{
                //    g_GlobalErrorHandler(RET_NO_MEMORY);
                //}
            }
        }
    }
}

#endif
