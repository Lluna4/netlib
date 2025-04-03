#include "comp_time_write.h"

template <typename T>
void write_type(char *v, T value)
{
    switch (sizeof(T))
    {
        case 2:
        {
            uint16_t conv = htobe16((*(uint16_t *)&value));
            std::memcpy(v, &conv, sizeof(T));
            break;
        }
        case 4:
        {
            uint32_t conv = htobe32((*(uint32_t *)&value));
            std::memcpy(v, &conv, sizeof(T));
            break;
        }
        case 8:
        {
            uint64_t conv = htobe64((*(uint64_t *)&value));
            std::memcpy(v, &conv, sizeof(T));
            break;
        }
        default:
            std::memcpy(v, &value, sizeof(T));
    }
}

template <int size, typename T>
void write_array(char *v, T value)
{
    std::memcpy(v, value, size);
}


namespace netlib
{
    template<typename ...T>
    int send_packet(std::tuple<T...> packet, int sock)
    {
        char *buffer = (char *)malloc(1024 * sizeof(char));
        char *start_buffer = buffer;
        constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
        char_size buff = {buffer, 0, 1024, start_buffer};
        write_comp_pkt(size, buff, packet);

        int ret = send(sock, buff.start_data, buff.consumed_size, 0);
        std::println("Sent {}B", ret);
        free(buff.start_data);
        
        return ret;
    }
}

template<typename ...T>
int write_to_file(std::tuple<T...> packet, int fd)
{
    char *buffer = (char *)malloc(1024 * sizeof(char));
    constexpr std::size_t size = std::tuple_size_v<decltype(packet)>;
    char_size buff = {buffer, 0, 1024, buffer};
    write_comp_pkt(size, buff, packet);
    
    int ret = write(fd, buff.start_data, buff.consumed_size);
    std::println("Sent {}B", ret);
    free(buff.start_data);
    
    return ret;
}
