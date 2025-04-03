#include "id.h"

DEFINE_ID_GENERATOR(g_id_gen);

// Макросы для генерации и освобождения ID
int generate_id(struct ida *name)
{
    int id = ida_alloc(name, GFP_KERNEL);
    if (id < 0)
    {
        ERR("Cannot allocate id");
    }
    return id;
}
void free_id(struct ida *name, int id)
{
    ida_free(name, id);
}