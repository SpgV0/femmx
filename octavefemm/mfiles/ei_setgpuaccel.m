% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-21.
%
% ei_setgpuaccel(flag): flag=1 asks belasolv.exe to try its optional
% CUDA-accelerated linear solve for this problem; it transparently falls
% back to the normal CPU solve if belasolv.exe wasn't built with CUDA
% support, or if the GPU solve fails for any reason. flag=0 (default) is
% CPU-only. Persisted in the .fee file as [GPUAccel], mirroring
% mi_setgpuaccel.m.

function ei_setgpuaccel(n)
callfemm(['ei_setgpuaccel(' , num(n), ')' ]);
