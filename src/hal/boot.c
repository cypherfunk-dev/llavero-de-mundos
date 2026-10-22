/* Lógica común de arranque y guardado, escrita solo con la HAL. */
#include <string.h>

#include "hal.h"

bool hal_boot_engine(Engine *e, uint64_t seed)
{
    static uint8_t buf[LLV_SAVE_MAX];
    int64_t now = hal_clock_local_seconds();
    uint32_t ms = hal_clock_ms();
    size_t n = hal_storage_load(buf, sizeof(buf));
    GameState loaded;
    if (n > 0 && state_deserialize(&loaded, buf, n) == 0) {
        engine_resume(e, &loaded, now, ms);
        return true;
    }
    engine_init(e, seed, now, ms);
    return false;
}

bool hal_save_engine(Engine *e)
{
    static uint8_t buf[LLV_SAVE_MAX];
    if (!e->asleep)
        engine_sleep(e, hal_clock_local_seconds());
    size_t n = state_serialize(&e->gs, buf, sizeof(buf));
    return n > 0 && hal_storage_save(buf, n);
}
