#include "netlib.h"

void user_raw::add_data(char *new_data, size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    if (!new_data || size == 0 || size > MAX_PACKET_SIZE)
        return;
    if (data_size + size > alloc_size)
    {
        data = (char *)realloc(data, size + data_size + 1);
        if (!data)
        {
            std::runtime_error(std::format("Realloc failed {}", strerror(errno)).c_str());
        }
        alloc_size = data_size + size + 1;
    }
    memcpy(&data[data_size], new_data, size);
    data_size += size;
}

void user_raw::remove_data(size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    if (size == 0)
        return;
    int new_data_size = data_size - size;
    if (new_data_size < 0)
        return;
    char *new_data = (char *)calloc(alloc_size, sizeof(char));
    if (!new_data)
        std::runtime_error(std::format("Calloc failed {}", strerror(errno)).c_str());
    memcpy(new_data, &data[size], new_data_size);
    free(data);
    data = new_data;
    data_size = new_data_size;
    if (data_size == 0)
        readable = false;
}

void netlib::server_raw::open_server(std::string address, short port)
{
    fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &(addr.sin_addr)) == -1)
    {
        std::println("Inet pton failed! {}", strerror(errno));
        close(fd);
        return ;
    }
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
    {
        std::println("Bind failed! {}", strerror(errno));
        close(fd);
        return ;
    }
    if (listen(fd, 10) == -1)
    {
        std::println("Listen failed!");
        close(fd);
        return ;
    }
    #if defined(__APPLE__) || defined(__FreeBSD__)
    epfd = kqueue();
    #elif defined(__linux__)
    epfd = epoll_create1(0);
    #endif
    add_to_list(fd);
    recv_thread = std::thread([this]() { this->recv_th(); });
}

void netlib::server_raw::disconnect_user(int current_fd)
{
    remove_from_list(current_fd);
    std::println("Removed fd {} from epoll", current_fd);
    close(current_fd);
    users.erase(current_fd);
    readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
}

char *netlib::server_raw::receive_data(int current_fd, size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    auto current_user_test = users.find(current_fd);
    if (current_user_test == users.end())
        return nullptr;
    auto &current_user = current_user_test->second;
    if (size == current_user.data_size)
        readable.erase(std::remove(readable.begin(), readable.end(), current_fd), readable.end());
    return current_user.receive_data(size);
}

std::vector<int> netlib::server_raw::get_readable()
{
    std::lock_guard<std::mutex> lock(sync);
    return readable;
}

#if defined(__APPLE__) || defined(__FreeBSD__)
void netlib::server_raw::add_to_list(int sockfd)
{
    struct kevent ev;
    EV_SET(&ev, sockfd, EVFILT_READ, EV_ADD, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}

void netlib::server_raw::remove_from_list(int fd)
{
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}
#elif defined(__linux__)
void netlib::server_raw::add_to_list(int sockfd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

void netlib::server_raw::remove_from_list(int fd)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}
#endif

void netlib::server_raw::recv_th()
{
    int events_ready = 0;
    #if defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent events[1024];
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 500000000; //500ms
    #elif defined(__linux__)
    epoll_event events[1024];
    #endif
    int status = 0;
    char *buffer = (char *)calloc(1024, sizeof(char));
    while (threads == true)
    {
        #if defined(__APPLE__) || defined(__FreeBSD__)
        events_ready = kevent(epfd, NULL, 0, events, 1024, &timeout);
        #elif defined(__linux__)
        events_ready = epoll_wait(epfd, events, 1024, -1);
        #endif
        if (events_ready == -1)
        {
            std::println("Epoll/kqueue failed {}", strerror(errno));
            break;
        }
        for (int i = 0; i < events_ready; i++)
        {
            int current_fd = events[i].ident;
            if (current_fd == fd)
            {
                sockaddr_in addr = {0};
                unsigned int addr_size = sizeof(addr);
                char str[INET_ADDRSTRLEN];
                int new_client = accept(fd, (sockaddr *)&addr, &addr_size);
                std::println("Client accepted");
                add_to_list(new_client);
                struct in_addr ipAddr = addr.sin_addr;
                std::println("{} connected", inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN));
                std::println("New fd {}", new_client);
                users.emplace(std::piecewise_construct, std::forward_as_tuple(new_client), std::forward_as_tuple(new_client));
                continue;
            }
            auto current_user_prov = users.find(current_fd);
            if (current_user_prov == users.end())
            {
                remove_from_list(current_fd);
                continue;
            }
            auto &current_user = current_user_prov->second;
            status = recv(current_fd, buffer, 1024, 0);
            if (status == -1 || status == 0)
            {
                std::lock_guard<std::mutex> lock(sync);
                disconnect_user(current_fd);
                continue;
            }
            current_user.add_data(buffer, status);
            std::lock_guard<std::mutex> lock(sync);
            if (std::find(readable.begin(), readable.end(), current_fd) == readable.end())
            {
                current_user.readable = true;
                readable.push_back(current_fd);
            }
        }
    }
}

char *user_raw::receive_data(size_t size)
{
    if (readable == true)
    {
        if (size > data_size)
        {
            size = data_size;
        }    
        char *ret = (char *)calloc(size, sizeof(char));
        memcpy(ret, data, size);
        remove_data(size);
        return ret;
    }
    return nullptr;
}


void netlib::cli_raw::add_data(char *new_data, size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    if (!new_data || size == 0 || size > MAX_PACKET_SIZE)
        return;
    if (data_size + size > alloc_size)
    {
        data = (char *)realloc(data, size + data_size + 1);
        if (!data)
        {
            std::runtime_error(std::format("Realloc failed {}", strerror(errno)).c_str());
        }
        alloc_size = data_size + size + 1;
    }
    memcpy(&data[data_size], new_data, size);
    data_size += size;
}

void netlib::cli_raw::remove_data(size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    if (size == 0)
        return;
    int new_data_size = data_size - size;
    if (new_data_size < 0)
        return;
    char *new_data = (char *)calloc(alloc_size, sizeof(char));
    if (!new_data)
        std::runtime_error(std::format("Calloc failed {}", strerror(errno)).c_str());
    memcpy(new_data, &data[size], new_data_size);
    free(data);
    data = new_data;
    data_size = new_data_size;
    if (data_size == 0)
        readable = false;
}

char *netlib::cli_raw::receive_data(size_t size)
{
    if (readable == true)
    {
        if (size > data_size)
        {
            size = data_size;
        }    
        char *ret = (char *)calloc(size, sizeof(char));
        memcpy(ret, data, size);
        remove_data(size);
        return ret;
    }
    return nullptr;
}

void netlib::client_raw::connect_to_server(std::string address, short port)
{
    fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address.c_str(), &(addr.sin_addr)) == -1)
    {
        std::println("Inet pton failed! {}", strerror(errno));
        close(fd);
        return ;
    }
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
    {
        std::println("Connect failed!");
        return ;
    }
    #if defined(__APPLE__) || defined(__FreeBSD__)
    epfd = kqueue();
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
    #elif defined(__linux__)
    epfd = epoll_create1(0);
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    #endif
    recv_thread = std::thread([this]() { this->recv_th(); });
}

void netlib::client_raw::disconnect_from_server()
{
    close(fd);
}

char *netlib::client_raw::receive_data(int current_fd, size_t size)
{
    std::lock_guard<std::mutex> lock(sync);
    auto &current_user = serv;
    if (size == current_user.data_size)
    {
        readable = false;
        serv.readable = false;
    }
    return current_user.receive_data(size);
}

void netlib::client_raw::recv_th()
{
    int events_ready = 0;
    #if defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent events[1024];
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 500000000; //500ms
    #elif defined(__linux__)
    epoll_event events[1024];
    #endif
    int status = 0;
    char *buffer = (char *)calloc(1024, sizeof(char));
    while (threads == true)
    {
        #if defined(__APPLE__) || defined(__FreeBSD__)
        events_ready = kevent(epfd, NULL, 0, events, 1024, &timeout);
        #elif defined(__linux__)
        events_ready = epoll_wait(epfd, events, 1024, -1);
        #endif
        if (events_ready == -1)
        {
            std::println("Epoll/kqueue failed {}", strerror(errno));
            break;
        }
        for (int i = 0; i < events_ready; i++)
        {
            int current_fd = events[i].ident;
            status = recv(current_fd, buffer, 1024, 0);
            if (status == -1 || status == 0)
            {
                std::lock_guard<std::mutex> lock(sync);
                disconnect_from_server();
                continue;
            }
            serv.add_data(buffer, status);
            std::lock_guard<std::mutex> lock(sync);
            readable = true;
            serv.readable = true;
        }
    }
}
