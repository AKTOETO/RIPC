#ifndef ID_GENERATOR_H
#define ID_GENERATOR_H

#include <linux/idr.h>
#include "err.h"
#include "id_pack.h"

// глобальный генератор id
extern struct ida g_id_gen;

// определение генератора
#define DEFINE_ID_GENERATOR(name) DEFINE_IDA(name)

// удаление генератора
#define DELETE_ID_GENERATOR(name) ida_destroy(name)

// Макросы для генерации и освобождения ID
int generate_id(struct ida *name);
void free_id(struct ida *name, int id);

#endif /* ID_GENERATOR_H */
