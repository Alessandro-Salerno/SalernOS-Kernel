#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/powerman.h>

// MODEL: our model creates an implicit state machine: an event  can request a
// system shutdown, which we implement by signaling the init process, which is
// then responsible for terminating the userspace session, and ACK'ing in a
// manner similar to that used by KTTY. The code is spread between this file,
// architecture-specific files, and options to hopefully build a coherent
// abstraction

int powerman_request_shutdown(void) {
    return signal_send_to_proc(1, SIGUSR2, NULL);
}
