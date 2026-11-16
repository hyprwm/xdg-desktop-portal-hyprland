#include "UUID.hpp"

#include <uuid/uuid.h>

std::string getRandomUUID() {
    uuid_t uuid     = {};
    char   text[37] = {};
    uuid_generate_random(uuid);
    uuid_unparse_lower(uuid, text);
    return text;
}
