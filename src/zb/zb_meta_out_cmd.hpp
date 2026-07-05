#ifndef ZB_META_OUT_CMD_HPP_
#define ZB_META_OUT_CMD_HPP_

#include "zb_meta_annotations.hpp"

namespace zbm
{

    template<class Func> requires std::is_function_v<Func>
    struct cmd_out_t
    {
        //std::meta::info -> parameter types
        static constexpr auto g_Params = []() consteval{
            auto params = std::meta::parameters_of(^^Func);
            return std::define_static_array(params);
        }();

        //order of parameters to store in the pool (alignment sorted for optimal size)
        //mapped as: position 'idx' in stored location at pool => position of the parameter in the function signature
        static constexpr auto g_ParamsIndxSortedForStorage = []() consteval{
            std::array<int, g_Params.size()> indices{};
            for(int i = 0; i < g_Params.size(); ++i)
                indices[i] = i;
            std::sort(indices.begin(), indices.end(), [](auto p1, auto p2){ return std::meta::alignment_of(g_Params[p1]) > std::meta::alignment_of(g_Params[p2]); });
            return std::define_static_array(indices);
        }();
        //order of parameters to call when delivered from the pool
        //mapped as: position 'idx' of the parameter in the function signature => position of the member in the pool item structure
        static constexpr auto g_ParamsIndxSortedForCall = []() consteval{
            std::array<int, g_Params.size()> indices{};
            for(int i = 0; i < g_Params.size(); ++i)
                indices[i] = std::distance(g_ParamsIndxSortedForStorage.begin(), std::find(g_ParamsIndxSortedForStorage.begin(), g_ParamsIndxSortedForStorage.end(), i));
            return std::define_static_array(indices);
        }();

        //TODO:Put static inline pool here
        //TODO:provide a store-to-pool static function
        //TODO:provide 'call'-from-pool static function (rather stream-to-bytes)
    };
}

#endif
