#ifndef INDEX_SET_H
#define INDEX_SET_H

#include <stdint.h>

namespace NP {

		class Index_set
		{
			public:
			// Using uint64_t to store 64 bits in each byte
			typedef std::vector<uint64_t> Set_type;

			// new empty job set
			Index_set() : the_set() {}

			// new empty job set of a specific size
			Index_set(size_t s) : the_set() {
				the_set.reserve(s / 64 + 1);
			}

			Index_set(const Index_set& origin) {
				the_set = origin.the_set;
			}

			// derive a new set by "cloning" an existing set and adding an index
			Index_set(const Index_set& from, std::size_t idx)
					: the_set(std::max(from.the_set.size(), (idx / 64) + 1))
			{
				std::copy(from.the_set.begin(), from.the_set.end(), the_set.begin());
				set_bit(idx, true);
			}

			// derive a new set by "cloning" an existing set and adding an index
			void set(const Index_set& from, std::size_t idx)
			{
				the_set.clear();
				the_set.resize(std::max(from.the_set.size(), (idx / 64) + 1));
				std::copy(from.the_set.begin(), from.the_set.end(), the_set.begin());
				set_bit(idx, true);
			}

			// create the diff of two job sets (intended for debugging only)
			Index_set(const Index_set &a, const Index_set &b)
					: the_set(std::max(a.the_set.size(), b.the_set.size()), 0)
			{
				auto limit = std::min(a.the_set.size(), b.the_set.size());
				for (std::size_t i = 0; i < limit; ++i)
					the_set[i] = a.the_set[i] & ~b.the_set[i];
			}

			Index_set& operator=(const Index_set& other)
			{
				if (this != &other) {
					the_set = other.the_set;
				}
				return *this;
			}

			bool operator==(const Index_set &other) const
			{
				return the_set == other.the_set;
			}

			bool operator!=(const Index_set &other) const
			{
				return the_set != other.the_set;
			}

			bool contains(std::size_t idx) const
			{
				if (idx / 64 >= the_set.size()) {
					return false;
				}
				return get_bit(idx);
			}

			bool includes(std::vector<std::size_t> indices) const
			{
				for (auto i : indices)
					if (!contains(i))
						return false;
				return true;
			}

			bool is_subset_of(const Index_set& other) const
			{
				// Compare each integer in the sets
				for (std::size_t i = 0; i < the_set.size(); ++i) {
					// If there are bits set in the current set that are not set in the other set, it's not a subset
					if (the_set[i]>0 && (other.the_set.size()<=i || (the_set[i] & other.the_set[i]) != the_set[i])) {
						return false;
					}
				}

				return true;
			}

			std::size_t size() const
			{
				/*std::size_t count = 0;
				for (std::size_t i = 0; i < the_set.size() * 64; ++i)
					if (contains(i))
						count++;
				return count;*/
				return  the_set.size() * 64;
			}

			void add(std::size_t idx)
			{
				if (idx / 64 >= the_set.size())
					the_set.resize((idx / 64) + 1, 0);
				set_bit(idx, true);
			}

			void clear()
			{
				the_set.clear();
			}

			friend std::ostream& operator<< (std::ostream& stream,
			                                 const Index_set& s)
			{
				bool first = true;
				stream << "{";
				for (std::size_t i = 0; i < s.the_set.size() * 64; ++i) {
					if (s.contains(i)) {
						if (!first)
							stream << ", ";
						first = false;
						stream << i;
					}
				}
				stream << "}";

				return stream;
			}

			private:

			Set_type the_set;

			// Helper functions to set and get individual bits
			void set_bit(std::size_t idx, bool value)
			{
				std::size_t byte_index = idx / 64;
				std::size_t bit_index = idx % 64;
				if (value) {
					the_set[byte_index] |= (((uint64_t)1) << bit_index);
				} else {
					the_set[byte_index] &= ~(((uint64_t)1) << bit_index);
				}
			}

			bool get_bit(std::size_t idx) const
			{
				std::size_t byte_index = idx / 64;
				std::size_t bit_index = idx % 64;
				return the_set[byte_index] & (((uint64_t)1) << bit_index);
			}
		};
}

#endif
