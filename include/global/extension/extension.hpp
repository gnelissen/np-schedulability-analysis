#ifndef EXTENSION_HPP
#define EXTENSION_HPP

#include "global/extension/state_space_data_extension.hpp"
#include "global/extension/state_extension.hpp"

namespace NP {
	namespace Global {

		template<class Time, class State_ext, class State_space_data_ext>
		class Extension
		{
		public:
			Extension() = default;
			virtual ~Extension() = default;

			// Register the extension in the State_extensions and State_space_data_extensions manager
			template<class... Args>
			static void activate(Args&&... args) {
				auto ext_id = State_space_data_extensions::register_extension(
					std::make_unique<State_space_data_ext>(std::forward<Args>(args)...));
				State_extensions<Time>::template register_extension<State_ext>(ext_id);
			}
		};
	}
}

#endif // !EXTENSION_HPP
