#include "racing.h"
#include "../rng.h"
#include "../problem/base_stochastic.h"
#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/students_t.hpp>

#include <utility>

namespace pagmo{ namespace util{
	
namespace racing{
/** --- Rank assignment (before every racing iteration) ---
 * Updates racers with the friedman ranks, assuming that the required individuals
 * in racing_pop have been re-evaluated with the newest seed. Ranking is the same
 * as the one returned by get_best_idx(N), but in case of ties an average rank
 * will be assigned among those who tied.
**/
void FRace_assign_ranks(racing_pool_type& racers, const population& racing_pop)
{
	// Update ranking to be used for stat test
	// Note that the ranking returned by get_best_idx() requires some post-processing,
	// as individuals not currently in race should be ignored.

	typedef population::size_type size_type;

	std::vector<size_type> raw_order = racing_pop.get_best_idx(racing_pop.size());
	int cur_rank = 1;
	std::vector<size_type> ordered_idx_active;
	for(size_type i = 0; i < raw_order.size(); i++){
		int ind_idx = raw_order[i];
		// Note that some individuals would have been dropped along the way and
		// become inactive. They should do not affect the latest rankings in any way.
		if(racers[ind_idx].active){
			racers[ind_idx].m_hist.push_back(cur_rank);
			cur_rank++;
			ordered_idx_active.push_back(ind_idx);
		}
	}
	
	// --Adjust ranking to cater for ties--
	// 1. Check consecutively ranked individuals whether they are tied.
	std::vector<bool> tied(ordered_idx_active.size() - 1, false);
	if(racing_pop.problem().get_f_dimension()==1){
		// Single-objective case
		population::trivial_comparison_operator comparator(racing_pop);
		for(size_type i = 0; i < ordered_idx_active.size() - 1; i++){	
			size_type idx1 = ordered_idx_active[i];
			size_type idx2 = ordered_idx_active[i+1];
			if (!comparator(idx1, idx2) && !comparator(idx2, idx1)){
				tied[i] = true;
			}
		}
	}	
	else{
		// Multi-objective case
		// TODO: crowding distance will now be affected by outdated
		// objective function values wrt current seed.
		population::crowded_comparison_operator comparator(racing_pop);
		for(size_type i = 0; i < ordered_idx_active.size() - 1; i++){	
			size_type idx1 = ordered_idx_active[i];
			size_type idx2 = ordered_idx_active[i+1];
			if (!comparator(idx1, idx2) && !comparator(idx2, idx1)){
				tied[i] = true;
			}
		}
	}
	// std::cout << "Tied stats: "; for(size_type i = 0; i < tied.size(); i++) std::cout << tied[i] <<" "; std::cout<<std::endl;

	// 2. For all the individuals who are tied, modify their rankings to
	// be the average rankings in case of no tie.
	size_type cur_pos = 0;
	size_type begin_avg_pos;
	while(cur_pos < tied.size()){
		begin_avg_pos = cur_pos;
		while(tied[cur_pos]==1){
			cur_pos++;
			if(cur_pos >= tied.size()){
				break;
			}
		}

		double avg_rank = 0;
		// std::cout << "Ties between: ";
		for(size_type i = begin_avg_pos; i <= cur_pos; i++){
			avg_rank += racers[ordered_idx_active[i]].m_hist.back() / ((double)cur_pos - begin_avg_pos + 1);
			// std::cout << ordered_idx_active[i] << " (r=" << racers[ordered_idx_active[i]].m_hist.back() << ") ";
		}
		// std::cout << std::endl;
		// if(cur_pos - begin_avg_pos + 1 > 1)
			// std::cout << "Setting tied ranks between " << cur_pos - begin_avg_pos + 1 << " individuals to be " << avg_rank   << std::endl;
		for(size_type i = begin_avg_pos; i <= cur_pos; i++){
			racers[ordered_idx_active[i]].m_hist.back() = avg_rank;
		}

		// If no tie at all for this begin pos
		if(begin_avg_pos == cur_pos){
			cur_pos++;
		}
	}

	// Update mean of rank (which also reflects the sum of rank, useful for later
	// pair-wise test)
	for(size_type i = 0; i < ordered_idx_active.size(); i++){
		racer_type& cur_racer = racers[ordered_idx_active[i]];
		cur_racer.m_mean = 0;
		for(unsigned int i = 0; i < cur_racer.length(); i++){
			cur_racer.m_mean += (cur_racer.m_hist[i]) / (double)cur_racer.length();
		}
	}

	//std::cout << "Adjusted ranking: "; for(size_type i = 0; i < ordered_idx_active.size(); i++) std::cout << "(" << ordered_idx_active[i] << ")-" << racers[ordered_idx_active[i]].m_hist.back() << " "; std::cout << std::endl;
}

/** --- Rank adjustment (after every racing iteration) ---
 * Once some individuals are removed (de-activated) from the racing pool,
 * adjust the previous ranks of the remaining individuals. The resulted ranks
 * are free of influence from those just-deleted individuals.
**/
void FRace_adjust_ranks(racing_pool_type& racers, const std::vector<population::size_type>& deleted_racers)
{
	for(unsigned int i = 0; i < racers.size(); i++){
		if(!racers[i].active) continue;
		for(unsigned int j = 0; j < racers[i].length(); j++){
			int adjustment = 0;
			for(unsigned int k = 0; k < deleted_racers.size(); k++){
				if(racers[i].m_hist[j] > racers[deleted_racers[k]].m_hist[j]){
					adjustment++;
				}
			}
			racers[i].m_hist[j] -= adjustment;
		}
		//std::cout << "After deleting, rank of [" << i << "] adjusted as: " << racers[i].m_hist << std::endl;
	}
	//TODO: Tie cases not handled yet, worth it?
}


stat_test_result friedman_test(const std::vector<std::vector<double> >& X, double delta)
{	
	// TODO: throw when X is empty
	
	unsigned int N = X.size(); // # of different configurations
	unsigned int B = X[0].size(); // # of different instances

	// std::cout << "N = " << N << " B = " << B << std::endl;

	// (Now obtained the rankings, done with problems and pop.)

	// ----------- Stat. tests  starts-------------

	// Compute mean rank
	std::vector<double> X_mean(N, 0);
	for(unsigned int i = 0; i < N; i++){
		for(unsigned int j = 0; j < X[i].size(); j++){
			X_mean[i] += X[i][j];
		}
		X_mean[i] /= (double)X[i].size();
	}

	// Fill in R and T
	std::vector<double> R(N, 0);
	double A1 = 0;
	double C1 = B * N * (N+1) * (N+1) / 4.0;

	for(unsigned int i = 0; i < N; i++){
		for(unsigned int j = 0; j < B; j++){
			R[i] += X[i][j];
			A1 += (X[i][j])*(X[i][j]);
		}
	}

	double T1 = 0;
	for(unsigned int i = 0; i < N; i++){
		T1 += ((R[i] - B*(N+1)/2.0) * (R[i] - B*(N+1)/2.0));
	}
	T1 *= (N - 1) / (A1 - C1);
	
	using boost::math::chi_squared;
	using boost::math::students_t;
	using boost::math::quantile;

	chi_squared chi_squared_dist(N - 1);

	double delta_quantile = quantile(chi_squared_dist, 1 - delta);

	//std::cout << "T1: "<< T1 << "; delta_quantile = " << delta_quantile << std::endl;

	// Null hypothesis 0: All the ranks observed are equally likely
	bool null_hypothesis_0 = (boost::math::isnan(T1) || T1 < delta_quantile);

	stat_test_result res;

	// True if null hypothesis is rejected -- some differences between
	// the observations will be significant
	res.trivial = null_hypothesis_0;

	res.is_better = std::vector<std::vector<bool> >(N, std::vector<bool>(N, false));

	if(!null_hypothesis_0){	
		std::vector<std::vector<bool> >& is_better = res.is_better;

		students_t students_t_dist((N-1)*(B-1));
		double t_delta2_quantile = quantile(students_t_dist, 1 - delta/2.0);
		double Q = sqrt(((A1-C1) * 2.0 * B / ((B-1) * (N-1))) * (1 - T1 / (B*(N-1))));
		for(unsigned int i = 0; i < N; i++){
			for(unsigned int j = i + 1; j < N; j++){
				double diff_r = fabs(R[i] - R[j]);
				// std::cout<< "diff_r = " << diff_r << std::endl;
				// Check if a pair is statistically significantly different
				if(diff_r > t_delta2_quantile * Q){
					if(X_mean[i] < X_mean[j]){
						is_better[i][j] = true;
					}
					if(X_mean[j] < X_mean[i]){
						is_better[j][i] = true;
					}
				}
			}
		}
	}

	return res;

}

/// Core implementation of the racing routine.
/**
 * Internally it varies the problem's rng seed and re-evaluate the still-active
 * individuals, perform stat. test and drop statistically inferior individual
 * whenever possible. As F-Race is based on rankings between the racing entities,
 * it requires a routine that can determine the ranking of the individual w.r.t.
 * any given rng seed. Here, get_best_idx() function will be used to retrieve
 * such rankings.
 *
 * @param[in] N_final Desired number of winners.
 * @param[in] min_trials Minimum number of trials to be executed before dropping individuals.
 * @param[in] max_count Maximum number of iterations / objective evaluation before the race ends.
 * @param[in] delta Confidence level for statistical testing.
 * @param[in] active_set Indices of individuals that should participate in the race. If empty, race on the whole population.
 *
 * @return Indices of the individuals that remain in the race in the end, a.k.a the winners.
 */
std::vector<population::size_type> race(const population& pop_snapshot, const std::vector<population::size_type>& active_set, const population::size_type N_final, const unsigned int min_trials, const unsigned int max_count, double delta, unsigned int start_seed)
{
	try
	{
		dynamic_cast<const pagmo::problem::base_stochastic &>(pop_snapshot.problem()).get_seed();
	}
	catch (const std::bad_cast& e)
	{
		std::cout << "Warning: Attempt to use racing for a non-stochastic problem. No racing is performed" << std::endl;
		return pop_snapshot.get_best_idx(N_final);
	}

	typedef population::size_type size_type;

	unsigned int cur_seed = start_seed;
	rng_uint32 seeder = rng_generator::get<rng_uint32>();

	race_termination_condition::type term_cond = race_termination_condition::EVAL_COUNT;
	
	// Make a local copy of population dedicated for racing, so that the 
	// get_best_idx() utilities can be used but at the same time the public race()
	// is still a const function. In the following, all problem / population related
	// operations *should* be performed on this local copy.
	population racing_pop(pop_snapshot);

	// Temporary: Consider a fresh start every time when race() is called
	racing_pool_type racers(racing_pop.size(), racer_type());

	// Indices of the racers who are currently active in the pop_snapshot's sense
	// Examples:
	// (1) in_race_list.size() == 5 ---> Only 5 individuals are still being raced.
	// (2) pop_snapshot.get_individual(in_race_list[0]) gives a reference to an actual
	//     individual which is active in the race.
	std::vector<size_type> in_race;
	std::vector<size_type> decided;
	std::vector<size_type> discarded;

	for(size_type i = 0; i < active_set.size(); i++){
		if(active_set[i] >= racing_pop.size()){
			pagmo_throw(value_error,"Racing: Active set contains invalid index.");
		}
		racers[active_set[i]].active = true;
	}

	unsigned int N_begin = 0;

	for(size_type i = 0; i < racers.size(); i++){
		if(racers[i].active){
			in_race.push_back(i);
			N_begin++;
		}
	}

	unsigned int count_iter = 0;
	unsigned int count_nfes = 0;

	while(decided.size() < N_final &&
		  decided.size() + in_race.size() > N_final &&
		  discarded.size() < N_begin - N_final){
		
		// std::cout << "-----Iteration: " << count_iter << std::endl;
		// std::cout << "Decided: " << std::vector<size_type>(decided.begin(), decided.end()) << std::endl;
		// std::cout << "In-race: " << std::vector<size_type>(in_race.begin(), in_race.end()) << std::endl;
		// std::cout << "Discarded: " << std::vector<size_type>(discarded.begin(), discarded.end()) << std::endl;

		// Check if there are enough budget for evaluation
		if(term_cond == race_termination_condition::EVAL_COUNT && count_nfes + in_race.size() > max_count){
			break;
		}

		cur_seed = seeder();
		dynamic_cast<const pagmo::problem::base_stochastic &>(racing_pop.problem()).set_seed(cur_seed);	

		// NOTE: Here after resetting to a new seed, we do not perform re-evaluation of the
		// whole population, as this defeats the purpose of doing race! Only the required
		// individuals (i.e. those still active in racing) shall be re-evaluated. A direct 
		// consequence is that the champion of the population is not valid anymore -- it 
		// doesn't correspond to the latest seed. But this should be OK, as this only affects
		// the local copy the population, which will not be accessed elsewhere, and the
		// champion information is not utilized during racing.

		// Do racing!!
		// Evalute with the new rng seed
		for(std::vector<size_type>::iterator it = in_race.begin(); it != in_race.end(); it++){
	
			// Re-evaluate the individuals under the new seed
			count_nfes++;

			racing_pop.set_x(*it, racing_pop.get_individual(*it).cur_x);

		}

		FRace_assign_ranks(racers, racing_pop);

		// Enforce a minimum required number of trials
		if(count_iter < min_trials)
			continue;

		// Observation data
		std::vector<std::vector<double> > X;
		for(unsigned int i = 0; i < in_race.size(); i++){
			X.push_back(racers[in_race[i]].m_hist);
		}

		// Friedman Test
		stat_test_result ss_result = friedman_test(X, delta);

		if(!ss_result.trivial){
			// Inside here some pairs must be statistically different, let's find them out
		
			const std::vector<std::vector<bool> >& is_better = ss_result.is_better;

			// std::cout << "Null hypothesis 0 rejected!" << std::endl;

			std::vector<size_type> out_of_race;

			std::vector<bool> to_decide(in_race.size(), false), to_discard(in_race.size(), false);

			for(unsigned int i = 0; i < in_race.size(); i++){
				unsigned int vote_decide = 0;
				unsigned int vote_discard = 0;
				for(unsigned int j = 0; j < in_race.size(); j++){
					if(i == j || to_decide[j] || to_discard[j])
						continue;
					// Check if a pair is statistically significantly different
					if(is_better[i][j]){
						vote_decide++;
					}
					else if(is_better[j][i]){
						vote_discard++;
					}
				}
				// std::cout << "[" << *it_i << "]: vote_decide = " << vote_decide << ", vote_discard = " << vote_discard << std::endl;
				if(vote_decide >= N_begin - N_final - discarded.size()){
					to_decide[i] = true;
				}
				else if(vote_discard >= N_final - decided.size()){
				//else if(vote_discard >= 1){ // Equivalent to the previous more aggressive approach
					to_discard[i] = true;
				}
			}

			std::vector<size_type> new_in_race;
			for(unsigned int i = 0; i < in_race.size(); i++){
				if(to_decide[i]){
					decided.push_back(in_race[i]);
				}
				else if(to_discard[i]){
					discarded.push_back(in_race[i]);
				}
				else{
					new_in_race.push_back(in_race[i]);
				}
			}

			in_race = new_in_race;

			// Check if this is that important
			// FRace_adjust_ranks(racers, out_of_race);
		}

		count_iter++;

		bool termination_condition = false;
		if(term_cond == race_termination_condition::EVAL_COUNT){
			termination_condition = (count_nfes >= max_count);
		}
		else if(term_cond == race_termination_condition::ITER_COUNT){
			termination_condition = (count_iter >= max_count);
		}
		if(termination_condition){
			break;
		}

	};

	// If required N_final not met, return the first best N_final indices
	std::vector<std::pair<double, size_type> > argsort;
	if(decided.size() < N_final){	
		for(std::vector<size_type>::iterator it = in_race.begin(); it != in_race.end(); it++){
			argsort.push_back(std::make_pair(racers[*it].m_mean, *it));
		}
		std::sort(argsort.begin(), argsort.end());
		int sorted_idx = 0;
		while(decided.size() < N_final){
			decided.push_back(argsort[sorted_idx++].second);
		}
	}

	std::vector<size_type> winners(decided.begin(), decided.end());

	// std::cout << "Race ends after " << count_iter << " iterations, incurred nfes = " << count_nfes << std::endl;
	// std::cout << "Returning winners: " << std::vector<size_type>(winners.begin(), winners.end()) << std::endl;
	return winners;
}

}}}
