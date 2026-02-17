#include <catch2/catch_test_macros.hpp>

#include "ipc_client.hpp"
#include "ipc_server.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;

namespace {

std::string tmp_socket_path() {
    return "/tmp/sa_test_ipc_" + std::to_string(getpid()) + ".sock";
}

} // namespace

TEST_CASE("IPC protocol", "[ipc]") {
    auto sock_path = tmp_socket_path();

    SECTION("ServerStartStop") {
        {
            IpcServer server;
            REQUIRE(server.start(sock_path));
            REQUIRE(std::filesystem::exists(sock_path));
            server.stop();
            REQUIRE_FALSE(std::filesystem::exists(sock_path));
        }
    }

    SECTION("ClientConnects") {
        IpcServer server;
        REQUIRE(server.start(sock_path));

        IpcClient client;
        REQUIRE(client.connect(sock_path));

        int client_fd = server.accept_client();
        REQUIRE(client_fd >= 0);

        server.close_client(client_fd);
        client.close();
        server.stop();
    }

    SECTION("RoundTrip") {
        IpcServer server;
        REQUIRE(server.start(sock_path));

        IpcClient client;
        REQUIRE(client.connect(sock_path));

        int client_fd = server.accept_client();
        REQUIRE(client_fd >= 0);

        // Client sends command
        json cmd = {{"command", "status"}};
        REQUIRE(client.send(cmd));

        // Server reads — may need a brief pause for data to arrive
        // Server socket is non-blocking, so poll briefly
        json received;
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            got = server.read_command(client_fd, received);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(got);
        REQUIRE(received["command"] == "status");

        // Server sends response
        json resp = {{"status", "idle"}};
        REQUIRE(server.send_response(client_fd, resp));

        // Client reads response
        json client_resp;
        REQUIRE(client.recv(client_resp, 1000));
        REQUIRE(client_resp["status"] == "idle");

        server.close_client(client_fd);
        client.close();
        server.stop();
    }

    SECTION("MultipleMessages") {
        IpcServer server;
        REQUIRE(server.start(sock_path));

        IpcClient client;
        REQUIRE(client.connect(sock_path));

        int client_fd = server.accept_client();
        REQUIRE(client_fd >= 0);

        for (int i = 0; i < 5; ++i) {
            json cmd = {{"command", "ping"}, {"seq", i}};
            REQUIRE(client.send(cmd));

            json received;
            bool got = false;
            for (int j = 0; j < 50 && !got; ++j) {
                got = server.read_command(client_fd, received);
                if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            REQUIRE(got);
            REQUIRE(received["seq"] == i);

            json resp = {{"ok", true}, {"seq", i}};
            REQUIRE(server.send_response(client_fd, resp));

            json client_resp;
            REQUIRE(client.recv(client_resp, 1000));
            REQUIRE(client_resp["seq"] == i);
        }

        server.close_client(client_fd);
        client.close();
        server.stop();
    }

    SECTION("ClientDisconnect") {
        IpcServer server;
        REQUIRE(server.start(sock_path));

        IpcClient client;
        REQUIRE(client.connect(sock_path));

        int client_fd = server.accept_client();
        REQUIRE(client_fd >= 0);

        // Client closes connection
        client.close();

        // Server should detect disconnect (read returns false)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        json cmd;
        REQUIRE_FALSE(server.read_command(client_fd, cmd));

        server.close_client(client_fd);
        server.stop();
    }
}
