#include "vulkan_template/VulkanTemplate.hpp"

#include <cstdlib>

int main(int argc, char** argv)
{
    auto const runResult{vkt::run()};

    if (runResult != vkt::RunResult::SUCCESS)
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}