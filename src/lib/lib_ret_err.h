#ifndef LIB_RET_ERR_H_
#define LIB_RET_ERR_H_

#include <cstddef>
#include <tuple>

struct Err
{
    const char *pLocation = "";
    int code = 0;
};

template<class Ref, class Val>
struct RetValT
{
    Ref r;
    Val v;
};

namespace std{
    template<class Ref, class Val>
    struct tuple_size<RetValT<Ref,Val>>: std::integral_constant<std::size_t, 2> {};

    template<class Ref, class Val>
    struct tuple_element<0, RetValT<Ref,Val>>
    {
        using type = Ref;
    };

    template<class Ref, class Val>
    struct tuple_element<1, RetValT<Ref,Val>>
    {
        using type = Val;
    };

    template<std::size_t idx, class Ref, class Val>
    auto& get(RetValT<Ref,Val> &rv)
    {
        if constexpr (idx == 0)
            return rv.r;
        else if constexpr (idx == 1)
            return rv.v;
    }

    template<std::size_t idx, class Ref, class Val>
    auto& get(RetValT<Ref,Val> const& rv)
    {
        if constexpr (idx == 0)
            return rv.r;
        else if constexpr (idx == 1)
            return rv.v;
    }
};

#define CALL_WITH_EXPECTED(location, f) \
    if (auto err = f; err != 0) \
        return std::unexpected(Err{location, err})

#define CALL_WITH_EXPECTED2(location, f) \
    if (auto err = f; err < 0) \
        return std::unexpected(Err{location, err})

#endif
