/* 
  SZARP: SCADA software 
  

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#ifndef __SZ4_LUA_OPTIMIZED_PARAM_ENTRY_H__
#define __SZ4_LUA_OPTIMIZED_PARAM_ENTRY_H__

#include "szarp_base_common/lua_param_optimizer.h"
#include "sz4/definable_param_cache.h"

namespace sz4 {

class execution_engine : public LuaExec::ExecutionEngine {
	base* m_base;
	LuaExec::Param* m_exec_param;
	std::vector<double> m_vars;
	bool m_initialized;

	void initialize() {
		m_vars.resize(m_exec_param->m_vars.size());

		m_vars[3] = PT_MIN10;
		m_vars[4] = PT_HOUR;
		m_vars[5] = PT_HOUR8;
		m_vars[6] = PT_DAY;
		m_vars[7] = PT_WEEK;
		m_vars[8] = PT_MONTH;
		m_vars[9] = PT_SEC10;
	
		for (size_t i = 0; i < m_vars.size(); i++)
			m_exec_param->m_vars[i].PushExecutionEngine(this);

	}
public:
	execution_engine(base* _base, TParam* param) : m_base(_base), m_exec_param(param->GetLuaExecParam()), m_initialized(false) {
	}

	std::pair<double, bool> calculate_value(second_time_t time, SZARP_PROBE_TYPE probe_type) {
		if (!m_initialized) {
			initialize();
			m_initialized = true;
		}

		m_base->fixed_stack().push(true);
		m_vars[0] = nan("");
		m_vars[1] = time;
		m_vars[2] = probe_type;
		m_exec_param->m_statement->Execute();

		bool fixed = m_base->fixed_stack().top();
		m_base->fixed_stack().pop();
	
		return std::make_pair(m_vars[0], fixed);
	}

	virtual double Value(size_t param_index, const double& time_, const double& probe_type) {
		LuaExec::ParamRef& ref = m_exec_param->m_par_refs[param_index];
		//XXX
		weighted_sum<double, second_time_t> sum;
		m_base->get_weighted_sum(ref.m_param, second_time_t(time_), second_time_t(szb_move_time(time_, 1, SZARP_PROBE_TYPE(probe_type), 0)), SZARP_PROBE_TYPE(probe_type), sum);
		if (sum.weight())
			return sum.sum() / sum.weight();
		else
			return nan("");
		assert(m_base->fixed_stack().size());
		m_base->fixed_stack().top() &= sum.fixed();
	}

	virtual std::vector<double>& Vars() {
		return m_vars;
	}

	~execution_engine() {
		if (m_initialized) for (size_t i = 0; i < m_vars.size(); i++)
			m_exec_param->m_vars[i].PopExecutionEngine();
	}
};

template<class value_type, class time_type> class lua_optimized_param_entry_in_buffer : public SzbParamObserver {
	base* m_base;
	TParam* m_param;
	bool m_invalidate_non_fixed;

	typedef definable_param_cache<value_type, time_type> cache_type;
	typedef std::vector<cache_type> cache_vector;
	cache_vector m_cache;
public:
	lua_optimized_param_entry_in_buffer(base *_base, TParam* param, const boost::filesystem::wpath&) : m_base(_base), m_param(param), m_invalidate_non_fixed(false)
	{
		for (SZARP_PROBE_TYPE p = PT_FIRST; p < PT_LAST; p = SZARP_PROBE_TYPE(p + 1))
			m_cache.push_back(definable_param_cache<value_type, time_type>(p));
	}

	void invalidate_non_fixed_if_needed() {
		if (!m_invalidate_non_fixed)
			return;

		m_invalidate_non_fixed = true;
		std::for_each(m_cache.begin(), m_cache.end(), std::mem_fun_ref(&cache_type::invalidate_non_fixed_values));
	}

	void get_weighted_sum_impl(const time_type& start, const time_type& end, SZARP_PROBE_TYPE probe_type, weighted_sum<value_type, time_type>& sum)  {
		invalidate_non_fixed_if_needed();

		time_type current(start);
		execution_engine ee(m_base, m_param);

		while (current < end) {
			value_type value;
			if (m_cache[probe_type].get_value(current, value) == false) {
				std::pair<double, bool> r = ee.calculate_value(current, probe_type);
				value = r.first;
				m_cache[probe_type].store_value(value, current, r.second);
			}

			time_type next = szb_move_time(current, 1, probe_type);
			if (!value_is_no_data(value)) {
				sum.sum() += value * (next - current);
				sum.weight() += next - current;
			} else
				sum.no_data_weight() += next - current;
			current = next;
		}

	}

	time_type search_data_right_impl(const time_type& start, const time_type& end, SZARP_PROBE_TYPE probe_type, const search_condition& condition) {
		invalidate_non_fixed_if_needed();

		execution_engine ee(m_base, m_param);
		time_type current(start);
		while (true) {
			std::pair<bool, time_type> r = m_cache[probe_type].search_data_right(current, end, condition);
			if (r.first)
				return r.second;
			
			current = r.second;
			std::pair<double, bool> vf = ee.calculate_value(current, probe_type);
			m_cache[probe_type].store_value(vf.first, current, vf.second);
		}

		return invalid_time_value<time_type>::value;
	}

	time_type search_data_left_impl(const time_type& start, const time_type& end, SZARP_PROBE_TYPE probe_type, const search_condition& condition) {
		invalidate_non_fixed_if_needed();

		execution_engine ee(m_base, m_param);
		time_type current(start);
		while (true) {
			std::pair<bool, time_type> r = m_cache[probe_type].search_data_left(current, end, condition);
			if (r.first)
				return r.second;
			current = r.second;

			std::pair<double, bool> vf = ee.calculate_value(current, probe_type);
			m_cache[probe_type].store_value(vf.first, current, vf.second);
		}

		return invalid_time_value<time_type>::value;
	}

	void register_at_monitor(SzbParamMonitor* monitor) {
	}

	void deregister_from_monitor(SzbParamMonitor* monitor) {
	}

	void param_data_changed(TParam*, const std::string& path) {
		m_invalidate_non_fixed = true;
	}

	void reffered_param_removed(generic_param_entry* param_entry) {
		delete m_param->GetLuaExecParam();
		m_param->SetLuaExecParam(NULL);
		//go trough preparation procedure again
		//XXX: reset cache as well
		m_param->SetSz4Type(TParam::SZ4_NONE);
	}

};

}
#endif

