% get_solve_stats(): returns [exec_time, cpu_max, cpu_avg, gpu_max,
% gpu_avg, ram_max, ram_avg] for the most recently completed solve
% (exec_time in seconds, the rest are percentages 0-100). gpu_max/
% gpu_avg are -1 if no NVIDIA GPU/NVML was available to sample from.
% Raises an error if no solve has completed yet, or if the load
% monitor is disabled (View menu), since it doesn't sample while off.

function z=get_solve_stats()
	z=callfemm('get_solve_stats()');
