#include "Picker.hpp"

#include <string>

int main(int argc, char** argv) {
    bool allowToken = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string{"--allow-token"})
            allowToken = true;
    }

    CPicker picker(allowToken);
    return picker.run();
}
