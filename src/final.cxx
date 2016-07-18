#include "thread_safe_socket_queue.h"
#include <arpa/inet.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <netdb.h>
#include <regex>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace po = boost::program_options;

namespace {
const unsigned kNumberOfThreads = 4;
}

class HttpServer {
public:
    HttpServer(const std::string& directory, const std::string& address, const std::string& port, unsigned nthreads)
        : m_directory{ std::move(directory) }
        , m_address{ std::move(address) }
        , m_port{ std::move(port) }
    {
        for (auto i = 0u; i < nthreads; ++i) {
            workers.emplace_back([this] { handle_clients(); });
        }
    }
    ~HttpServer() {
        for (auto& worker: workers) {
            worker.join();
        }
    }

    void __attribute__((noreturn)) run();

private:
    addrinfo* get_hints() const;
    int bind_and_listen();
    void __attribute__((noreturn)) handle_clients();

    std::vector<std::thread> workers{};
    ThreadSafeSocketQueue socketQueue{};
    std::string m_directory;
    std::string m_address;
    std::string m_port;
};

addrinfo* HttpServer::get_hints() const
{
    struct addrinfo hints = {};
    struct addrinfo* servinfo;
    int rv;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(nullptr, m_port.c_str(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return nullptr;
    }
    return servinfo;
}

int HttpServer::bind_and_listen()
{
    int server_socket = 0;
    auto servinfo = get_hints();
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

void handle_client(int client_socket)
{
    const int BUFSIZE = 4096;
    char buf[BUFSIZE];
    if (recv(client_socket, buf, BUFSIZE, 0) == -1) {
        perror("recv");
    }
    auto buf_string = std::string(std::move(buf));
    auto newline = buf_string.find_first_of("\r\n");
    if (newline != std::string::npos) {
        buf_string = buf_string.substr(0, newline);
    }

    std::regex request{ "GET (.*) HTTP.*" };
    std::smatch sm;
    std::regex_match(buf_string, sm, request);

    // If not, we should send 400 Bad Request
    if (sm.size() > 1) {
        auto request_path = std::string{ sm[1] };

        /* TODO: lookup file and send its contents or 404 */
        if (send(client_socket, request_path.c_str(), request_path.length(), 0) == -1) {
            perror("send");
        }
    }
    close(client_socket);
}

void HttpServer::handle_clients()
{
    while (true) {
        auto client_socket = socketQueue.wait_and_pop();
        handle_client(client_socket);
    }
}

void HttpServer::run()
{
    auto server_socket = bind_and_listen();
    while (true) {
        auto client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }
        socketQueue.push(client_socket);
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

    HttpServer server{ directory, address, port, kNumberOfThreads };
    server.run();
}
