#ifndef ZB_META_DEVICE_HPP_
#define ZB_META_DEVICE_HPP_

#include "zb_meta_ep.hpp"

namespace zbm
{
    namespace detail {
        //workaround for not working constexpr std::format
        // Fixed-capacity compile-time string generator
        template<unsigned ID>
        struct ep_name_provider 
        {
            static constexpr unsigned prefix_len = 3; // length of "cluster"
            static constexpr unsigned total_len = prefix_len + 2;//hex

            char chars[total_len + 1]{};

            constexpr ep_name_provider() {
                const char* prefix = "ep_";
                for (unsigned i = 0; i < prefix_len; ++i) {
                    chars[i] = prefix[i];
                }
                unsigned temp = ID;
                for (unsigned i = 0; i < 2; ++i) {
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

    template<std::meta::info dev_data_ref>
    struct device_factory_t
    {
        /*
         * struct device_t
         * {
             ep_create_t<>::ep_type_t &ep_01;//1 EP ID
             ep_create_t<>::ep_type_t &ep_02;//2 EP ID
         * };
         * */
        struct device_t;
        consteval{
            std::vector<std::meta::info> mems;
            template for(constexpr auto ei : define_static_array(extract_ep_from_device(dev_data_ref)))
            {
                constexpr auto nsdm_ep_mem = std::meta::reflect_object([:dev_data_ref:].[:ei.ep:]);
                constexpr auto ep_create_inst_t = std::meta::substitute(^^ep_create_t, {std::meta::reflect_constant(ei.ep), std::meta::reflect_constant(nsdm_ep_mem)});
                using ep_end_type_t = typename [:ep_create_inst_t:]::ep_front_t;
                //TODO: use correct ep_front_t
                //ep_ref = std::meta::dealias(std::meta::members_of(ep_ref, std::meta::access_context::current())[0]);//ep_create_t::ep_type_t
                constexpr auto ep_ref = std::meta::add_lvalue_reference(^^ep_end_type_t);
                constexpr auto name = detail::ep_name_provider<ei.annotation.ep>();
                mems.push_back(std::meta::data_member_spec(ep_ref, std::meta::data_member_options{.name = name.chars}));
            }
            std::meta::define_aggregate(^^device_t, mems);
        };
    };

    struct ep_refl_info
    {
        std::meta::info mem_decl;
        std::meta::info obj_ref;
    };

    template<std::meta::info dev_data_ref>
    consteval auto get_all_ep_ref()
    {
        std::vector<ep_refl_info> refs;
        template for(constexpr auto ei : define_static_array(extract_ep_from_device(dev_data_ref)))
            refs.emplace_back(ei.ep, std::meta::reflect_object([:dev_data_ref:].[:ei.ep:]));
        return define_static_array(refs);
    }

    template<std::meta::info dev_data_ref>
    struct device_full_t: device_factory_t<dev_data_ref>::device_t
    {
        using base_t = device_factory_t<dev_data_ref>::device_t;
        static constexpr auto ep_list = get_all_ep_ref<dev_data_ref>();
        constexpr device_full_t():
            device_full_t(std::make_index_sequence<ep_list.size()>{})
        {
        }

        zb_af_device_ctx_t* device_context() { return &ctx; }
    protected:
        template<size_t... I>
        constexpr device_full_t(std::index_sequence<I...>):
            base_t{
                ep_create_t<ep_list[I].mem_decl, ep_list[I].obj_ref>::value...
            },
            endpoints{
                &ep_create_t<ep_list[I].mem_decl, ep_list[I].obj_ref>::value.ep_data.ep...
            },
            ctx{
                .ep_count = ep_list.size(),
                .ep_desc_list = endpoints
            }
        {}

        zb_af_endpoint_desc_t *endpoints[ep_list.size()];
        zb_af_device_ctx_t ctx;
    };
}

#endif
