#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "global/state.hpp"

#include <deque>
#include <memory>
#include <set>
#include <map>
#include <limits>
#include <iostream>

namespace NP {
	namespace Global {

		struct Dot_file_config {
			// info on edge label
			bool print_dispatched_job = true; // whether to print the dispatched jobs in the dot file
			bool print_start_times = true; // whether to print the start times of the jobs in the dot file
			bool print_finish_times = true; // whether to print the finish times of the jobs in the dot file
			bool print_parallelism = true; // whether to print the parallelism of the jobs in the dot file
			bool print_deadline = false; // whether to print the deadline of the jobs in the dot file
			// info on states
			bool print_core_availability = true; // whether to print the core availability in the dot file
			bool print_certainly_running_jobs = false; // whether to print the certainly running jobs in the dot file
			bool print_pred_finish_times = false; // whether to print the predecessor finish times in the dot file
			// info on nodes
			bool print_node_key = false; // whether to print the node keys in the dot file
			bool print_ready_successors = true; // whether to print the ready successors in the dot file
		};
		
		
		template <typename Time>
		class Logging_condition {
			typedef std::shared_ptr<Schedule_node<Time>> Node_ptr;

			// the logger stores the state transitions based on the following conditions
			// - the monitored_interval interval in which the job is dispatched
			Interval<Time> monitored_interval;
			// - the monitored_depth of the SAG (how many monitored_jobs have been dispatched so far)
			Interval<unsigned long> monitored_depth;
			// - the task dispatched
			std::set<unsigned long> monitored_tasks;
			// - the job dispatched
			std::set<Job_index> monitored_jobs;
			// - the set of monitored_jobs that have been dispatched so far
			std::set<Job_index> must_be_dispatched;
			// - the set of monitored_jobs that have not been dispatched so far
			std::set<Job_index> must_not_be_dispatched;
			// - whether the job misses its deadline or not
			bool deadline_miss;
			bool always_log = false;

		public:
			// default constructor (monitors everything)
			Logging_condition()
				: always_log(true)
			{
			}

			Logging_condition(Interval<Time> t, Interval<unsigned long> depth, std::set<unsigned long> tasks,
				std::set<Job_index> jobs, std::set<Job_index> dispatched, std::set<Job_index> not_dispatched, 
				bool deadline_miss)
				: monitored_interval(t)
				, monitored_depth(depth)
				, monitored_tasks(tasks)
				, monitored_jobs(jobs)
				, must_be_dispatched(dispatched)
				, must_not_be_dispatched(not_dispatched)
				, deadline_miss(deadline_miss)
			{
				// check if we should log everything
				if (deadline_miss == false && monitored_depth.min() == 0 && monitored_depth.max() == std::numeric_limits<unsigned long>::max()
					&& monitored_interval.min() == 0 && monitored_interval.max() == Time_model::constants<Time>::infinity()
					&& monitored_tasks.empty() && monitored_jobs.empty() && dispatched.empty() && not_dispatched.empty())

				{
					always_log = true;
				}
			}

			// check if the logging condition is true for the given job, start time and depth
			bool is_true(const Node_ptr& n, const Job<Time>& dispatched_j, const Interval<Time>& start,
				bool deadline_missed, unsigned long depth) const
			{
				if (always_log) {
					return true;
				}

				// check if all jobs in dispatched have been dispatched
				for (Job_index j : must_be_dispatched) {
					if (n->job_not_dispatched(j)) {
						return false;
					}
				}

				// check if all jobs in not_dispatched have not been dispatched
				for (Job_index j : must_not_be_dispatched) {
					if (n->job_dispatched(j)) {
						return false;
					}
				}

				return (!deadline_miss || (deadline_miss && deadline_missed))
					&& (depth >= monitored_depth.min() && depth <= monitored_depth.max())
					&& (start.max() >= monitored_interval.min() && start.min() <= monitored_interval.max())
					&& (monitored_jobs.empty() || monitored_jobs.find(dispatched_j.get_job_index()) != monitored_jobs.end())
					&& (monitored_tasks.empty() || monitored_tasks.find(dispatched_j.get_task_id()) != monitored_tasks.end());
			}
		};

		template<class Time>
		struct Log_options {
			// Should we log the SAG?
			bool log = false;
			Logging_condition<Time> log_cond;
		};

		template <typename Time>
		class SAG_logger {
			typedef std::shared_ptr<Schedule_node<Time>> Node_ptr;
			typedef std::shared_ptr<Schedule_state<Time>> State_ptr;
			struct Edge {
				const Job<Time>* scheduled;
				const Node_ptr source, target;
				const Interval<Time> finish_range;
				const unsigned int parallelism;

				Edge(const Job<Time>* s, const Node_ptr& src, const Node_ptr& tgt,
					const Interval<Time>& fr, unsigned int parallelism = 1)
					: scheduled(s)
					, source(src)
					, target(tgt)
					, finish_range(fr)
					, parallelism(parallelism)
				{
				}

				bool deadline_miss_possible() const
				{
					return scheduled->exceeds_deadline(finish_range.upto());
				}

				Time earliest_finish_time() const
				{
					return finish_range.from();
				}

				Time latest_finish_time() const
				{
					return finish_range.upto();
				}

				Time earliest_start_time() const
				{
					return finish_range.from() - scheduled->least_exec_time();
				}

				Time latest_start_time() const
				{
					return finish_range.upto() - scheduled->maximal_exec_time();
				}

				unsigned int parallelism_level() const
				{
					return parallelism;
				}
			};

			std::deque<Edge> edges;

			// the logger stores the state transitions based on the following conditions
			Logging_condition<Time> condition;

		public:
			// default constructor (monitors everything)
			SAG_logger()
			{
			}

			SAG_logger(const Logging_condition<Time>& cond)
				: condition(cond)
			{
			}

			// save the dispatch of job j between old_node and new_node
			void log_job_dispatched(const Node_ptr& old_node, const Job<Time>& dispatched_j,
				const Interval<Time>& start, Interval<Time> finish, unsigned int parallelism,
				const Node_ptr& new_node, unsigned long depth) {
				if (condition.is_true(new_node, dispatched_j, start, dispatched_j.exceeds_deadline(finish.upto()), depth)) {
					// create an edge and add it to the edge queue
					edges.emplace_back(&dispatched_j, old_node, new_node, finish, parallelism);
				}
			}

			void print_dot_file(std::ostream& out,
				const typename Job<Time>::Job_set& jobs,
				const Dot_file_config& conf) const
			{
				std::map<const Schedule_node<Time>*, unsigned int> node_id;
				unsigned int i = 0;

				out << "digraph {" << std::endl;
				for (const Edge& e : edges) {
					// check if we already printed the nodes
					if (node_id.count(e.source.get()) == 0) {
						node_id[e.source.get()] = i;
						print_vertex(out, i, e.source, jobs, conf);
						i++;
					}
					if (node_id.count(e.target.get()) == 0) {
						node_id[e.target.get()] = i;
						print_vertex(out, i, e.target, jobs, conf);
						i++;
					}

					print_edge(out, e, node_id[e.source.get()], node_id[e.target.get()], conf);
				}
				out << "}" << std::endl;
			}

		private:
			
			void print_vertex(std::ostream& out, int id, const Node_ptr& n,
				const typename Job<Time>::Job_set& jobs, const Dot_file_config& conf) const
			{
				out << "\tN" << id
					<< "[label=\"N" << id << ":";
				if (conf.print_node_key) 
					out << " Key=" << n->get_key();
				const auto* n_states = n->get_states();

				int i = 0;
				for (const State_ptr& s : *n_states)
				{
					out << "\\n---S" << i++ << "---";

					if (conf.print_core_availability) {
						out << "\\nAvail:{";
						for (const auto& a : s->get_cores_availability()) {
							out << "[" << a.from() << ", " << a.until() << "]";
						}
						out << "}";
					}

					if (conf.print_certainly_running_jobs) {
						bool first = true;
						out << "\\nRun:{";
						for (const auto& rj : s->get_cert_running_jobs()) {
							if (!first)
								out << ", ";
							out << "T" << jobs[rj.idx].get_task_id()
								<< "J" << jobs[rj.idx].get_job_id() << ":["
								<< rj.finish_time.min() << ", " << rj.finish_time.max() << "]";
							first = false;
						}
						out << "}";
					}

					if (conf.print_pred_finish_times) {
						// TODO: implement this
					}
				}
				out << "\\n---------";

				if(conf.print_ready_successors) {
					out << "\\nReady:{";
					bool first = true;
					for (const Job<Time>* rj : n->get_ready_successor_jobs()) {
						if (!first)
							out << ", ";
						out << "T" << jobs[rj->get_job_index()].get_task_id()
							<< "J" << jobs[rj->get_job_index()].get_job_id();
						first = false;
					}
					out << "}";
				}				
				out << "\"];"
					<< std::endl;
			}

			void print_edge(std::ostream& out, const Edge& e, int source, int target, const Dot_file_config& conf) const
			{
				out << "\tN" << source
					<< " -> "
					<< "N" << target
					<< "[label=\"";
				if (conf.print_dispatched_job) {
					out << "T" << e.scheduled->get_task_id()
						<< " J" << e.scheduled->get_job_id();
				}
				if (conf.print_deadline) {
					out << "\\nDL=" << e.scheduled->get_deadline();
				}
				if (conf.print_start_times) {
					out << "\\nStart=[" << e.earliest_start_time()
						<< ", " << e.latest_start_time() << "]";
				}
				if (conf.print_finish_times) {
					out << "\\nFinish=[" << e.earliest_finish_time()
						<< ", " << e.latest_finish_time() << "]";
				}
				if (conf.print_parallelism) {
					out << "\\nParal=" << e.parallelism_level();
				}
				out << "\"";
				if (e.deadline_miss_possible()) {
					out << ",color=Red,fontcolor=Red";
				}
				out << ",fontsize=8" << "]"
					<< ";"
					<< std::endl;
				if (e.deadline_miss_possible()) {
					out << "N" << target
						<< "[color=Red];"
						<< std::endl;
				}
			}
		};
	}
}
#endif // LOGGER_HPP
