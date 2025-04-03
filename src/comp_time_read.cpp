#include "comp_time_read.h"


template<>
float read_type<float>(char *v)
{
    return read_float(v);
}

template<>
double read_type<double>(char *v)
{
    return read_double(v);
}

namespace netlib
{
    template<typename ...T>
    std::tuple<T...> read_packet(std::tuple<T...> packet, struct packet pkt)
    {
        char_size buff = {.data = pkt.data, .consumed_size = 0, .max_size = (int)pkt.size, .start_data = pkt.data};
        constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
        read_comp_pkt(size, buff, packet);
        return packet;
    }
}