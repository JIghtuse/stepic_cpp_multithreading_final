#include "http_server.h"
#include <boost/program_options.hpp>
#include <iostream>

namespace po = boost::program_options;

namespace {

const unsigned kNumberOfThreads = 4;

} // namespace

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
