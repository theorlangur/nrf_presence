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
}

#endif
