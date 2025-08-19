#ifndef STATE_EXTENSION_HPP
#define STATE_EXTENSION_HPP

#include <vector>
#include <memory>
#include <typeinfo>
#include <type_traits>
#include "jobs.hpp"
#include "interval.hpp"
#include "global/state_space_data.hpp"
#include "global/state.hpp"

namespace NP {
	namespace Global {
		typedef Index_set Job_set;

		template<class Time> class Schedule_state;
		template<class Time> class State_space_data;

		template<class Time>
		class State_extension
		{
		public:
			State_extension() = default;
			virtual ~State_extension() = default;

			virtual void construct(const size_t extension_id, 
				const size_t state_space_data_ext_id, 
				const Schedule_state<Time>& new_state, 
				const unsigned int num_processors, 
				const State_space_data<Time>& state_space_data) 
			{
			}
			
			virtual void construct(const size_t extension_id,
				const size_t state_space_data_ext_id,
				const Schedule_state<Time>& new_state,
				const std::vector<Interval<Time>>& proc_initial_state, 
				const State_space_data<Time>& state_space_data)
			{
			}

			virtual void construct(const size_t extension_id,
				const size_t state_space_data_ext_id,
				const Schedule_state<Time>& new_state,
				const Schedule_state<Time>& from,
				Job_index j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_index>& jobs_with_pending_succ,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
			{
			}
			
			virtual void reset(const size_t extension_id,
				const size_t state_space_data_ext_id,
				const Schedule_state<Time>& new_state,
				const unsigned int num_processors,
				const State_space_data<Time>& state_space_data)
			{
			}

			virtual void reset(const size_t extension_id,
				const size_t state_space_data_ext_id,
				const Schedule_state<Time>& new_state,
				const std::vector<Interval<Time>>& proc_initial_state,
				const State_space_data<Time>& state_space_data)
			{
			}

			virtual void reset(const size_t extension_id,
				const size_t state_space_data_ext_id,
				const Schedule_state<Time>& new_state,
				const Schedule_state<Time>& from,
				Job_index j,
				const Interval<Time>& start_times,
				const Interval<Time>& finish_times,
				const Job_set& scheduled_jobs,
				const std::vector<Job_index>& jobs_with_pending_succ,
				const std::vector<const Job<Time>*>& ready_succ_jobs,
				const State_space_data<Time>& state_space_data,
				Time next_source_job_rel,
				unsigned int ncores = 1)
			{
			}
			
			// merge this extension with another one of the same type
			virtual void merge(size_t extension_id, const Schedule_state<Time>& this_state, const Schedule_state<Time>& other) {}
		};

		template<class Time>
		class State_extensions {
		public:
			explicit State_extensions() {
				create_extensions();
			}

			// register an extension of type Extension in the State_extensions manager
			// return the ID of the newly registered extension
			template<class Extension>
			static size_t register_extension(size_t state_space_data_ext_ID) {
				// add one constructor for each signature in Constructor_signatures
				add_constructor_for<Extension>();
				state_space_data_extensions.push_back(state_space_data_ext_ID);
				return get_registry().size() - 1; // return the index of the newly registered extension
			}

			// returns a const pointer to the extension with id, or nullptr if not found
			template<class Extension>
			const Extension* operator[](size_t id) const {
				if (id < extensions.size()) {
					return static_cast<Extension*>(extensions[id].get());
				}
				return nullptr; // Out of bounds
			}

			// returns a const pointer to the extension with id, or nullptr if not found
			template<class Extension>
			const Extension* get(size_t id) const {
				if (id < extensions.size()) {
					return static_cast<Extension*>(extensions[id].get());
				}
				return nullptr; // Out of bounds
			}

			// returns a pointer to the extension of type Extension, or nullptr if not found
			template<class Extension>
			Extension* get() {
				// Find the extension by comparing type_info
				for (const auto& ext : extensions) {
					if (auto casted = dynamic_cast<Extension*>(ext.get())) {
						// If found, cast the void* back to the correct type
						return casted;
					}
				}
				return nullptr; // Not found
			}

			template<class... Args>
			void construct(Args&&... args) {
				for (size_t i = 0; i < extensions.size(); i++) {
					extensions[i]->construct(i, state_space_data_extensions[i], std::forward<Args>(args)...);
				}
			}

			template<class... Args>
			void reset(Args&&... args) {
				for (size_t i = 0; i < extensions.size(); i++) {
					extensions[i]->reset(i, state_space_data_extensions[i], std::forward<Args>(args)...);
				}
			}

			// merge all extensions with their counter-part in `other`
			void merge(const Schedule_state<Time>& this_state, const Schedule_state<Time>& other)
			{
				for (size_t i = 0; i < extensions.size(); i++) {
					extensions[i]->merge(i, this_state, other);
				}
			}

		private:			
			// vector of all registered extensions after their construction
			std::vector<std::unique_ptr<State_extension<Time>>> extensions;
			// vector of state space data extension IDs for each registered extension
			static std::vector<size_t> state_space_data_extensions;

			// type-erased creator function to construct extensions with different signatures
			using Creator = std::function<std::unique_ptr<State_extension<Time>>()>;

			// registry of constructors for each signature of the extensions
			static std::vector<Creator>& get_registry() {
				static std::vector<Creator> v;
				return v;
			}

			// add a constructor for a specific extension type and signature
			template<class Extension>
			static void add_constructor_for() {
				auto& reg = get_registry();
				reg.push_back([]() { return std::make_unique<Extension>(); });
			}

			// construct all registered extensions with the given arguments
			void create_extensions() {
				auto& reg = get_registry();
				for (auto& c : reg) {
					extensions.push_back(c());
				}
			}
		};

		template<class Time> std::vector<size_t> State_extensions<Time>::state_space_data_extensions;
	}
}
#endif // !STATE_EXTENSION_HPP
