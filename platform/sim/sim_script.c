#include "sim_script.h"

#include <stddef.h>
#include <string.h>

typedef struct KeyName {
    const char *name;
    Key key;
} KeyName;

static const KeyName KEY_NAMES[] = {
    {"up", KEY_VOLUME_DOWN}, {"down", KEY_VOLUME_UP}, {"play", KEY_PLAY},
    {"menu", KEY_MENU},      {"back", KEY_BACK},      {"power", KEY_POWER},
};

static const char LONG_SUFFIX[] = ":long";
static const char HELD_SUFFIX[] = ":held";
static const char WAIT_PREFIX[] = "wait:";

static bool parse_wait(const char *start, size_t length, SimScriptStep *step)
{
    size_t prefix_length = strlen(WAIT_PREFIX);
    if (length <= prefix_length || strncmp(start, WAIT_PREFIX, prefix_length) != 0) {
        return false;
    }
    uint32_t seconds = 0;
    for (size_t index = prefix_length; index < length; index++) {
        if (start[index] < '0' || start[index] > '9' || seconds > SIM_SCRIPT_MAX_WAIT_SECONDS) {
            return false;
        }
        seconds = seconds * 10 + (uint32_t)(start[index] - '0');
    }
    *step = (SimScriptStep){.wait_frames = seconds * 1000 / SIM_SCRIPT_FRAME_MS};
    return seconds > 0 && seconds <= SIM_SCRIPT_MAX_WAIT_SECONDS;
}

static bool has_suffix(const char *start, size_t length, const char *suffix)
{
    size_t suffix_length = strlen(suffix);
    return length > suffix_length &&
           strncmp(start + length - suffix_length, suffix, suffix_length) == 0;
}

static bool parse_key(const char *start, size_t length, SimScriptStep *step)
{
    bool long_press = has_suffix(start, length, LONG_SUFFIX);
    bool held = has_suffix(start, length, HELD_SUFFIX);
    size_t name_length = long_press || held ? length - strlen(LONG_SUFFIX) : length;
    for (size_t index = 0; index < sizeof KEY_NAMES / sizeof KEY_NAMES[0]; index++) {
        const char *name = KEY_NAMES[index].name;
        if (strlen(name) == name_length && strncmp(start, name, name_length) == 0) {
            *step = (SimScriptStep){
                .key = KEY_NAMES[index].key, .long_press = long_press, .held = held};
            return true;
        }
    }
    return false;
}

static bool parse_step(const char *start, size_t length, SimScriptStep *step)
{
    return parse_wait(start, length, step) || parse_key(start, length, step);
}

bool sim_script_parse(const char *text, SimScript *script)
{
    *script = (SimScript){.count = 0};
    const char *start = text;
    for (;;) {
        const char *end = strchr(start, ',');
        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
        if (script->count == SIM_SCRIPT_MAX_STEPS ||
            !parse_step(start, length, &script->steps[script->count])) {
            return false;
        }
        script->count++;
        if (end == NULL) {
            return true;
        }
        start = end + 1;
    }
}

int sim_script_schedule(const SimScript *script, uint32_t first_frame, SimScriptEvent *events)
{
    int count = 0;
    uint32_t frame = first_frame;
    for (int index = 0; index < script->count; index++) {
        const SimScriptStep *step = &script->steps[index];
        if (step->wait_frames > 0) {
            frame += step->wait_frames;
            continue;
        }
        uint32_t hold = step->held         ? SIM_SCRIPT_HELD_FRAMES
                        : step->long_press ? SIM_SCRIPT_HOLD_FRAMES
                                           : SIM_SCRIPT_TAP_FRAMES;
        events[count++] = (SimScriptEvent){.frame = frame, .key = step->key, .pressed = true};
        events[count++] =
            (SimScriptEvent){.frame = frame + hold, .key = step->key, .pressed = false};
        frame += hold + SIM_SCRIPT_GAP_FRAMES;
    }
    return count;
}
