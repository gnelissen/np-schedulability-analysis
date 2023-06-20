#ifndef TIME_HPP
#define TIME_HPP

#include <utility>
#include <limits>

// discrete time
using dtime_t = long long;

// dense time
using dense_t = double;

namespace Time_model {

	template<typename T>
	struct constants
	{
		static constexpr T infinity()
		{
			return std::numeric_limits<T>::max();
		}

		// minimal time distance before some event
		static constexpr T epsilon()
		{
			return T(1);
		}

		// a deadline miss of a magnitude of less than the given tolerance is
		// ignored as noise
		static constexpr T deadline_miss_tolerance()
		{
			return T(0);
		}
	};


	template<>
	struct constants<dense_t>
	{
		static constexpr dense_t infinity()
		{
			return std::numeric_limits<dense_t>::infinity();
		}

		static constexpr dense_t epsilon()
		{
			return std::numeric_limits<dense_t>::epsilon();
		}

		static constexpr dense_t deadline_miss_tolerance()
		{
			// assuming we work with microseconds, this is one picosecond
			// (i.e., much less than one processor cycle)
			return 1E-6;
		}

	};

}


#endif

