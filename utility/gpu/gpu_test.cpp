#include "gpu_tools.h"
#include <iostream>

int main()
{
    std::cout << beam::HasSupportedCard() << std::endl;
}