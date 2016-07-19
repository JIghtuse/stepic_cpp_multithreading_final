#include "http_server.h"
#include <arpa/inet.h>
#include <boost/algorithm/string/predicate.hpp>
#include <fcntl.h>
#include <linux/tcp.h>
#include <netdb.h>
#include <regex>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char kPage404[] = 
R"(HTTP/1.1 404 Not Found
Server: stepic_final
Content-Length: 56
Connection: close

<html>
<body><h2>404 - No such page</h2></body>
</html>)";

const char kPage200Headers[] =
R"(HTTP/1.1 200 OK
Server: stepic_final
Connection: close
)";

} // namespace

HttpServer::HttpServer(const std::string& directory, const std::string& address, const std::string& port, unsigned nthreads)
    : m_address{ std::move(address) }
    , m_port{ std::move(port) }
{
    if (0 != chroot(directory.c_str())) {
        perror("chroot");
        throw std::runtime_error("cannot chroot to specified directory");
    }
    if (0 == daemon(0, 0)) {
        perror("daemon");
        throw std::runtime_error("cannot daemonize process");
    }
    for (auto i = 0u; i < nthreads; ++i) {
        workers.emplace_back([this] { handle_clients(); });
    }
}
HttpServer::~HttpServer() {
    for (auto& worker: workers) {
        worker.join();
    }
}

addrinfo* get_hints(const char* port)
{
    struct addrinfo hints = {};
    struct addrinfo* servinfo;
    int rv;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(nullptr, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return nullptr;
    }
    return servinfo;
}

int bind_and_listen(const char* port)
{
    int server_socket = 0;
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

std::string extractRequestPath(std::string&& buf)
{
    auto newline = buf.find_first_of("\r\n");
    if (newline != std::string::npos) {
        buf = buf.substr(0, newline);
    }

    std::regex request{ "GET (.*) HTTP.*" };
    std::smatch sm;
    std::regex_match(buf, sm, request);

    if (sm.size() > 1) {
        return std::string{ sm[1] };
    } else {
        return "";
    }
}

bool sendData(int client_socket, const char *data, size_t length)
{
    if (send(client_socket, data, length, 0) == -1) {
        perror("send");
        return false;
    }
    return true;
}

void handle_client(int client_socket)
{
    const int BUFSIZE = 4096;
    char buf[BUFSIZE];
    if (recv(client_socket, buf, BUFSIZE, 0) == -1) {
        perror("recv");
    }
    auto request = extractRequestPath(std::string(buf));

    // If not, we should send 400 Bad Request
    if (!request.empty()) {
        struct stat st;
        auto fd = open(request.c_str(), O_RDONLY);
        if (fd == -1) {
            sendData(client_socket, kPage404, sizeof(kPage404));
        } else if (fstat(fd, &st) != 0) {
            perror("fstat");
        } else {
            const char *mime = "text/plain";
            if (boost::algorithm::ends_with(request, ".html")) {
                mime = "text/html";
            }

            int enable = 1;
            if (setsockopt(client_socket, IPPROTO_TCP, TCP_CORK, &enable, sizeof(int)) == -1) {
                perror("setsockopt");
            }
            auto kPage200 = std::string{kPage200Headers};
            kPage200 += "Content-type: ";
            kPage200 += mime;
            kPage200 += "\nContent-Length: " + std::to_string(st.st_size);
            kPage200 += "\n\n";

            sendData(client_socket, kPage200.c_str(), kPage200.length());
            sendfile(client_socket, fd, 0, static_cast<size_t>(st.st_size));

            enable = 0;
            if (setsockopt(client_socket, IPPROTO_TCP, TCP_CORK, &enable, sizeof(int)) == -1) {
                perror("setsockopt");
            }
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
    auto server_socket = bind_and_listen(m_port.c_str());
    while (true) {
        auto client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1) {
            perror("accept");
            continue;
        }
        socketQueue.push(client_socket);
    }
}

