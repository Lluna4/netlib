#include "netlib.h"

void netlib::add_to_list(int sockfd)
{
    struct kevent ev;
    EV_SET(&ev, sockfd, EVFILT_READ, EV_ADD, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}

void netlib::remove_from_list(int fd, int epfd)
{
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    kevent(epfd, &ev, 1, NULL, 0, NULL);
}

void netlib::recv_th()
{
    int events_ready = 0;
    struct kevent events[1024];
    int status = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 500000000; //500ms
    while (threads == true)
    {
        events_ready = kevent(epfd, NULL, 0, events, 1024, &timeout);
        if (events_ready == -1)
            std::println("Epoll error! {}", strerror(errno));
        for (int i = 0; i < events_ready; i++)
        {
            std::unique_lock<std::mutex> lk(mut);
            int current_fd = events[i].ident;
            auto &current_user = users.find(current_fd)->second;
            size_t available;
            ioctl(current_fd, FIONREAD, &available);
            if (available > current_user.data_alloc_size - current_user.data_size)
            {
                std::shared_ptr<char[]> newBuffer(new char[current_user.data_size + available + 1]);
                std::memcpy(newBuffer.get(), current_user.data.get(), current_user.data_size);
                current_user.data.reset();
                current_user.data = newBuffer;
                current_user.data_alloc_size = current_user.data_size + available + 1;
            }
            status = recv(current_fd, &(current_user.data.get())[current_user.data_size], available, 0);
            if (status == -1 || status == 0)
            {
                disconnect_user(current_fd);
                continue;
            }
            current_user.data_size += available;
            if (current_user.data_size >= current_user.default_size)
                current_user.readable = true;
            lk.unlock();
            cond.notify_all();
            std::println("Got {}B of data", available);
        }
    }
}

void netlib::accept_th()
{
    sockaddr_in addr = {0};
    unsigned int addr_size = sizeof(addr);
    char str[INET_ADDRSTRLEN];
    std::println("Listening for clients");
    while (threads == true)
    {
        int new_client = accept(fd, (sockaddr *)&addr, &addr_size);
        if (new_client < 0)
        {
            //std::println("Returned {}", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        std::println("Client accepted");
        netlib::add_to_list(new_client);
        struct in_addr ipAddr = addr.sin_addr;
        std::println("{} connected", inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN));
        users.emplace(std::piecewise_construct, std::forward_as_tuple(new_client), std::forward_as_tuple(new_client, default_packet_size));
    }
}

void netlib::disconnect_user(int current_fd)
{
    remove_from_list(fd, epfd);
    //std::println("Removed fd {} from epoll", fd);
    close(fd);
    //users.erase(current_fd);
}



void netlib::open_server(std::string address, short port)
{
    fd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(fd, F_SETFL, O_NONBLOCK);
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
    epfd = kqueue();
    accept_thread = std::thread([this]() { this->accept_th(); });
    recv_thread = std::thread([this]() { this->recv_th(); });
}
