// Runs the system preprocessor for programs with directives the lexer does not handle (docs/architecture.md, "Tokens
// and preprocessing").

#include <array>
#include <cerrno>
#include <format>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "driver/frontend.hpp"

namespace nitwit::driver {

namespace {

/// Closes a file descriptor when it goes out of scope.
class Descriptor {
public:
    explicit Descriptor(int fd) : fd_(fd) {}
    ~Descriptor() { reset(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    [[nodiscard]] int get() const { return fd_; }
    void reset() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_;
};

} // namespace

std::expected<std::string, std::string> preprocess(const std::filesystem::path& file, DataModel model) {
    std::array<int, 2> pipe{};
    if (::pipe(pipe.data()) != 0) {
        return std::unexpected("cannot create a pipe for the preprocessor");
    }
    Descriptor readEnd(pipe[0]);
    Descriptor writeEnd(pipe[1]);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, writeEnd.get(), STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, readEnd.get());

    std::string program = "cpp";
    std::string bits = model == DataModel::ILP32 ? "-m32" : "-m64";
    std::string input = file.string();
    std::vector<char*> arguments = {program.data(), bits.data(), input.data(), nullptr};
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, program.c_str(), &actions, nullptr, arguments.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    writeEnd.reset();
    if (spawned != 0) {
        return std::unexpected(std::format("cannot run the preprocessor 'cpp': error {}", spawned));
    }

    std::string output;
    std::array<char, 65536> buffer{};
    while (true) {
        const ssize_t count = ::read(readEnd.get(), buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
        } else if (count == 0 || errno != EINTR) {
            break;
        }
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::unexpected(std::format("the preprocessor failed on {}", input));
    }
    return output;
}

} // namespace nitwit::driver
