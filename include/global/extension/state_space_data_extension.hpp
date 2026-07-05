#ifndef STATE_SPACE_DATA_EXTENSION_HPP
#define STATE_SPACE_DATA_EXTENSION_HPP

#include <vector>
#include <memory>
#include <sstream>
// Forward declaration is sufficient; avoid including state_space_data.hpp here to prevent a circular include

namespace NP {
	namespace Global {

		template<class Time>
		class State_space_data;

		// Base class for state space data extension
		template<class Time>
		class State_space_data_extension
		{
		public:
			State_space_data_extension() = default;
			virtual ~State_space_data_extension() = default;
			virtual std::ostringstream export_results(const State_space_data<Time>& sp_data) const 
			{ 
				return std::ostringstream(); 
			}
			virtual bool success() const { return true; }
		};

		// Manager for all state space data extensions
		template<class Time>
		class State_space_data_extensions
		{
		public:
			State_space_data_extensions() = default;
			virtual ~State_space_data_extensions() = default;

			// Register an extension
			// return the ID of the newly registered extension
			size_t register_extension(std::unique_ptr<State_space_data_extension<Time>> extension) {
				extensions.push_back(std::move(extension));
				return extensions.size() - 1; // Return the index of the registered extension
			}

			template<class Extension, typename... Args>
			inline size_t register_extension(Args&&... args) {
				return register_extension(std::make_unique<Extension>(std::forward<Args>(args)...));
			}

			// Get an extension by ID
			template<typename Extension>
			Extension* get(size_t id) const {
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
			std::vector<std::unique_ptr<State_space_data_extension<Time>>> extensions;
		};
	}
}

#endif // !STATE_SPACE_DATA_EXTENSION_HPP
