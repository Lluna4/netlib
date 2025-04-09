#include "comp_time_write.h"



template <int size, typename T>
void write_array(char *v, T value)
{
    std::memcpy(v, value, size);
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
