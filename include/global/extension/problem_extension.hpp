#ifndef PROBLEM_EXTENSION_HPP
#define PROBLEM_EXTENSION_HPP

#include <vector>
#include <memory>

namespace NP
{
	namespace Global {
		
		// Base class for problem extension
		struct Problem_extension_base {
			virtual ~Problem_extension_base() = default;
		};

		class Problem_extensions
		{
		public:
			Problem_extensions() = default;
			virtual ~Problem_extensions() = default;

			// Register an extension
			static size_t register_extension(std::unique_ptr<Problem_extension_base> extension) {
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
			static std::vector<std::unique_ptr<Problem_extension_base>> extensions;
		};

		template<typename Derived>
		struct Problem_extension : public Problem_extension_base
		{
			Problem_extension() = default;
			virtual ~Problem_extension() = default;

			template<typename ...Args>
			static size_t register_extension(Args&& ...args) {
				return Problem_extensions::register_extension(std::make_unique<Derived>(std::forward<Args>(args)...));
			}
		};

	} // namespace Global
} // namespace NP

#endif // !PROBLEM_EXTENSION_HPP
