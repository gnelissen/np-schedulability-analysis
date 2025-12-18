#ifndef STATE_POOLS_HPP
#define STATE_POOLS_HPP

#include "object_pool.hpp"

namespace NP {

	namespace Global {
		template<class Time> class Cluster_state;
		template<class Time> class Schedule_state;
		template<class Time> class Schedule_node;

		// we define a global object pool for Cluster_state objects to avoid frequent allocations and deallocations
		template<class T>
		static Object_pool<Cluster_state<T>> cluster_pool;

		template<class T, typename... Args>
		std::shared_ptr<Cluster_state<T>> acquire_cluster(Args&&... args)
		{
			return cluster_pool<T>.acquire(std::forward<Args>(args)...);
		}

		template<class T>
		void release_cluster(const std::shared_ptr<Cluster_state<T>>& cluster)
		{
			cluster_pool<T>.release(cluster);
		}

		template<class T>
		void clear_cluster_pool()
		{
			cluster_pool<T>.clear();
		}

		// we define a global object pool for Schedule_state objects to avoid frequent allocations and deallocations
		template<class T>
		static Object_pool<Schedule_state<T>> state_pool;

		template<class T, typename... Args>
		std::shared_ptr<Schedule_state<T>> acquire_state(Args&&... args)
		{
			return state_pool<T>.acquire(std::forward<Args>(args)...);
		}

		template<class T>
		void release_state(const std::shared_ptr<Schedule_state<T>>& state)
		{
			state_pool<T>.release(state);
		}

		template<class T>
		void clear_state_pool()
		{
			state_pool<T>.clear();
		}

		// we define a global object pool for Schedule_node objects to avoid frequent allocations and deallocations
		template<class T>
		static Object_pool<Schedule_node<T>> node_pool;

		template<class T, typename... Args>
		std::shared_ptr<Schedule_node<T>> acquire_node(Args&&... args)
		{
			return node_pool<T>.acquire(std::forward<Args>(args)...);
		}

		template<class T>
		void release_node(const std::shared_ptr<Schedule_node<T>>& node)
		{
			// release all states held by the node
			auto states = node->get_states();
			for (auto s = states.begin(); s != states.end(); s++ ) {
				release_state(*s); 
			}
			// release the node itself
			node_pool<T>.release(node);
		}

		template<class T>
		void clear_node_pool()
		{
			node_pool<T>.clear();
		}
	}
}

#endif // !STATE_POOLS_HPP
