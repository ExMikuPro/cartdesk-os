#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "task_messages.h"

#define TEST_QUEUE_DEPTH 2u

typedef enum {
    TEST_REQUEST_FREE = 0,
    TEST_REQUEST_QUEUED,
    TEST_REQUEST_COMPLETED,
    TEST_REQUEST_RELEASED,
} test_request_state_t;

typedef struct {
    uint32_t request_id;
    uint32_t owner_id;
    uint32_t deadline;
    test_request_state_t state;
    bool buffer_owned;
} test_request_t;

typedef struct {
    test_request_t slots[TEST_QUEUE_DEPTH];
    uint32_t next_id;
    uint32_t stale;
} test_model_t;

static uint32_t next_id(test_model_t *model)
{
    uint32_t id = ++model->next_id;
    if (id == 0u) id = ++model->next_id;
    return id;
}

static test_request_t *submit(test_model_t *model, uint32_t owner_id,
                              uint32_t now, uint32_t timeout, bool owns_buffer)
{
    for (size_t i = 0u; i < TEST_QUEUE_DEPTH; ++i) {
        if (model->slots[i].state == TEST_REQUEST_FREE ||
            model->slots[i].state == TEST_REQUEST_RELEASED) {
            model->slots[i] = (test_request_t) {
                .request_id = next_id(model),
                .owner_id = owner_id,
                .deadline = now + timeout,
                .state = TEST_REQUEST_QUEUED,
                .buffer_owned = owns_buffer,
            };
            return &model->slots[i];
        }
    }
    return NULL;
}

static cart_io_status_t begin(test_request_t *request, uint32_t now,
                              uint32_t cancelled_owner)
{
    if (request->owner_id != 0u && request->owner_id == cancelled_owner)
        return CART_IO_STATUS_CANCELLED;
    if ((int32_t)(now - request->deadline) >= 0)
        return CART_IO_STATUS_TIMEOUT;
    return CART_IO_STATUS_OK;
}

static bool complete(test_request_t *request)
{
    if (request->state != TEST_REQUEST_QUEUED) return false;
    request->state = TEST_REQUEST_COMPLETED;
    return true;
}

static bool release_once(test_request_t *request)
{
    if (request->state != TEST_REQUEST_COMPLETED || !request->buffer_owned)
        return false;
    request->buffer_owned = false;
    request->state = TEST_REQUEST_RELEASED;
    return true;
}

static void receive(test_model_t *model, test_request_t *request,
                    uint32_t active_owner)
{
    if (request->owner_id != 0u && request->owner_id != active_owner)
        ++model->stale;
}

int main(void)
{
    test_model_t model = {0};
    test_request_t *first = submit(&model, 7u, 100u, 50u, true);
    test_request_t *second = submit(&model, 8u, 100u, 10u, true);
    assert(first && second);
    assert(first->request_id != second->request_id);
    assert(submit(&model, 9u, 100u, 10u, false) == NULL); /* queue full */

    assert(begin(first, 120u, 0u) == CART_IO_STATUS_OK);
    assert(complete(first));
    assert(!complete(first)); /* duplicate completion rejected */
    receive(&model, first, 7u);
    assert(model.stale == 0u);
    assert(release_once(first));
    assert(!release_once(first)); /* duplicate release rejected */

    assert(begin(second, 111u, 0u) == CART_IO_STATUS_TIMEOUT);
    assert(begin(second, 105u, 8u) == CART_IO_STATUS_CANCELLED);
    assert(complete(second));
    receive(&model, second, 9u);
    assert(model.stale == 1u);
    assert(release_once(second)); /* failure/cancel still returns ownership */

    cart_io_request_t request = {0};
    request.request_id = next_id(&model);
    request.owner_id = 42u;
    request.operation = CART_IO_OP_STORAGE_COMMIT;
    request.timeout_ms = 3000u;
    request.params.storage.payload = (cart_task_buffer_t) {
        .data = (void *)(uintptr_t)0x1000u,
        .capacity = 64u,
        .length = 32u,
        .owner_id = 42u,
        .source = CART_BUFFER_SOURCE_RTOS_HEAP,
    };
    assert(request.params.storage.payload.length <=
           request.params.storage.payload.capacity);
    assert(request.params.storage.payload.owner_id == request.owner_id);
    assert(sizeof(request) <= 96u);
    assert(sizeof(cart_io_completion_t) <= 48u);
    return 0;
}
