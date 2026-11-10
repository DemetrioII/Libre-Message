#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct Proc {
    pid_t pid{-1};
    int stdin_fd{-1};
    std::filesystem::path out;
};

std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

std::string read_id(const std::filesystem::path &dir) {
    std::ifstream in(dir / "user_id", std::ios::binary);
    std::string id;
    std::getline(in, id);
    return id;
}

bool is_valid_user_id(const std::string &id) {
    if (id.size() != 16) {
        return false;
    }
    for (char ch : id) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string shell_quote(const std::filesystem::path &path) {
    std::string value = path.string();
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string find_application() {
    const char *srcdir = std::getenv("TEST_SRCDIR");
    const char *workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir != nullptr) {
        std::vector<std::string> candidates;
        if (workspace != nullptr) {
            candidates.emplace_back(workspace);
        }
        candidates.emplace_back("_main");
        candidates.emplace_back("libremessage");
        candidates.emplace_back("i_try_messenger2");
        for (const auto &name : candidates) {
            std::filesystem::path path =
                std::filesystem::path(srcdir) / name / "application";
            if (std::filesystem::exists(path)) {
                return path.string();
            }
        }

        std::error_code ec;
        auto options = std::filesystem::directory_options::follow_directory_symlink;
        for (auto it = std::filesystem::recursive_directory_iterator(srcdir, options, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->path().filename() == "application" &&
                std::filesystem::is_regular_file(it->path())) {
                return it->path().string();
            }
        }
    }

    for (const char *candidate : {"bazel-bin/application",
                                  "../bazel-bin/application"}) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate).string();
        }
    }
    return {};
}

std::pair<uint16_t, uint16_t> reserve_two_ports() {
    auto reserve_one = []() -> std::pair<int, uint16_t> {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return {-1, 0};
        }
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return {-1, 0};
        }
        socklen_t len = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
            ::close(fd);
            return {-1, 0};
        }
        return {fd, ntohs(addr.sin_port)};
    };

    auto [fd_a, port_a] = reserve_one();
    auto [fd_b, port_b] = reserve_one();
    if (fd_a >= 0) {
        ::close(fd_a);
    }
    if (fd_b >= 0) {
        ::close(fd_b);
    }
    return {port_a, port_b};
}

bool write_all(int fd, const std::string &data) {
    std::size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

class E2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        app_ = find_application();
        ASSERT_FALSE(app_.empty()) << "application binary was not found in runfiles";

        work_ = std::filesystem::temp_directory_path() /
                ("libremessage_e2e_" + std::to_string(::getpid()));
        std::filesystem::remove_all(work_);
        std::filesystem::create_directories(work_ / "a");
        std::filesystem::create_directories(work_ / "b");
        std::filesystem::create_directories(work_ / "c");
        ASSERT_TRUE(make_certs()) << "failed to generate TLS certificates";
    }

    void TearDown() override {
        stop(a_);
        stop(b_);
        stop(c_);
        std::error_code ec;
        std::filesystem::remove_all(work_, ec);
    }

    bool make_certs() {
        const auto crt = work_ / "a" / "server.crt";
        const auto key = work_ / "a" / "server.key";
        std::string command =
            "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
            "-subj /CN=localhost -keyout " +
            shell_quote(key) + " -out " + shell_quote(crt) +
            " >/dev/null 2>&1";
        if (std::system(command.c_str()) != 0) {
            return false;
        }
        for (const char *name : {"b", "c"}) {
            std::filesystem::copy_file(crt, work_ / name / "server.crt");
            std::filesystem::copy_file(key, work_ / name / "server.key");
        }
        return true;
    }

    Proc start(const std::filesystem::path &dir, uint16_t port) {
        Proc proc;
        proc.out = dir / "out.log";

        int in_pipe[2];
        if (::pipe(in_pipe) != 0) {
            return proc;
        }

        pid_t pid = ::fork();
        if (pid == 0) {
            ::close(in_pipe[1]);
            ::dup2(in_pipe[0], STDIN_FILENO);
            ::close(in_pipe[0]);

            int out = ::open(proc.out.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (out >= 0) {
                ::dup2(out, STDOUT_FILENO);
                ::dup2(out, STDERR_FILENO);
                ::close(out);
            }
            if (::chdir(dir.c_str()) != 0) {
                ::_exit(127);
            }
            ::execl(app_.c_str(), app_.c_str(), static_cast<char *>(nullptr));
            ::_exit(127);
        }

        ::close(in_pipe[0]);
        proc.pid = pid;
        proc.stdin_fd = in_pipe[1];
        write_all(proc.stdin_fd, std::to_string(port) + "\n");
        return proc;
    }

    void send_cmd(Proc &proc, const std::string &line) {
        ASSERT_GE(proc.stdin_fd, 0);
        ASSERT_TRUE(write_all(proc.stdin_fd, line + "\n"));
    }

    bool wait_for(const Proc &proc, const std::string &needle, int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (read_file(proc.out).find(needle) != std::string::npos) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    bool wait_for_regex(const Proc &proc, const std::string &pattern,
                        int timeout_ms) {
        const std::regex re(pattern);
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::regex_search(read_file(proc.out), re)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    void stop(Proc &proc) {
        if (proc.stdin_fd >= 0) {
            ::close(proc.stdin_fd);
            proc.stdin_fd = -1;
        }
        if (proc.pid <= 0) {
            return;
        }
        int status = 0;
        for (int attempt = 0; attempt < 40; ++attempt) {
            pid_t result = ::waitpid(proc.pid, &status, WNOHANG);
            if (result == proc.pid) {
                proc.pid = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ::kill(proc.pid, SIGKILL);
        ::waitpid(proc.pid, &status, 0);
        proc.pid = -1;
    }

    std::string app_;
    std::filesystem::path work_;
    Proc a_;
    Proc b_;
    Proc c_;
};

TEST_F(E2ETest, TwoPeersExchangeMessages) {
    auto [port_a, port_b] = reserve_two_ports();
    ASSERT_NE(port_a, 0);
    ASSERT_NE(port_b, 0);

    a_ = start(work_ / "a", port_a);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_a), 5000))
        << read_file(a_.out);

    b_ = start(work_ / "b", port_b);
    ASSERT_GT(b_.pid, 0);
    ASSERT_TRUE(wait_for(b_, "Listening on 127.0.0.1:" + std::to_string(port_b), 5000))
        << read_file(b_.out);

    const std::string id_a = read_id(work_ / "a");
    const std::string id_b = read_id(work_ / "b");
    ASSERT_EQ(id_a.size(), 16u) << "unexpected user id: '" << id_a << "'";
    ASSERT_EQ(id_b.size(), 16u) << "unexpected user id: '" << id_b << "'";
    ASSERT_NE(id_a, id_b) << "separate working directories must produce distinct ids";

    send_cmd(b_, "connect " + std::to_string(port_a));
    ASSERT_TRUE(wait_for(a_, "registered peer with id=" + id_b, 8000))
        << "A log:\n" << read_file(a_.out) << "\nB log:\n" << read_file(b_.out);
    ASSERT_TRUE(wait_for(b_, "registered peer with id=" + id_a, 8000))
        << "A log:\n" << read_file(a_.out) << "\nB log:\n" << read_file(b_.out);

    send_cmd(a_, "send " + id_b + " hello_from_a");
    ASSERT_TRUE(wait_for(b_, "message from " + id_a + " for me: hello_from_a", 5000))
        << read_file(b_.out);

    send_cmd(b_, "send " + id_a + " hello_from_b");
    ASSERT_TRUE(wait_for(a_, "message from " + id_b + " for me: hello_from_b", 5000))
        << read_file(a_.out);
}

TEST_F(E2ETest, PeerCanReconnectAfterDisconnect) {
    auto [port_a, port_b] = reserve_two_ports();
    ASSERT_NE(port_a, 0);
    ASSERT_NE(port_b, 0);

    a_ = start(work_ / "a", port_a);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_a), 5000))
        << read_file(a_.out);

    b_ = start(work_ / "b", port_b);
    ASSERT_GT(b_.pid, 0);
    ASSERT_TRUE(wait_for(b_, "Listening on 127.0.0.1:" + std::to_string(port_b), 5000))
        << read_file(b_.out);

    const std::string id_a = read_id(work_ / "a");
    const std::string id_b = read_id(work_ / "b");
    ASSERT_EQ(id_a.size(), 16u);
    ASSERT_EQ(id_b.size(), 16u);

    send_cmd(b_, "connect " + std::to_string(port_a));
    ASSERT_TRUE(wait_for(a_, "registered peer with id=" + id_b, 8000))
        << "A log:\n" << read_file(a_.out) << "\nB log:\n" << read_file(b_.out);

    stop(b_);
    ASSERT_TRUE(wait_for(a_, "removed recipient mapping for connection_id=",
                         8000)) << "A log:\n"
                                << read_file(a_.out);

    const auto port_c = reserve_two_ports().first;
    ASSERT_NE(port_c, 0);
    c_ = start(work_ / "c", port_c);
    ASSERT_GT(c_.pid, 0);
    ASSERT_TRUE(wait_for(c_, "Listening on 127.0.0.1:" + std::to_string(port_c), 5000))
        << read_file(c_.out);

    const std::string id_c = read_id(work_ / "c");
    ASSERT_EQ(id_c.size(), 16u);
    ASSERT_NE(id_c, id_a);

    send_cmd(c_, "connect " + std::to_string(port_a));
    ASSERT_TRUE(wait_for(a_, "registered peer with id=" + id_c, 8000))
        << "A log:\n" << read_file(a_.out) << "\nC log:\n" << read_file(c_.out);

    send_cmd(a_, "send " + id_c + " hello_to_c");
    ASSERT_TRUE(wait_for(c_, "message from " + id_a + " for me: hello_to_c", 5000))
        << read_file(c_.out);
}

TEST_F(E2ETest, StaleRecipientMappingIsRemovedOnDisconnect) {
    auto [port_a, port_b] = reserve_two_ports();
    ASSERT_NE(port_a, 0);
    ASSERT_NE(port_b, 0);

    a_ = start(work_ / "a", port_a);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_a), 5000))
        << read_file(a_.out);

    b_ = start(work_ / "b", port_b);
    ASSERT_GT(b_.pid, 0);
    ASSERT_TRUE(wait_for(b_, "Listening on 127.0.0.1:" + std::to_string(port_b), 5000))
        << read_file(b_.out);

    const std::string id_a = read_id(work_ / "a");
    const std::string id_b = read_id(work_ / "b");
    ASSERT_TRUE(is_valid_user_id(id_a));
    ASSERT_TRUE(is_valid_user_id(id_b));

    send_cmd(b_, "connect " + std::to_string(port_a));
    ASSERT_TRUE(wait_for(a_, "registered peer with id=" + id_b, 8000))
        << "A log:\n" << read_file(a_.out) << "\nB log:\n" << read_file(b_.out);

    stop(b_);
    ASSERT_TRUE(wait_for(a_, "removed recipient mapping for connection_id=",
                         8000)) << "A log:\n"
                                << read_file(a_.out);

    const auto port_c = reserve_two_ports().first;
    ASSERT_NE(port_c, 0);
    c_ = start(work_ / "c", port_c);
    ASSERT_GT(c_.pid, 0);
    ASSERT_TRUE(wait_for(c_, "Listening on 127.0.0.1:" + std::to_string(port_c), 5000))
        << read_file(c_.out);

    const std::string id_c = read_id(work_ / "c");
    ASSERT_TRUE(is_valid_user_id(id_c));
    ASSERT_NE(id_c, id_a);
    ASSERT_NE(id_c, id_b);

    send_cmd(c_, "connect " + std::to_string(port_a));
    ASSERT_TRUE(wait_for(a_, "registered peer with id=" + id_c, 8000))
        << "A log:\n" << read_file(a_.out) << "\nC log:\n" << read_file(c_.out);

    send_cmd(a_, "send " + id_b + " stale_message");
    ASSERT_TRUE(wait_for(a_, "no connection registered for recipient", 5000))
        << "A log:\n" << read_file(a_.out);

    EXPECT_EQ(read_file(c_.out).find("stale_message"), std::string::npos);

    send_cmd(a_, "send " + id_c + " hello_to_c");
    ASSERT_TRUE(wait_for(c_, "message from " + id_a + " for me: hello_to_c", 5000))
        << read_file(c_.out);
}

TEST_F(E2ETest, UserIdPersistsAcrossRestarts) {
    auto [port_a, port_b] = reserve_two_ports();
    ASSERT_NE(port_a, 0);
    ASSERT_NE(port_b, 0);

    a_ = start(work_ / "a", port_a);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_a), 5000))
        << read_file(a_.out);

    const std::string first_id = read_id(work_ / "a");
    ASSERT_TRUE(is_valid_user_id(first_id)) << "unexpected user id: '" << first_id << "'";

    stop(a_);

    a_ = start(work_ / "a", port_b);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_b), 5000))
        << read_file(a_.out);

    const std::string second_id = read_id(work_ / "a");
    EXPECT_EQ(second_id, first_id);
}

TEST_F(E2ETest, InvalidUserIdIsRegenerated) {
    const auto id_path = work_ / "a" / "user_id";
    {
        std::ofstream out(id_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "zzzzzzzzzzzzzzzz";
    }

    auto [port_a, port_b] = reserve_two_ports();
    ASSERT_NE(port_a, 0);

    a_ = start(work_ / "a", port_a);
    ASSERT_GT(a_.pid, 0);
    ASSERT_TRUE(wait_for(a_, "Listening on 127.0.0.1:" + std::to_string(port_a), 5000))
        << read_file(a_.out);

    const std::string regenerated = read_id(work_ / "a");
    ASSERT_TRUE(is_valid_user_id(regenerated))
        << "expected a valid regenerated id, got: '" << regenerated << "'";
    EXPECT_NE(regenerated, "zzzzzzzzzzzzzzzz");
}

}  // namespace
