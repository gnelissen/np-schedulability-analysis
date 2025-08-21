#ifndef STATE_SPACE_DATA_EXTENSION_HPP
#define STATE_SPACE_DATA_EXTENSION_HPP

#include <vector>
#include <memory>

namespace NP {
	namespace Global {

		class State_space_data_extension
		{
		public:
			State_space_data_extension() = default;
			virtual ~State_space_data_extension() = default;
		};

		class State_space_data_extensions
		{
		public:
			State_space_data_extensions() = default;
			virtual ~State_space_data_extensions() = default;

			// Register an extension
			// return the ID of the newly registered extension
			static size_t register_extension(std::unique_ptr<State_space_data_extension> extension) {
				extensions.push_back(std::move(extension));
				return extensions.size() - 1; // Return the index of the registered extension
			}

			// Get an extension by ID
			template<typename Extension>
			static Extension* get(size_t id) {
				if (id < extensions.size()) {
					return static_cast<Extension*>(extensions[id].get());
				}
				return nullptr; // Out of bounds
			}

			// Get an extension by type
			template<typename Extension>
			Extension* get() const {
				for (auto& ext : extensions) {
					if (auto casted = dynamic_cast<Extension*>(ext.get())) {
						return casted;
					}
				}
				return nullptr; // Not found
			}

		private:
			static std::vector<std::unique_ptr<State_space_data_extension>> extensions; // defined in src/extensions.cpp
		};
	}
}

#endif // !STATE_SPACE_DATA_EXTENSION_HPP
