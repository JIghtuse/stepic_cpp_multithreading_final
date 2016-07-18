#include <arpa/inet.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

namespace po = boost::program_options;

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(reinterpret_cast<sockaddr_in*>(sa)->sin_addr);
    }
    return &(reinterpret_cast<sockaddr_in6*>(sa)->sin6_addr);
}

struct addrinfo *get_hints(const std::string& port)
{
    struct addrinfo hints = {};
    struct addrinfo *servinfo;
    int rv;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(nullptr, port.c_str(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return nullptr;
    }
    return servinfo;
}

int bind_and_listen(const std::string& port)
{
    int server_socket;
    auto servinfo = get_hints(port);
    auto p = servinfo;
    for (; p != nullptr; p = p->ai_next) {
        if ((server_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        const int enable = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) == -1) {
            perror("setsockopt");
            return 0;
        }

        if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_socket);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == nullptr) {
        fprintf(stderr, "server: failed to bind\n");
        return 0;
    }

    if (listen(server_socket, 10) == -1) {
        perror("listen");
        return 0;
    }
    return server_socket;
}

void launch_server(const std::string& directory, const std::string& address, const std::string& port)
{
    // struct sockaddr_info client_info;
    std::cerr << "Launching server in " << directory
              << " on " << address << ":" << port
              << std::endl;

    auto server_socket = bind_and_listen(port);

    struct sockaddr_storage client_address;
    char s[INET6_ADDRSTRLEN];
    socklen_t sin_size = sizeof(client_address);
    while (true) {
        auto client_socket = accept(server_socket, reinterpret_cast<struct sockaddr*>(&client_address), &sin_size);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }
        inet_ntop(client_address.ss_family, get_in_addr(reinterpret_cast<struct sockaddr*>(&client_address)), s, sizeof(s));
        std::cerr << "got connection from " << s << '\n';

        if (send(client_socket, "Hello, world!", 13, 0) == -1) {
            perror("send");
        }
        close(client_socket);
    }
}

int main(int argc, char** argv)
{
    po::options_description desc("Available options");
    desc.add_options()
        ("help", "produce help message")
        ("directory,d", po::value<std::string>(), "root directory of server")
        ("address,h", po::value<std::string>(), "address to listen on")
        ("port,p", po::value<std::string>(), "port number")
        ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    if (!vm.count("address") || !vm.count("directory") || !vm.count("port")) {
        std::cerr << "Missed arguments\n" << desc << "\n";
        return 1;
    }

    const auto directory = vm["directory"].as<std::string>();
    const auto address = vm["address"].as<std::string>();
    const auto port = vm["port"].as<std::string>();

    launch_server(directory, address, port);
}
