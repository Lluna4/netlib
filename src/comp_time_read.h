#include <concepts>
#include <cstring>
#include <tuple>
#include <stdlib.h>
#include "utils.h"
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif
#define read_comp_pkt(size, ptr, t) const_for_<size>([&](auto i){std::get<i.value>(t) = read_var<std::tuple_element_t<i.value, decltype(t)>>::call(&ptr);});

template <typename T>
T read_type(char *v)
{
    T a;

    std::memcpy(&a, v, sizeof(T));
    switch (sizeof(T))
    {
        case 1:
            break;
        case 2:
            a = be16toh(a);
            break;
        case 4:
            a = be32toh(a);
            break;
        case 8:
            a = be64toh(a);
            break;
    }
    return a;
}
template<>
float read_type<float>(char *v);

template<>
double read_type<double>(char *v);

template<typename T>
struct read_var
{
    static T call(char_size* v)
    {
        if (sizeof(T) + v->consumed_size > v->max_size)
            return T{};
        T ret = read_type<T>(v->data);
        v->data += sizeof(T);
        v->consumed_size += sizeof(T);
        return ret;
    }
};


template<typename ...T>
struct read_var<std::tuple<T...>>
{
    static std::tuple<T...> call(char_size *v)
    {
        std::tuple<T...> ret;
        constexpr std::size_t size = std::tuple_size_v<decltype(ret)>;
        if (size + v->consumed_size > v->max_size)
            return std::tuple<T...>{};
        char *diff = v->data;
        ret = read_comp_pkt(size, diff, ret);
        int size_diff =(diff - v->data); 
        v->consumed_size += size_diff;
        v->data += size_diff;
        return ret;
    }
};

template <typename Integer, Integer ...I, typename F> constexpr void const_for_each_(std::integer_sequence<Integer, I...>, F&& func)
{
    (func(std::integral_constant<Integer, I>{}), ...);
}

template <auto N, typename F> constexpr void const_for_(F&& func)
{
    if constexpr (N > 0)
        const_for_each_(std::make_integer_sequence<decltype(N), N>{}, std::forward<F>(func));
}

template<typename ...T>
std::tuple<T...> read_packet(std::tuple<T...> packet, struct packet pkt);