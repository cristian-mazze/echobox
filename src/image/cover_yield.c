#include "image/cover_yield.h"

CoverYield cover_yield_make(uint32_t (*now_ms)(void *context), bool (*service)(void *context),
                            void *context)
{
    return (CoverYield){
        .now_ms = now_ms, .service = service, .context = context, .slice_ms = COVER_YIELD_SLICE_MS};
}

void cover_yield_begin(CoverYield *yield)
{
    yield->started_ms = yield->now_ms(yield->context);
    yield->longest_ms = 0;
    yield->cancelled = false;
}

bool cover_yield_serve(CoverYield *yield)
{
    if (yield->cancelled) {
        return false;
    }
    uint32_t elapsed = yield->now_ms(yield->context) - yield->started_ms;
    if (elapsed < yield->slice_ms) {
        return true;
    }
    yield->longest_ms = elapsed > yield->longest_ms ? elapsed : yield->longest_ms;
    yield->cancelled = !yield->service(yield->context);
    yield->started_ms = yield->now_ms(yield->context);
    return !yield->cancelled;
}
