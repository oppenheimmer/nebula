#pragma once
#include <functional>

namespace nebula {
// Delivers SIGTERM/SIGINT/SIGHUP to the Qt event loop so pending edits can be
// flushed before the process exits (for example on `docker stop`). The handler
// itself only writes one byte to a pipe; the callback runs on the GUI thread.
void install_termination_handler(std::function<void(int signal)> callback);
} // namespace nebula
