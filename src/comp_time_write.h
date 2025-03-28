#pragma once
#include <sys/socket.h>
#include <concepts>
#include <cstring>
#include <tuple>
#include <bitset>
#include <unistd.h>
#include <print>
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

#define write_comp_pkt(size, ptr, t) const_for<size>([&](auto i){write_var<std::tuple_element_t<i.value, decltype(t)>>::call(&ptr, std::get<i.value>(t));});

template <typename Integer, Integer ...I, typename F> constexpr void const_for_each(std::integer_sequence<Integer, I...>, F&& func)
{
    (func(std::integral_constant<Integer, I>{}), ...);
}

template <auto N, typename F> constexpr void const_for(F&& func)
{
    if constexpr (N > 0)
        const_for_each(std::make_integer_sequence<decltype(N), N>{}, std::forward<F>(func));
} 

template <typename T>
void write_type(char *v, T value);

template <int size, typename T>
void write_array(char *v, T value);

template<typename T>
concept arithmetic = std::integral<T> or std::floating_point<T>;

template<typename T>
concept IsChar = std::same_as<T, char *>;

template<typename T>
concept IsPointer = std::is_pointer_v<T>;

template<typename T>
struct write_var
{
    static void call(char_size *v, T value) requires (arithmetic<T>)
    {
        if (v->consumed_size >= v->max_size || v->consumed_size + sizeof(T) >= v->max_size)
        {
            v->start_data = (char *)realloc(v->start_data, v->max_size + 1024);
            v->max_size += 1024;
            v->data = v->start_data + v->consumed_size;
        }
        write_type<T>(v->data, value);
        v->data += sizeof(T);
        v->consumed_size += sizeof(T);
    }
};

template<typename ...T>
struct write_var<std::tuple<T...>>
{
    static void call(char_size *v, std::tuple<T...> value)
    {
        constexpr std::size_t size = std::tuple_size_v<decltype(value)>;
        write_comp_pkt(size, *v, value);
    }
};

template<>
struct write_var<std::string>
{
    static void call(char_size *v, std::string value)
    {
        while (v->consumed_size >= v->max_size || v->consumed_size + value.size() >= v->max_size)
        {
            v->start_data = (char *)realloc(v->start_data, v->max_size + 1024);
            v->max_size += 1024;
            v->data = v->start_data + v->consumed_size;
        }
        memcpy(v->data, value.c_str(), value.size());
        v->data += value.size();
        v->consumed_size += value.size();
    }
};


template<>
struct write_var<char_size>
{
    static void call(char_size *v, char_size value)
    {
        while (v->consumed_size >= v->max_size || v->consumed_size + value.consumed_size >= v->max_size)
        {
            v->start_data = (char *)realloc(v->start_data, v->max_size + 1024);
            v->max_size += 1024;
            v->data = v->start_data + v->consumed_size;
        }
        std::memcpy(v->data, value.data, value.consumed_size);
        v->data += value.consumed_size;
        v->consumed_size += value.consumed_size;
    }
};

template<typename T>
struct write_var<std::vector<T, std::allocator<T>>>
{
    static void call(char_size *v, std::vector<T, std::allocator<T>> value)
    {
        for (auto val : value)
        {
            std::tuple<T> va = val;
            constexpr std::size_t size = std::tuple_size_v<decltype(va)>;
            write_comp_pkt(size, *v, va);
        }
    }
};

template<typename ...T>
int send_packet(std::tuple<T...> packet, int sock);

template<typename ...T>
int write_to_file(std::tuple<T...> packet, int fd);