#include <boost/program_options.hpp>
#include <iostream>
#include <string>

namespace po = boost::program_options;

void launch_server(const std::string& directory, const std::string& address, const size_t port)
{
    std::cerr << "Launching server in " << directory
              << " on " << address << ":" << port
              << std::endl;
}

int main(int argc, char** argv)
{
    po::options_description desc("Available options");
    desc.add_options()("help", "produce help message")
        ("directory", po::value<std::string>(), "root directory of server")
        ("address", po::value<std::string>(), "address to listen on")
        ("port", po::value<size_t>(), "port number")
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
    const auto port = vm["port"].as<size_t>();

    launch_server(directory, address, port);
}
