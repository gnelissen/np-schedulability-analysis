#ifndef PRECEDENCE_HPP
#define PRECEDENCE_HPP

#include "jobs.hpp"

namespace NP {

	template<class Time>
	class Precedence_constraint {
	public:
		Precedence_constraint(const JobID& from,
			const JobID& to,
			const Interval<Time>& sus_times,
			const Job_lookup& lookup)
			: from(from)
			, to(to)
			, sus_times(sus_times)
		{
			fromIndex = lookup.at(from);
			toIndex = lookup.at(to);
		}

		JobID get_fromID() const
		{
			return from;
		}

		JobID get_toID() const
		{
			return to;
		}

		Time get_minsus() const
		{
			return sus_times.from();
		}

		Time get_maxsus() const
		{
			return sus_times.until();
		}

		Interval<Time> get_suspension() const
		{
			return sus_times;
		}

		Job_index get_toIndex() const
		{
			return toIndex;
		}

		Job_index get_fromIndex() const
		{
			return fromIndex;
		}

	private:
		JobID from;
		JobID to;
		// toIndex and fromIndex are set during validation
		Job_index toIndex;
		Job_index fromIndex;
		Interval<Time> sus_times;
	};

	class InvalidPrecParameter : public std::exception
	{
	public:

		InvalidPrecParameter(const JobID& bad_id)
			: ref(bad_id)
		{}

		const JobID ref;

		virtual const char* what() const noexcept override
		{
			return "invalid precedence constraint parameters";
		}

	};

	template<class Time>
	void validate_prec_cstrnts(std::vector<Precedence_constraint<Time>>& precs)
	{

		for (Precedence_constraint<Time>& prec : precs) {
			if (prec.get_maxsus() < prec.get_minsus()) {
				throw InvalidPrecParameter(prec.get_fromID());
			}
		}
	}
}

#endif
