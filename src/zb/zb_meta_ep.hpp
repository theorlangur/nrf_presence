#ifndef ZB_META_EP_HPP_
#define ZB_META_EP_HPP_

#include "zb_meta_types.hpp"
#include "zb_meta_annotations.hpp"

#include <meta>
#include <ranges>
#include <format>

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

    struct ep_base_cfg_t
    {
        size_t server_clusters = 0;
        size_t client_clusters = 0;
        size_t reporting_attributes = 0;
        size_t cvc_attributes = 0;

        constexpr ep_base_cfg_t& operator+=(ep_base_cfg_t r)
        {
            server_clusters += r.server_clusters;
            client_clusters += r.client_clusters;
            reporting_attributes += r.reporting_attributes;
            cvc_attributes += r.cvc_attributes;
            return *this;
        }
        constexpr bool operator==(const ep_base_cfg_t&) const = default;
    };

    template<ep_base_cfg_t cfg>
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
                            .app_cluster_list = {/*TODO: implement*/}
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
                    clusters_descriptions[i] = zb_zcl_cluster_desc_t{
                        .cluster_id = ca.annotation.id,
                        .attr_count = std::size(local_clusters.[:m:].attributes),
                        .attr_desc_list = local_clusters.[:m:].attributes,
                        .role_mask = (zb_uint8_t)ca.annotation.role,
                        .manuf_code = ca.annotation.manuf_code,
                        .cluster_init = nullptr//TODO: &generic_cluster_init<StructTag, ep>
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

    consteval attribute_a derive_member_annotation(std::meta::info mem, std::meta::info declared_a)
    {
        attribute_a res = std::meta::extract<attribute_a>(declared_a);
        if (res.type == type_t::Invalid)
        {
            //need to derive type
            auto mem_type = std::meta::type_of(mem);
            auto type_to_type_id_inst = std::meta::substitute(^^TypeToTypeId, {mem_type});
            auto get_type = std::meta::extract<type_t(*)()>(type_to_type_id_inst);
            res.type = get_type();
        }
        return res;
    }

    consteval ep_base_cfg_t analyze_cluster(std::meta::info r_cluster)
    {
        ep_base_cfg_t res;
        auto mems = std::meta::nonstatic_data_members_of(r_cluster, std::meta::access_context::current());
        for(auto m : mems)
        {
            auto annotations = std::meta::annotations_of_with_type(m, ^^zbm::attribute_a);
            if (!annotations.empty())
            {
                auto attr_desc = derive_member_annotation(m, annotations[0]);
                res.reporting_attributes += (attr_desc.a & access_t::Report) ? 1 : 0;
                res.cvc_attributes += attr_desc.is_cvc();
            }
        }
        auto cluster_annotations = std::meta::annotations_of_with_type(r_cluster, ^^zbm::cluster_a);
        if (!cluster_annotations.empty())
        {
            auto cluster_desc = std::meta::extract<cluster_a>(cluster_annotations[0]);
            if (cluster_desc.role == role_t::Server)
                res.server_clusters += 1;
            else if (cluster_desc.role == role_t::Client)
                res.client_clusters += 1;
        }
        return res;
    }

    consteval ep_base_cfg_t analyze_clusters(std::vector<std::meta::info> r_clusters)
    {
        ep_base_cfg_t res;
        for(auto c : r_clusters)
            res += analyze_cluster(c);
        return res;
    }

    struct cluster_with_annotation
    {
        std::meta::info cluster;
        cluster_a annotation;
    };
    consteval std::vector<cluster_with_annotation> extract_clusters_from_ep(std::meta::info ep)
    {
        ep_base_cfg_t res;
        std::meta::info ep_type = std::meta::remove_cvref(std::meta::type_of(ep));
        auto mems = std::meta::nonstatic_data_members_of(ep_type, std::meta::access_context::current());
        std::vector<cluster_with_annotation> clusters;
        for(auto mem_cluster : mems)
        {
            auto cluster_type = std::meta::type_of(mem_cluster);
            auto cluster_annotations = std::meta::annotations_of_with_type(cluster_type, ^^zbm::cluster_a);
            if (!cluster_annotations.empty())
                clusters.emplace_back(mem_cluster, std::meta::extract<cluster_a>(cluster_annotations[0]));
        }
        //sort clusters: first server, then client. cluster_a knows how, see operator<
        std::ranges::sort(clusters, {}, &cluster_with_annotation::annotation);
        return clusters;
    }

    struct attribute_with_annotation
    {
        std::meta::info attribute;
        attribute_a annotation;
    };
    consteval std::vector<attribute_with_annotation> extract_attributes_from_cluster(std::meta::info cluster)
    {
        ep_base_cfg_t res;
        std::meta::info cluster_type = std::meta::remove_cvref(std::meta::type_of(cluster));
        auto mems = std::meta::nonstatic_data_members_of(cluster_type, std::meta::access_context::current());
        std::vector<attribute_with_annotation> attributes;
        for(auto mem_attr : mems)
        {
            auto attr_type = std::meta::type_of(mem_attr);
            auto attribute_annotations = std::meta::annotations_of_with_type(attr_type, ^^zbm::attribute_a);
            if (!attribute_annotations.empty())
                attributes.emplace_back(mem_attr, std::meta::extract<attribute_a>(attribute_annotations[0]));
        }
        return attributes;
    }

    template<std::meta::info epm>
        struct ep_factory_t
        {
            struct ep_t;
            consteval{
                std::meta::info ep_type = std::meta::remove_cvref(std::meta::type_of(epm));
                auto mems = std::meta::nonstatic_data_members_of(ep_type, std::meta::access_context::current());
                std::vector<std::meta::info> cluster_types;
                for(auto m : mems)
                    cluster_types.push_back(std::meta::type_of(m));
                if (!cluster_types.empty())
                {
                    ep_base_cfg_t cfg = analyze_clusters(cluster_types);
                    auto ep_data_type = std::meta::substitute(^^ep_base_t, {std::meta::reflect_constant(cfg)});
                    std::meta::define_aggregate(^^ep_t, {
                            std::meta::data_member_spec(ep_data_type, std::meta::data_member_options{"ep_data"})
                            });
                }
            };
        };

    template<std::meta::info cluster_ref>
        struct cluster_t
        {
            using cluster_data_type_t = [: []() consteval{ return std::meta::remove_cvref(std::meta::type_of(cluster_ref)); }() :];
            static constexpr cluster_a g_ClusterA = []() consteval{
                return std::meta::extract<zbm::cluster_a>(std::meta::annotations_of_with_type(std::meta::dealias(^^cluster_data_type_t), ^^zbm::cluster_a)[0]);
            }();
            static constexpr size_t N = extract_attributes_from_cluster(cluster_ref).size();
            static constexpr auto attributes_info = std::define_static_array(extract_attributes_from_cluster(cluster_ref));

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
                }

            cluster_data_type_t &cluster_struct;
            alignas(4) zb_zcl_attr_t attributes[N + 2];
            zb_uint16_t rev;
        };

    namespace detail {
        // Simple compile-time digit counter
        constexpr unsigned count_digits(unsigned val) {
            return val < 10 ? 1 : 1 + count_digits(val / 10);
        }

        // Fixed-capacity compile-time string generator
        template<unsigned ID>
            struct cluster_name_provider {
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

    template<std::meta::info ep_ref> requires (!std::meta::annotations_of_with_type(ep_ref, ^^zbm::ep_a).empty() && !extract_clusters_from_ep(ep_ref).empty())
        struct ep_create_t
        {
            using ep_type_t = [:
                []() consteval{
                    auto ep_fact_inst = std::meta::substitute(^^ep_factory_t, {std::meta::reflect_constant(ep_ref)});
                    return std::define_static_array(std::meta::members_of(ep_fact_inst, std::meta::access_context::current()))[0];
                }() :];
            static constexpr auto epa = []() consteval{
                auto ep_annotations = std::define_static_array(std::meta::annotations_of_with_type(ep_ref, ^^zbm::ep_a));
                return std::meta::extract<ep_a>(ep_annotations[0]);
            }();
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
