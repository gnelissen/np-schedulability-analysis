// Central definitions of static extension registries (avoid multiple definitions)
#include "global/extension/problem_extension.hpp"
#include "global/extension/state_space_data_extension.hpp"

namespace NP {
    // Definition of Problem_extensions::extensions
    std::vector<std::unique_ptr<Problem_extension_base>> Problem_extensions::extensions;
}

namespace NP { namespace Global {
    // Definition of State_space_data_extensions::extensions
    std::vector<std::unique_ptr<State_space_data_extension>> State_space_data_extensions::extensions;
}} // namespace NP::Global
