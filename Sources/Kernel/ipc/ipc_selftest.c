#include "ipc/ipc_selftest.h"

#include <stdint.h>

#include "contracts.h"
#include "debug/panic.h"
#include "ipc/endpoint.h"
#include "mm/mem.h"            // memcmp, memcpy, memset
#include "sched/sched.h"
#include "sched/thread.h"

typedef struct ipc_test_state {
    volatile uint32_t recv_done;
    volatile uint32_t send_done;
    cap_handle_t endpoint;
    task_t *task;
} ipc_test_state_t;

static void receiver_entry(void *arg) {
    ipc_test_state_t *st = (ipc_test_state_t *)arg;
    if (!st || !st->task || !st->task->caps) {
        panic("ipc_selftest: receiver invalid arg");
    }

    ks_ipc_msg_t out;
    memset(&out, 0, sizeof(out));
    ks_ipc_status_t r = ipc_recv_cap(st->task->caps, st->endpoint, &out);
    if (r != KS_IPC_OK) {
        panic("ipc_selftest: recv failed");
    }

    static const char kExpect[] = "ping";
    if (out.tag != 0xC0REu) {
        panic("ipc_selftest: bad tag");
    }
    if (out.len != (uint32_t)(sizeof(kExpect) - 1)) {
        panic("ipc_selftest: bad len");
    }
    if (memcmp(out.data, kExpect, sizeof(kExpect) - 1) != 0) {
        panic("ipc_selftest: bad payload");
    }

    st->recv_done = 1;
    thread_exit();
}

static void sender_entry(void *arg) {
    ipc_test_state_t *st = (ipc_test_state_t *)arg;
    if (!st || !st->task || !st->task->caps) {
        panic("ipc_selftest: sender invalid arg");
    }

    ks_ipc_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    static const char kPayload[] = "ping";
    msg.tag = 0xC0REu;
    msg.len = (uint32_t)(sizeof(kPayload) - 1);
    memcpy(msg.data, kPayload, sizeof(kPayload) - 1);

    ks_ipc_status_t s = ipc_send_cap(st->task->caps, st->endpoint, &msg);
    if (s != KS_IPC_OK) {
        panic("ipc_selftest: send failed");
    }

    st->send_done = 1;
    thread_exit();
}

void ipc_selftest(task_t *task) {
    ASSERT_THREAD_CONTEXT();
    if (!task || !task->caps) {
        panic("ipc_selftest: task invalid");
    }

    ipc_test_state_t st;
    memset(&st, 0, sizeof(st));
    st.task = task;

    // Create an endpoint capability for this task.
    cap_handle_t ep = 0;
    ks_ipc_status_t cr = endpoint_create_cap(task->caps,
                                             (cap_rights_t)(CAP_R_SEND | CAP_R_RECV | CAP_R_DUP | CAP_R_TRANSFER),
                                             &ep);
    if (cr != KS_IPC_OK || ep == 0) {
        panic("ipc_selftest: endpoint_create failed");
    }
    st.endpoint = ep;

    // Create two kernel threads under the same task.
    thread_t *rx = thread_create_named("ipc/rx", receiver_entry, &st);
    thread_t *tx = thread_create_named("ipc/tx", sender_entry, &st);
    if (!rx || !tx) {
        panic("ipc_selftest: thread create failed");
    }
    rx->task = task;
    tx->task = task;

    // Enqueue receiver first so it blocks waiting.
    sched_enqueue(rx);
    sched_enqueue(tx);

    // Wait for completion.
    while (st.recv_done == 0) {
        yield();
    }
}
