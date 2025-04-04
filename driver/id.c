#include "id.h"

DEFINE_ID_GENERATOR(g_id_gen);

// Макросы для генерации и освобождения ID
int generate_id(struct ida *name)
{
    // Используем ida_alloc_range для ограничения максимального значения ID
    // Диапазон [0, MAX_ID_VALUE] включительно.
    int id = ida_alloc_range(name, 0, MAX_ID_VALUE, GFP_KERNEL);

    if (id < 0)
    {
        // -ENOSPC означает, что в заданном диапазоне нет свободных ID
        if (id == -ENOSPC)
        {
            ERR("Cannot allocate ID: No free IDs in range [0, %u]", MAX_ID_VALUE);
        }
        else
        {
            // Другие ошибки, например, -ENOMEM
            ERR("Cannot allocate ID: ida_alloc_range failed with error %d", id);
        }
    }
    return id;
}
void free_id(struct ida *name, int id)
{
    // проверка на валидность id перед освобождением
    if(!IS_ID_VALID(id))
    {
        ERR("Invalid id: %d", id);
        return;
    }


    ida_free(name, id);
}