/* PS5 implementation of khdays-port's cooperative execution contexts.
 * Source contract: khdays-port v0.1.1, port/host/app/context.h.
 * The PS5 uses the x86-64 System V ABI; context_ps5_x64.S saves its
 * callee-preserved registers and changes stacks without Windows TEB state. */
#include "context.h"

#include <stdint.h>
#include <stdlib.h>

#define KHDAYS_CONTEXT_STACK_SIZE (1024u * 1024u)

struct KhdaysContext {
    void *sp;
    unsigned char *stack;
    size_t size;
    void (*release)(void *stack, size_t size);
    void (*entry)(void *argument);
    void *argument;
};

void khdays_context_jump(void **save_sp, void *load_sp);
void khdays_context_trampoline(void);

static KhdaysContext *current;
static void *(*allocate_stack)(size_t size);
static void (*release_stack)(void *stack, size_t size);

void khdays_context_stack_source(void *(*allocate)(size_t size),
                                 void (*release)(void *stack, size_t size))
{
    allocate_stack = allocate;
    release_stack = release;
}
static void *host_stack(size_t size)
{
    void *stack = NULL;
    return posix_memalign(&stack, 16, size) == 0 ? stack : NULL;
}

static void host_release(void *stack, size_t size)
{
    (void)size;
    free(stack);
}

void khdays_context_started(KhdaysContext *context)
{
    context->entry(context->argument);
    abort();
}

KhdaysContext *khdays_context_init(void)
{
    KhdaysContext *context = (KhdaysContext *)calloc(1, sizeof(*context));
    if (context != NULL) {
        current = context;
    }
    return context;
}

KhdaysContext *khdays_context_current(void)
{
    return current;
}

KhdaysContext *khdays_context_create(void (*entry)(void *argument), void *argument)
{
    KhdaysContext *context = (KhdaysContext *)calloc(1, sizeof(*context));
    uintptr_t *frame;
    uintptr_t top;
    if (context == NULL) {
        return NULL;
    }
    context->size = KHDAYS_CONTEXT_STACK_SIZE;
    context->stack = (unsigned char *)(allocate_stack ? allocate_stack(context->size)
                                                     : host_stack(context->size));
    context->release = allocate_stack ? release_stack : host_release;
    if (context->stack == NULL || context->release == NULL) {
        free(context);
        return NULL;
    }
    context->entry = entry;
    context->argument = argument;

    top = ((uintptr_t)context->stack + context->size) & ~(uintptr_t)15;
    frame = (uintptr_t *)top;
    *--frame = 0; /* sentinel if the trampoline ever returns */
    *--frame = (uintptr_t)khdays_context_trampoline;
    *--frame = 0;                  /* rbp */
    *--frame = (uintptr_t)context; /* rbx: trampoline argument */
    *--frame = 0;                  /* r12 */
    *--frame = 0;                  /* r13 */
    *--frame = 0;                  /* r14 */
    *--frame = 0;                  /* r15 */
    *--frame = ((uint64_t)0x037fu << 32) | 0x1f80u; /* x87 control, MXCSR */
    context->sp = frame;
    return context;
}

void khdays_context_switch(KhdaysContext *to)
{
    KhdaysContext *from = current;
    if (to == NULL || to == from) {
        return;
    }
    current = to;
    khdays_context_jump(&from->sp, to->sp);
}

void khdays_context_delete(KhdaysContext *context)
{
    if (context == NULL) {
        return;
    }
    if (context->stack != NULL) {
        context->release(context->stack, context->size);
    }
    if (current == context) {
        current = NULL;
    }
    free(context);
}

