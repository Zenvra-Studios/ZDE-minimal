#pragma once

#include <string>

namespace Zenvra::Utility
{

class Doctor
{
public:
    /**
     * @brief Generates a comprehensive plain-text diagnostic report.
     * @return Formatted multi-line diagnostic report.
     */
    static std::string generate_report();

    /**
     * @brief Prints the diagnostic report directly to standard output.
     */
    static void print_report();
};

} // namespace Zenvra::Utility
