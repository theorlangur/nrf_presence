#ifndef ZB_META_TYPES_HPP_
#define ZB_META_TYPES_HPP_

extern "C" {
#include <zboss_api.h>
}
#include <type_traits>
#include <meta>
#include <ranges>

namespace zbm
{
    enum class access_t: zb_uint8_t
    {
        None = 0,
        Read = 0x01,
        Write = 0x02,
        Report = 0x04,
        Singleton = 0x08,
        Scene = 0x10,
        ManuSpec = 0x20,
        Internal = 0x40,

        RW = Read | Write,
        RWP = RW | Report,
        RP = Read | Report,
        RPS = RP | Report | Scene,
    };
    constexpr access_t operator|(access_t a1, access_t a2) { return access_t(zb_uint8_t(a1) | zb_uint8_t(a2)); }
    constexpr bool operator&(access_t a1, access_t a2) { return (zb_uint8_t(a1) & zb_uint8_t(a2)) != 0; }

    enum class type_t: zb_uint8_t
    {
        Null          = ZB_ZCL_ATTR_TYPE_NULL,
        Raw8          = ZB_ZCL_ATTR_TYPE_8BIT,
        Raw16         = ZB_ZCL_ATTR_TYPE_16BIT,
        Raw24         = ZB_ZCL_ATTR_TYPE_24BIT,
        Raw32         = ZB_ZCL_ATTR_TYPE_32BIT,
        Raw40         = ZB_ZCL_ATTR_TYPE_40BIT,
        Raw48         = ZB_ZCL_ATTR_TYPE_48BIT,
        Raw56         = ZB_ZCL_ATTR_TYPE_56BIT,
        Raw64         = ZB_ZCL_ATTR_TYPE_64BIT,
        Bool          = ZB_ZCL_ATTR_TYPE_BOOL,
        Map8          = ZB_ZCL_ATTR_TYPE_8BITMAP,
        Map16         = ZB_ZCL_ATTR_TYPE_16BITMAP,
        Map24         = ZB_ZCL_ATTR_TYPE_24BITMAP,
        Map32         = ZB_ZCL_ATTR_TYPE_32BITMAP,
        Map40         = ZB_ZCL_ATTR_TYPE_40BITMAP,
        Map48         = ZB_ZCL_ATTR_TYPE_48BITMAP,
        Map56         = ZB_ZCL_ATTR_TYPE_56BITMAP,
        Map64         = ZB_ZCL_ATTR_TYPE_64BITMAP,
        U8            = ZB_ZCL_ATTR_TYPE_U8,
        U16           = ZB_ZCL_ATTR_TYPE_U16,
        U24           = ZB_ZCL_ATTR_TYPE_U24,
        U32           = ZB_ZCL_ATTR_TYPE_U32,
        U40           = ZB_ZCL_ATTR_TYPE_U40,
        U48           = ZB_ZCL_ATTR_TYPE_U48,
        U56           = ZB_ZCL_ATTR_TYPE_U56,
        U64           = ZB_ZCL_ATTR_TYPE_U64,
        S8            = ZB_ZCL_ATTR_TYPE_S8,
        S16           = ZB_ZCL_ATTR_TYPE_S16,
        S24           = ZB_ZCL_ATTR_TYPE_S24,
        S32           = ZB_ZCL_ATTR_TYPE_S32,
        S40           = ZB_ZCL_ATTR_TYPE_S40,
        S48           = ZB_ZCL_ATTR_TYPE_S48,
        S56           = ZB_ZCL_ATTR_TYPE_S56,
        S64           = ZB_ZCL_ATTR_TYPE_S64,
        E8            = ZB_ZCL_ATTR_TYPE_8BIT_ENUM,
        E16           = ZB_ZCL_ATTR_TYPE_16BIT_ENUM,
        HalfFloat     = ZB_ZCL_ATTR_TYPE_SEMI,
        Float         = ZB_ZCL_ATTR_TYPE_SINGLE,
        Double        = ZB_ZCL_ATTR_TYPE_DOUBLE,
        OctetStr      = ZB_ZCL_ATTR_TYPE_OCTET_STRING,
        CharStr       = ZB_ZCL_ATTR_TYPE_CHAR_STRING,
        LongOctetStr  = ZB_ZCL_ATTR_TYPE_LONG_OCTET_STRING,
        LongCharStr   = ZB_ZCL_ATTR_TYPE_LONG_CHAR_STRING,
        Array         = ZB_ZCL_ATTR_TYPE_ARRAY,
        Struct        = ZB_ZCL_ATTR_TYPE_STRUCTURE,
        Set           = ZB_ZCL_ATTR_TYPE_SET,
        Bag           = ZB_ZCL_ATTR_TYPE_BAG,
        TimeOfDay     = ZB_ZCL_ATTR_TYPE_TIME_OF_DAY,
        Date          = ZB_ZCL_ATTR_TYPE_DATE,
        UTCTime       = ZB_ZCL_ATTR_TYPE_UTC_TIME,
        ClusterID     = ZB_ZCL_ATTR_TYPE_CLUSTER_ID,
        AttributeID   = ZB_ZCL_ATTR_TYPE_ATTRIBUTE_ID,
        BACnetOID     = ZB_ZCL_ATTR_TYPE_BACNET_OID,
        IEEEAddr      = ZB_ZCL_ATTR_TYPE_IEEE_ADDR,
        Sec128Key     = ZB_ZCL_ATTR_TYPE_128_BIT_KEY,
        Custom32Array = ZB_ZCL_ATTR_TYPE_CUSTOM_32ARRAY,
        Invalid       = ZB_ZCL_ATTR_TYPE_INVALID
    };

    enum class role_t: zb_uint8_t
    {
        Invalid = 0x00,
        Server = 0x01,
        Client = 0x02,
        Any = Server | Client
    };

    enum class addr_mode_t: uint8_t
    {
        NoAddr_NoEP = ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        Group_NoEP = ZB_APS_ADDR_MODE_16_GROUP_ENDP_NOT_PRESENT,
        Dst16EP = ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        Dst64EP = ZB_APS_ADDR_MODE_64_ENDP_PRESENT,
        EPAsBindTableId = ZB_APS_ADDR_MODE_BIND_TBL_ID
    };

    enum class frame_direction_t: uint8_t
    {
        ToServer = ZB_ZCL_FRAME_DIRECTION_TO_SRV,
        ToClient = ZB_ZCL_FRAME_DIRECTION_TO_CLI
    };


    union frame_ctl_t
    {
        struct{
            uint8_t cluster_specific     : 1;
            uint8_t manufacture_specific : 1;
            frame_direction_t direction     : 1;
            uint8_t disable_default_response : 1;
        } f;
        uint8_t u8;
    };

    template<class T>
    constexpr type_t type_to_type_id()
    {
        if constexpr (std::is_same_v<T,zb_uint8_t>)                return type_t::U8;
        else if constexpr (std::is_same_v<T,zb_uint16_t>)          return type_t::U16;
        else if constexpr (std::is_same_v<T,zb_uint32_t>)          return type_t::U32;
        else if constexpr (std::is_same_v<T,zb_uint64_t>)          return type_t::U64;
        else if constexpr (std::is_same_v<T,zb_int8_t>)            return type_t::S8;
        else if constexpr (std::is_same_v<T,zb_int16_t>)           return type_t::S16;
        else if constexpr (std::is_same_v<T,zb_int32_t>)           return type_t::S32;
        else if constexpr (std::is_same_v<T,zb_int64_t>)           return type_t::S64;
        else if constexpr (std::is_same_v<T,std::nullptr_t>)       return type_t::Null;
        else if constexpr (std::is_enum_v<T> && sizeof(T) == 1)    return type_t::E8;
        else if constexpr (std::is_enum_v<T> && sizeof(T) == 2)    return type_t::E16;
        else if constexpr (std::is_same_v<T,float>)                return type_t::Float;
        else if constexpr (std::is_same_v<T,double>)               return type_t::Double;
        else if constexpr (std::is_same_v<T,bool>)                 return type_t::Bool;
        else if constexpr (requires { T::type_id(); })              return T::type_id();
        else 
            static_assert(sizeof(T) == 0, "Unknown type");
        return type_t::Invalid;
    }

    template<class T>
    concept serializable_c = requires(T t, uint8_t *pDst, const uint8_t *pSrc, size_t len){
        { t.serialize_to(pDst, len) } -> std::same_as<std::optional<uint8_t*>>;
        { t.serialize_from(pSrc, len) } -> std::same_as<std::optional<const uint8_t*>>;
    };
}

#endif
