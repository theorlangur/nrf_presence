#ifndef ZB_META_ANNOTATIONS_HPP_
#define ZB_META_ANNOTATIONS_HPP_

#include "zb_meta_types.hpp"

namespace zbm
{
    struct attribute_a
    {
        using value_checker_t = zb_ret_t(*)(zb_uint8_t *value);

        zb_uint16_t id;
        access_t a = access_t::Read;
        type_t type = type_t::Invalid;//infer from type
        value_checker_t validator = {};                     

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

    struct cmd_a
    {
        uint8_t id;
    };

    struct cmd_in_a: cmd_a
    {
    };

    struct cmd_out_a: cmd_a
    {
        uint8_t pool_size = 1;
        uint16_t manuf_code = ZB_ZCL_MANUF_CODE_INVALID;
        uint32_t timeout_ms = 0;//no timeout
    };

    struct cluster_a
    {
        zb_uint16_t id;
        zb_uint16_t rev = 0;
        role_t        role = role_t::Server;
        zb_uint16_t manuf_code = ZB_ZCL_MANUF_CODE_INVALID;
        void (*pre_init)() = nullptr;

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

    struct ep_with_annotation
    {
        std::meta::info ep;
        ep_a annotation;
    };
    consteval std::vector<ep_with_annotation> extract_ep_from_device(std::meta::info device)
    {
        std::meta::info device_type = std::meta::remove_cvref(std::meta::type_of(device));
        auto mems = std::meta::nonstatic_data_members_of(device_type, std::meta::access_context::current());
        std::vector<ep_with_annotation> eps;
        for(auto mem_ep : mems)
        {
            auto ep_annotations = std::meta::annotations_of_with_type(mem_ep, ^^zbm::ep_a);
            if (!ep_annotations.empty())
                eps.emplace_back(mem_ep, std::meta::extract<ep_a>(ep_annotations[0]));
        }
        return eps;
    }

    template<class T>
    consteval attribute_a::value_checker_t get_attribute_validator_for_type()
    {
        if constexpr (requires { T::validate_value((uint8_t*)nullptr); })
            return &T::validate_value;
        else
            return {};
    }

    consteval attribute_a derive_member_annotation(std::meta::info mem, std::meta::info declared_a)
    {
        attribute_a res = std::meta::extract<attribute_a>(declared_a);
        if (res.type == type_t::Invalid)
        {
            //need to derive type
            auto mem_type = std::meta::type_of(mem);
            auto type_to_type_id_inst = std::meta::substitute(^^type_to_type_id, {mem_type});
            auto get_type = std::meta::extract<type_t(*)()>(type_to_type_id_inst);
            res.type = get_type();
        }

        if (!res.validator)
        {
            //try and provide the default
            auto mem_type = std::meta::type_of(mem);
            auto get_type_validator_inst = std::meta::substitute(^^get_attribute_validator_for_type, {mem_type});
            auto get_type_validator = std::meta::extract<attribute_a::value_checker_t(*)()>(get_type_validator_inst);
            res.validator = get_type_validator();
        }
        return res;
    }

    consteval std::optional<cluster_a> get_cluster_annotation(std::meta::info r_cluster)
    {
        std::meta::info cluster_type = r_cluster;
        if (!std::meta::is_type(cluster_type))
            cluster_type = std::meta::type_of(r_cluster);
        cluster_type = std::meta::remove_cvref(cluster_type);
        std::vector<std::meta::info> annotations;
        do
        {
            annotations = std::meta::annotations_of_with_type(r_cluster, ^^zbm::cluster_a);
            if (!annotations.empty())
                break;
            auto bases = std::meta::bases_of(cluster_type, std::meta::access_context::current());
            if (bases.empty())
                break;
            cluster_type = bases[0];
        }while(true);

        if (!annotations.empty())
        {
            auto a = annotations[0];
            return std::meta::extract<cluster_a>(a);
        }
        return std::nullopt;
    }

    struct cluster_with_annotation
    {
        std::meta::info cluster;
        cluster_a annotation;
    };
    consteval std::vector<cluster_with_annotation> extract_clusters_from_ep(std::meta::info ep)
    {
        std::meta::info ep_type = std::meta::remove_cvref(std::meta::type_of(ep));
        auto mems = std::meta::nonstatic_data_members_of(ep_type, std::meta::access_context::current());
        std::vector<cluster_with_annotation> clusters;
        for(auto mem_cluster : mems)
        {
            auto cluster_type = std::meta::type_of(mem_cluster);
            auto cluster_annotations = get_cluster_annotation(cluster_type);//std::meta::annotations_of_with_type(cluster_type, ^^zbm::cluster_a);
            if (cluster_annotations)
                clusters.emplace_back(mem_cluster, *cluster_annotations);
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
            auto attribute_annotations = std::meta::annotations_of_with_type(mem_attr, ^^zbm::attribute_a);
            if (!attribute_annotations.empty())
            {
                auto derived_a = derive_member_annotation(mem_attr, attribute_annotations[0]);
                attributes.emplace_back(mem_attr, derived_a);
            }
        }
        return attributes;
    }

    struct cmd_in_with_annotation
    {
        std::meta::info cmd;
        cmd_in_a annotation;
    };
    consteval std::vector<cmd_in_with_annotation> extract_incoming_commands_from_cluster(std::meta::info cluster)
    {
        ep_base_cfg_t res;
        std::meta::info cluster_type = std::meta::remove_cvref(std::meta::type_of(cluster));
        auto mems = std::meta::nonstatic_data_members_of(cluster_type, std::meta::access_context::current());
        std::vector<cmd_in_with_annotation> cmds;
        for(auto mem_attr : mems)
        {
            auto cmd_annotations = std::meta::annotations_of_with_type(mem_attr, ^^zbm::cmd_in_a);
            if (!cmd_annotations.empty())
                cmds.emplace_back(mem_attr, std::meta::extract<cmd_in_a>(cmd_annotations[0]));
        }
        return cmds;
    }

    struct cmd_out_with_annotation
    {
        std::meta::info cmd;
        cmd_out_a annotation;
    };
    consteval std::vector<cmd_out_with_annotation> extract_sending_commands_from_cluster(std::meta::info cluster)
    {
        ep_base_cfg_t res;
        std::meta::info cluster_type = std::meta::remove_cvref(std::meta::type_of(cluster));
        auto mems = std::meta::nonstatic_data_members_of(cluster_type, std::meta::access_context::current());
        std::vector<cmd_out_with_annotation> cmds;
        for(auto mem_attr : mems)
        {
            auto cmd_annotations = std::meta::annotations_of_with_type(mem_attr, ^^zbm::cmd_out_a);
            if (!cmd_annotations.empty())
                cmds.emplace_back(mem_attr, std::meta::extract<cmd_out_a>(cmd_annotations[0]));
        }
        return cmds;
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
}

#endif
