#include "config_template.hpp"

#include <config_template_data.hpp>
#include <google_config_data.hpp>
#include <llvm_config_data.hpp>
#include <ytsaurus_config_data.hpp>

#include <ostream>

namespace cli {

void PrintConfig(std::ostream& output, ConfigPreset preset)
{
    switch (preset) {
        case ConfigPreset::Default:
            output << ConfigTemplateText;
            break;
        case ConfigPreset::YTsaurus:
            output << YTsaurusConfigText;
            break;
        case ConfigPreset::Google:
            output << GoogleConfigText;
            break;
        case ConfigPreset::LLVM:
            output << LLVMConfigText;
            break;
    }
}

} // namespace cli
