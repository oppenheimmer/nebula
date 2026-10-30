#include "nebula/app/termination.hpp"
#include <QCoreApplication>
#include <QSocketNotifier>
#include <csignal>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace nebula {
namespace {
int pipe_ends[2] = {-1, -1};
void forward(int signal)
{
    const unsigned char value = static_cast<unsigned char>(signal);
    [[maybe_unused]] const auto written = ::write(pipe_ends[1], &value, 1);
}
} // namespace

void install_termination_handler(std::function<void(int)> callback)
{
    if (pipe_ends[0] < 0) {
        if (::pipe(pipe_ends) != 0)
            throw std::runtime_error("Cannot create the termination pipe");
        for (const int end : pipe_ends) ::fcntl(end, F_SETFD, FD_CLOEXEC);
        ::fcntl(pipe_ends[1], F_SETFL, O_NONBLOCK);
    }
    auto* notifier = new QSocketNotifier(pipe_ends[0], QSocketNotifier::Read, QCoreApplication::instance());
    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [callback](QSocketDescriptor, QSocketNotifier::Type) {
        unsigned char value = 0;
        if (::read(pipe_ends[0], &value, 1) == 1 && callback)
            callback(int(value));
    });
    struct sigaction action{};
    action.sa_handler = forward;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signal : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(signal, &action, nullptr);
}
} // namespace nebula
