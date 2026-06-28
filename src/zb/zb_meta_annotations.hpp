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
}

#endif
