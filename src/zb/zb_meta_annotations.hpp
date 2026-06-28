#ifndef ZB_META_ANNOTATIONS_HPP_
#define ZB_META_ANNOTATIONS_HPP_

#include "zb_meta_types.hpp"

namespace zbm
{
    struct attribute_a
    {
        zb_uint16_t id;
        access_t a = access_t::Read;
        type_t type = type_t::Invalid;//infer from type
                                      
        //TODO: validator
        //constexpr inline bool has_validator() const { return validator != nullptr; }

        constexpr inline bool has_access(access_t _a) const { return a & _a; } 
        constexpr inline bool is_cvc() const { 
            if (a & access_t::Report)
            {
                switch(type)
                {
                    case type_t::S8:
                    case type_t::U8:
                    case type_t::S16:
                    case type_t::U16:
                    case type_t::S24:
                    case type_t::U24:
                    case type_t::S32:
                    case type_t::U32:
                    case type_t::Float:
                    case type_t::HalfFloat:
                    case type_t::Double:
                        return true;
                    default:
                        break;
                }
            }
            return false;
        } 
    };

    struct cluster_a
    {
        zb_uint16_t id;
        zb_uint16_t rev = 0;
        role_t        role = role_t::Server;
        zb_uint16_t manuf_code = ZB_ZCL_MANUF_CODE_INVALID;

        constexpr bool operator==(cluster_a const&) const = default;
        constexpr bool operator<(cluster_a const& rhs) const { return role < rhs.role; }
        constexpr bool operator<=(cluster_a const& rhs) const { return role <= rhs.role; }
        constexpr bool operator>(cluster_a const& rhs) const { return role > rhs.role; }
        constexpr bool operator>=(cluster_a const& rhs) const { return role >= rhs.role; }
    };

    struct ep_a
    {
        zb_uint8_t ep;
        zb_uint16_t dev_id;
        zb_uint8_t dev_ver;
        uint8_t cmd_queue_depth = 0;//0 - auto
    };

    inline constexpr zb_zcl_attr_t g_LastAttribute{
        .id = ZB_ZCL_NULL_ID,
        .type = ZB_ZCL_ATTR_TYPE_NULL,
        .access = (zb_uint8_t)access_t::None,
        .manuf_code = ZB_ZCL_NON_MANUFACTURER_SPECIFIC,
        .data_p = nullptr
    };

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
}

#endif
