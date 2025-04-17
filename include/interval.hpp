#ifndef INTERVAL_HPP
#define INTERVAL_HPP

#include <cassert>
#include <ostream>
#include <memory>
#include <vector>
#include <limits>
#include "time.hpp"
#ifdef _WIN32
#include <ciso646>
#endif

template<class T> class Interval {
	T a, b;

	public:

	Interval(const T &a, const T &b)
	{
		if (a > b) {
			this->a = b;
			this->b = a;
		} else {
			this->a = a;
			this->b = b;
		}
	}

	Interval(const std::pair<T, T> p)
	: Interval(p.first, p.second)
	{
	}

	Interval(const Interval<T>& orig)
	: a(orig.a)
	, b(orig.b)
	{
	}

	Interval()
	: a(0)
	, b(0)
	{
	}

	const T& from() const
	{
		return a;
	}

	const T& min() const
	{
		return a;
	}

	const T& starting_at() const
	{
		return a;
	}

	const T& until() const
	{
		return b;
	}

	const T& upto() const
	{
		return b;
	}

	const T& max() const
	{
		return b;
	}

	bool contains(const Interval<T>& other) const
	{
		return from() <= other.from() && other.until() <= until();
	}

	bool contains(const T& point) const
	{
		return from() <= point && point <= until();
	}

	bool disjoint_before(const Interval<T>& other) const
	{
		return other.until() < from();
	}

	bool disjoint_after(const Interval<T>& other) const
	{
		return until() < other.from();
	}

  // RV:  note that  " a.overlap(b) "  is not equal to " !(a.disjoint(b) "
  //      mixing calls to overlap() and disjoint() might be confusing.
	bool overlap(const Interval<T>& other) const
	{
		return (not (disjoint_before(other)) || (disjoint_after(other)));
	}

	bool disjoint(const Interval<T>& other) const
	{
		//consecutive intervals are not considered to be disjoint
		// eg. [2,3] and [4,5] is not considered disjoint, this can be merged to form [2,5]
		bool disjoint;
		disjoint = (other.until() + Time_model::constants<T>::epsilon()) < from() ||
                               (until() + Time_model::constants<T>::epsilon()) < other.from();
		return disjoint;
	}

	bool intersects(const Interval<T>& other) const
	{
		return not disjoint(other);
	}

	bool operator==(const Interval<T>& other) const
	{
		return other.from() == from() && other.until() == until();
	}

	void operator +=(T offset)
	{
		a += offset;
		b += offset;
	}

	Interval<T> operator+(const Interval<T>& other) const
	{
		return {a + other.a, b + other.b};
	}

	Interval<T> operator+(const std::pair<T, T>& other) const
	{
		return {a + other.first, b + other.second};
	}

	Interval<T>& operator+=(const Interval<T>& other)
	{
		a += other.a;
		b += other.b;
		return *this;
	}

	Interval<T> merge(const Interval<T>& other) const
	{
		return Interval<T>{std::min(from(), other.from()),
		                   std::max(until(), other.until())};
	}

	void widen(const Interval<T>& other)
	{
		a = std::min(from(), other.from());
		b = std::max(until(), other.until());
	}

	void equate(const Interval<T>& other)
	{
		a = other.from();
		b = other.until();
	}

	Interval<T> operator|(const Interval<T>& other) const
	{
		return merge(other);
	}

	void operator|=(const Interval<T>& other)
	{
		widen(other);
	}

	void lower_bound(T lb)
	{
		a = std::max(lb, a);
	}

	void upper_bound(T ub)
	{
		a = std::min(ub, b);
	}

	void extend_to(T b_at_least)
	{
		b = std::max(b_at_least, b);
	}

	T length() const
	{
		return until() - from();
	}
};

template<class T> std::ostream& operator<< (std::ostream& stream, const Interval<T>& i)
{
	stream << "I(" << i.from() << ", " << i.until() << ")";
	return stream;
}

#endif
