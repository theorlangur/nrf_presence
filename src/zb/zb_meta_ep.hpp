#ifndef ZB_META_EP_HPP_
#define ZB_META_EP_HPP_

#include "zb_meta_types.hpp"
#include "zb_meta_annotations.hpp"
#include "zb_meta_cluster.hpp"


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

    template<ep_base_cfg_t cfg, uint8_t ep_id>
    struct ep_base_t
    {
        using SimpleDesc = simple_desc_t<cfg.server_clusters, cfg.client_clusters>;

        consteval ep_base_t(ep_base_t const&) = delete;
        consteval ep_base_t(ep_base_t &&) = delete;
        consteval ep_base_t(ep_a epa, auto clusters, auto &local_clusters):
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

            static constexpr auto local_cluster_r = std::meta::remove_cvref(^^decltype(local_clusters));
            static constexpr auto local_cluster_mems = std::define_static_array(std::meta::nonstatic_data_members_of(local_cluster_r, std::meta::access_context::current()));
            int i = 0;
            template for(constexpr auto m : local_cluster_mems)
            {
                auto const& ca = clusters[i];
                static constexpr auto cluster_refl = ^^typename decltype(local_clusters.[:m:])::cluster_data_type_t;
                clusters_descriptions[i] = zb_zcl_cluster_desc_t{
                    .cluster_id = ca.annotation.id,
                    .attr_count = std::size(local_clusters.[:m:].attributes),
                    .attr_desc_list = local_clusters.[:m:].attributes,
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

    template<std::meta::info epm>
    struct ep_factory_t
    {
        struct ep_t;//it's important this is a first declaration
        static constexpr ep_a g_Annotation = get_ep_annotations(epm);//source of constexpr template-capable ep_id
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

    consteval std::meta::info get_ep_type_from_factory(std::meta::info ep_ref)
    {
        auto ep_fact_inst = std::meta::substitute(^^ep_factory_t, {std::meta::reflect_constant(ep_ref)});
        return std::define_static_array(std::meta::members_of(ep_fact_inst, std::meta::access_context::current()))[0];
    } 


    template<std::meta::info ep_ref> requires (!std::meta::annotations_of_with_type(ep_ref, ^^zbm::ep_a).empty() && !extract_clusters_from_ep(ep_ref).empty())
    struct ep_create_t
    {
        using ep_type_t = [:get_ep_type_from_factory(ep_ref):];
        static constexpr auto epa = get_ep_annotations(ep_ref);
        static constexpr auto cluster_list = define_static_array(extract_clusters_from_ep(ep_ref));
        static constexpr auto cluster_refs = []() consteval{
            std::vector<std::meta::info> refs;
            template for(constexpr auto ci : cluster_list)
                refs.push_back(std::meta::reflect_object([:ep_ref:].[:ci.cluster:]));
            return define_static_array(refs);
        }();

        constinit static inline cluster_list_factory_t<ep_ref>::cluster_list_t clusters{};
        constinit static inline ep_type_t value{
            .ep_data{epa, cluster_list, clusters}
        };
    };
}

#endif
