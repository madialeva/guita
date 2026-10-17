#include <backend/api/Backend.h>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <pthread.h>
#include <string>

namespace {
std::string env(const char *name, const std::string &fallback = {}) {
    const char *value = std::getenv(name);
    return value ? value : fallback;
}
}

// Environment variables:
//   DB_CONN       libpq connection string  ("host=127.0.0.1 dbname=guita user=guita password=guita")
//   HTTP_ADDRESS  listen address           (0.0.0.0)
//   HTTP_PORT     port                     (8090)
//   DOCROOT       static files (WASM build) (./web)
//   API_TOKEN     required Bearer token; empty = no authentication (development only)
//                 (development aid until user authentication exists)
int main() {
    // Block the termination signals before any thread starts, so that every
    // server thread inherits the mask and only sigwait() below receives them
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    const auto token = env("API_TOKEN");
    if (token.empty())
        std::cerr << "WARNING: API_TOKEN is empty, the API does not require authentication\n";

    try {
        Backend backend({
            .connectionString = env("DB_CONN", "host=127.0.0.1 dbname=guita user=guita password=guita"),
            .authorizer = tokenAuthorizer(token),
            .http = HttpOptions{
                .address = env("HTTP_ADDRESS", "0.0.0.0"),
                .port = std::stoi(env("HTTP_PORT", "8090")),
                .docroot = env("DOCROOT", "./web"),
                .accessLog = true,
            },
        });

        if (!backend.start()) {
            std::cerr << "Error: cannot listen on port " << env("HTTP_PORT", "8090") << '\n';
            return 1;
        }
        std::cerr << "Server listening on port " << backend.port() << '\n';

        int signal = 0;
        sigwait(&signals, &signal);
        std::cerr << "Stopping\n";
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
