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
                            .data_p = &[:a.attribute:]
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

        cluster_data_type_t &cluster_struct;
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

    template<class StructTag, uint8_t ep>
    inline zb_bool_t on_cluster_cmd_handling(zb_uint8_t param)
    {
        return 0;
        //if ( ZB_ZCL_GENERAL_GET_CMD_LISTS_PARAM == param )
        //{
        //    ZCL_CTX().zb_zcl_cluster_cmd_list = cluster_custom_handler_t<StructTag, ep>::get_cmd_list();
        //    return ZB_TRUE;
        //}
        //
        //zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(param, zb_zcl_parsed_hdr_t);
        //cmd_handling_result_t r;
        //if (cmd_info->addr_data.common_data.dst_endpoint != ep)
        //{
        //    auto *pSlot = g_AdditionalClusterHandlers.find(cmd_info->addr_data.common_data.dst_endpoint, cmd_info->cluster_id);
        //    if (!pSlot)
        //    {
        //        r.status = RET_NOT_FOUND;
        //        r.processed = true;
        //    }else
        //        return pSlot->cmd_handler(param);
        //}else
        //    r = cluster_custom_handler_t<StructTag, ep>::on_cmd(cmd_info, std::span<uint8_t>{(uint8_t*)zb_buf_begin(param), zb_buf_len(param)});
        //
        //auto const& [status, processed] = r;
        //
        //if( processed )
        //{
        //    if( cmd_info->disable_default_response && status == RET_OK)
        //    {
        //        zb_buf_free(param);
        //    }
        //    else if (status == RET_NOT_IMPLEMENTED)
        //    {
        //        ZB_ZCL_PROCESS_COMMAND_FINISH(param, cmd_info, ZB_ZCL_STATUS_UNSUP_CMD);
        //    }
        //    else if (status != RET_BUSY)
        //    {
        //        ZB_ZCL_PROCESS_COMMAND_FINISH(param, cmd_info, status==RET_OK ? ZB_ZCL_STATUS_SUCCESS : ZB_ZCL_STATUS_INVALID_FIELD);
        //    }
        //}
        //
        //return processed;
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
        if constexpr (cluster_desc_t::N_cmd_in > 0)
        {
            //cmd_handler = &on_cluster_cmd_handling<StructTag, ep>;
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
