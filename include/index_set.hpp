#ifndef INDEX_SET_H
#define INDEX_SET_H

namespace NP {

	class Index_set {
	public:

		typedef std::vector<char> Set_type;  // Using char instead of bool

		// New empty job set
		Index_set() : the_set(), count(0) {}

		// Derive a new set by "cloning" an existing set and adding an index
		Index_set(const Index_set &from, std::size_t idx)
				: the_set(from.the_set), count(from.count) {
			if (idx >= the_set.size())
				the_set.resize(idx + 1, 0);
			if (!the_set[idx]) {
				the_set[idx] = 1;
				++count;
			}
		}

		// Create the diff of two job sets (intended for debugging only)
		Index_set(const Index_set &a, const Index_set &b)
				: the_set(std::max(a.the_set.size(), b.the_set.size()), 0), count(0) {
			auto limit = std::min(a.the_set.size(), b.the_set.size());
			for (std::size_t i = 0; i < limit; i++) {
				the_set[i] = a.contains(i) ^ b.contains(i);
				if (the_set[i])
					++count;
			}
			// No need to set elements beyond limit as they are already 0.
		}

		bool operator==(const Index_set &other) const {
			return the_set == other.the_set;
		}

		bool operator!=(const Index_set &other) const {
			return the_set != other.the_set;
		}

		bool contains(std::size_t idx) const {
			return idx < the_set.size() && the_set[idx];
		}

		bool includes(const std::vector<std::size_t> &indices) const {
			for (auto i: indices)
				if (!contains(i))
					return false;
			return true;
		}

		bool is_subset_of(const Index_set &other) const {
			for (std::size_t i = 0; i < the_set.size(); i++)
				if (contains(i) && !other.contains(i))
					return false;
			return true;
		}

		std::size_t size() const {
			return count;
		}

		void add(std::size_t idx) {
			if (idx >= the_set.size())
				the_set.resize(idx + 1, 0);
			if (!the_set[idx]) {
				the_set[idx] = 1;
				++count;
			}
		}

		friend std::ostream &operator<<(std::ostream &stream,
										const Index_set &s) {
			bool first = true;
			stream << "{";
			for (std::size_t i = 0; i < s.the_set.size(); i++)
				if (s.the_set[i]) {
					if (!first)
						stream << ", ";
					first = false;
					stream << i;
				}
			stream << "}";

			return stream;
		}

	private:

		Set_type the_set;
		std::size_t count;  // To store the number of true elements

		// Prevent accidental copying
		Index_set(const Index_set &origin) = delete;

		Index_set &operator=(const Index_set &origin) = delete;
	};
}

#endif