#include "process.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace platform {
ProcessResult runProcess(const std::string &executable, const std::vector<std::string> &args,
                         const std::string &directory, const std::string &libraryDir,
                         const Cancel &cancel)
{
    ProcessResult result;
    if (cancel && cancel()) {
        result.error = "cancelled";
        return result;
    }

    // Prepare the argument and environment buffers before fork. The child
    // only calls async-signal-safe operations before replacing its image.
    std::vector<char *> argv{const_cast<char *>(executable.c_str())};
    for (const auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    auto libraries = "LD_LIBRARY_PATH=" + libraryDir;
    if (const char *prior = getenv("LD_LIBRARY_PATH"); prior && *prior)
        libraries += ":" + std::string(prior);
    char locale[] = "LC_ALL=C";
    std::vector<char *> env;
    for (char **entry = environ; *entry; ++entry)
        if (strncmp(*entry, "LD_LIBRARY_PATH=", 16) != 0 && strncmp(*entry, "LC_ALL=", 7) != 0)
            env.push_back(*entry);
    env.push_back(libraries.data());
    env.push_back(locale);
    env.push_back(nullptr);
    const char *program = executable.c_str();
    const char *workDir = directory.c_str();
    char *const *arguments = argv.data();
    char *const *environment = env.data();

    int output[2];
    if (pipe2(output, O_CLOEXEC) < 0) {
        result.error = strerror(errno);
        return result;
    }
    if (fcntl(output[0], F_SETFL, O_NONBLOCK) < 0) {
        result.error = strerror(errno);
        close(output[0]);
        close(output[1]);
        return result;
    }
    const pid_t child = fork();
    if (child == 0) {
        close(output[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0 || dup2(output[1], STDERR_FILENO) < 0)
            _exit(127);
        if (output[1] > STDERR_FILENO)
            close(output[1]);
        if (chdir(workDir) == 0)
            execve(program, arguments, environment);
        constexpr char failure[] = "Cannot start recognition tool\n";
        const auto ignored = write(STDERR_FILENO, failure, sizeof(failure) - 1);
        (void)ignored;
        _exit(127);
    }
    close(output[1]);
    if (child < 0) {
        result.error = strerror(errno);
        close(output[0]);
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    constexpr std::size_t MaxLogBytes = 64 * 1024;
    int status = 0;
    bool reaped = false;
    for (;;) {
        char buffer[4096];
        // Bound each drain pass so an overly noisy child cannot delay timeout checks.
        for (int chunk = 0; chunk < 16; ++chunk) {
            const auto bytes = read(output[0], buffer, sizeof(buffer));
            if (bytes <= 0)
                break;
            result.output.append(buffer, std::min(static_cast<std::size_t>(bytes),
                                                   MaxLogBytes - result.output.size()));
        }
        if (reaped)
            break;
        const auto pid = waitpid(child, &status, WNOHANG);
        if (pid == child) {
            reaped = true;
            continue; // Drain output already buffered at exit.
        }
        if (pid < 0 && errno != EINTR) {
            result.error = strerror(errno);
            break;
        }
        if ((cancel && cancel()) || std::chrono::steady_clock::now() >= deadline) {
            result.error = "tool timed out or was cancelled";
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        pollfd pipeReady = {output[0], POLLIN, 0};
        const int ready = poll(&pipeReady, 1, 50);
        if (ready > 0 && (pipeReady.revents & POLLHUP) && !(pipeReady.revents & POLLIN))
            poll(nullptr, 0, 10);
    }
    close(output[0]);
    result.ok = reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!result.ok && result.error.empty())
        result.error = WIFEXITED(status) ? "exit status " + std::to_string(WEXITSTATUS(status))
                                         : "tool stopped by a signal";
    return result;
}
}
